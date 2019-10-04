---
title: "Forward progress delegation for executors"
document: D1898R0
date: 2019-09-14
audience: SG1
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---

# Introduction

This is an early proposal to sound out one approach to forward progress delegation for executors, based in part on a discussion of folly’s `DeferredExecutor`.

Building on the definitions in [@P0443R11] and [@P1660R0], we start with the definition of a `Callback` we propose adding a concept `WithExecutor` that allows a `Sender` to require that a `Callback` passed to it is able to provide an executor.
This concept offers a `get_executor` method that allows and executor to be propagated from downstream to upstream asynchronous tasks.
A given `Executor`, and therefore any `Sender` created off that executor may require the availability of such a method on `Callback`s passed to the `submit()` method.

An executor may delegate any tasks it is unable to perform to the executor it acquires from the downstream `Callback`.

A `sync_wait` operation, as defined in PXXX, encapsulates an `Executor` that is iterated directly in the calling thread.
This executor will make progress and as such can act as the executor-of-last-resort when the C++20 guarantee is required in blocking code.

We also propose that a set of definitions for forward progress of asynchronous graph nodes be provided.

# Motivation and Scope
## Forward progress delegation in C++20.
The standard parallel algorithms support weak forward progress guarantees.
For example, with the code:
```cpp
vector<int> vec{1, 2, 3};
vector<int> out(vec.size(), 0);
transform(
  std::execution::par,
  begin(vec),
  end(vec),
  begin(out),
  [](int i){return i+1;} // s0
  ); // s1
```

Allows the runtime to assume that the statement `s0` is safe to execute concurrently for some set of agents, and makes the guarantee that once started each instance of `s0` will run to completion, independent of whatever else is happening in the system.

However, the same is true of statement `s1`. Once started, `s1` must also run to completion. As a result, while the runtime makes no guarantee about instances of `s0` the collection of such instances must complete - and therefore every instance must run eventually.

In practice, on shared resources, other work in the system might occupy every available thread.  To guarantee that all `s0`s are able to run eventually, we might need to create three threads to achieve this - and in the extreme this might not be possible.

We therefore fall back to a design where the calling thread may run the remaining work. We know the caller is making progress. If no thread ever becomes available to run the work the caller might iterate through all instances of `s0` serially.

## Forward progress delegation in an asynchronous world
Unfortunately, if `transform` is made asynchronous, then we have a challenge. Take a result-by-return formulation of async transform as an example.

In either a coroutine formulation:
```cpp
Awaitable<vector<int>> doWork(vector<int> vec) {
  co_return co_await transform(std::execution::par, vec, [](int i) {return i+1;});
}
```

Or a declarative formulation:
```cpp
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) | transform(std::execution::par, [](int i) {return i+1;});
}
```

There is no guaranteed context to delegate work too. We cannot block the caller in either case - the body itself is asynchronous code. We need a more general approach.

# Examples of use
## Delegation in the folly futures library
In Facebook’s folly library we have a limited, but extreme, form of forward progress delegation in the `DeferredExecutor`.
As folly has evolved from an eager Futures library and added laziness, and because folly’s executors are passed by reference, this is special-cased in folly and is a hidden implementation detail of the `SemiFuture` class, however the principle would apply the same if it were made public, with appropriate lifetime improvements.
This functions as a mechanism to allow APIs to force the caller to specify work can run, but to also allow them to push some work on to the caller.
For example, an API that retrieves network data and then deserializes it need not provide any execution context of its own, it can defer all deserialization work into a context of the caller's choice.

`DeferredExecutor` is a single-slot holder of a callback. It will never run tasks itself, and represents no execution context of its own. The `DeferredExecutor` instance will acquire an execution context when it is passed a downstream executor.

So, for example take this simple chain of work:
```cpp
SemiFuture<int> f1 = doSomethingInterestingEagerly();
SemiFuture<int> f2 = std::move(f1)
  .deferValue([](int i){return i+1;}); // s3
Future<int> f3 = f2.via(&aRealExecutor);
```

Even if `f1` is complete at the point that `s3` is enqueued onto the SemiFuture, `f2` will never become ready. The `deferValue` enqueues work and ensures that there is a `DeferredExecutor` on the future (whatever the executor was that completed the work resulting in `f1`).

When `f1` completes it will pass callback `s3` to the executor’s `execute` method, and that callback will be held there, with no context to run it on.

With the creation of `f3`, an executor is attached and is passed to the `DeferredExecutor`’s `set_executor` method. At that point, if `f1` was complete, `s3` would be passed through to `aRealExecutor`. All forward progress here has been delegated from the `DeferredExecutor` to `aRealExecutor`.

Note that a blocking operation:
```cpp
SemiFuture<int> f1 = doSomethingInterestingEagerly();
SemiFuture<int> f2 = std::move(f1)
  .deferValue([](int i){return i+1;}); // s3
int result = std::move(f2).get();
```
Implicitly carries an executor driven by the caller, which is propagated similarly, and a coroutine:
```cpp
int result = co_await std::move(f2);
```
Similarly propagates an executor that the coroutine carries with it. That is, unlike in the synchronous example, the coroutine is not blocking the caller, it is simply transferring work from an executor that is unable to perform it to the one the caller is aware of, that is.

## Submitting work to an accelerator
We may have a chain of tasks that we want to run on an accelerator, but that are dependent on some input trigger.
For example, in OpenCL we might submit a sequence of tasks to a queue, such that the first one is dependent on an event triggered by the host.
```cpp
host_event | gpu_task | gpu_task | gpu_task
```

We do not know when the host event completes. It may be ready immediately.
When do we submit the gpu work?
There are heuristics for doing this:
 * The queue might be being consumed constantly by a hardware thread.
 * There may be a background driver thread pulling from the queue.
 * Some submit operations may cause work to happen.

The last one is unstable if we do not know for sure that more work will arrive.
Some hardware queuing models, such as HSA, require an explicit action to mark a set of tasks as valid.
If we are building that queue from a weakly parallel agent, or if we want to delay passing the queue to the device until `host_event` is ready but we do not know whether the task completing `host_event` has a strong enough forward progress guarantee to safely call into a driver to trigger that queue copy we can instead structure this so that we require an executor to be specified.
In that case the chain of tasks may require an executor to be attached at the end of the chain: a task may be enqueued onto that executor, that flushes the queue.
This may remove the need for background threads.

## Supporting overflow from a fixed size thread pool
This is the traditional case for the parallel algorithms as specified in C++20.
If for whatever reason a thread pool backing an executor is unable to take more work, for example it has stopped processing and not freed its input queue, then it may delegate work to the downstream executor.
Clearly, there are challenges with implementing this and making a true guarantee in all cases.
What I believe is clear is that this has to be implemented in the executor - a queue of work passed to the executor is no longer owned by the algorithm.
The algorithm is in no position to change where this work is scheduled once the executor owns it.
Therefore the most appropriate solution is to make available a valid downstream executor to the upstream executor and allow one to delegate to the other.


# Generalising
Let us say that a given asynchronous algorithm promises some forward progress guarantee, that depends on its executor (and by requirement also on the execution property passed).

So in a work chain (using `|` to represent a chaining of work as in C++20's Ranges):
```cpp
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) | on(DeferredExecutor{}) | transform(std::execution::par, [](int i) {return i+1;});
}
```

`DeferredExecutor` is an executor type known to make no forward progress guarantee for its algorithms.
Indeed, quite the opposite, it is guaranteed to make no progress (note that this is laziness of runtime, but is independent of laziness of submission of a work chain).

As a result, the returned `Sender` also makes no progress.
We may want to be able to query this about the resulting objects.
What it certainly means, though, is that if we add a node to this chain, it must be able to provide an executor to this `Sender`, which can be enforced at compile time.
The submit method on the returned `Sender` requires that the callback passed to it offers an executor.
At worst, this executor might also be deferred, but it is a requirement that at the point this work is made concrete, the `DeferredExecutor` has an executor available to delegate to.

Now let’s say we explicitly provide an executor with a strong guarantee.
A concurrent executor like `NewThreadExecutor`:
```cpp
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) |
    on(DeferredExecutor{}) |
    transform(std::execution::par, [](int i) {return i+1;}) | // c2
    on(NewThreadExecutor{});
}
```

Now the callback `c2` is guaranteed to run on the `NewThreadExecutor`, it is guaranteed to make concurrent progress, which is enough to ensure that the `transform` starts and completes in reasonable time.

Note that this also provides an opportunity for an intermediate state. If instead of a `DeferredExecutor` we had a `BoundedQueueExecutor` with the obvious meaning and a queue size of 1:
```cpp
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) |
    on(BoundedQueueExecutor{1}) |
    transform(std::execution::par, [](int i) {return i+1;}) | // c2
    on(NewThreadExecutor{});
}
```

But where transform will launch a task per element of `vec` and `vec` is larger than 1, the second task may be rejected because of the queue bound. One option is to propagate a failure error downstream, such that the `transform` would fail with an error. However, if `BoundedQueueExecutor` can make use of the executor provided by the downstream `Callback`, it can instead delegate the work, ensuring that even though it hit a limit internally, the second executor will complete the work.

Any given executor should then declare in its specification if it will delegate or not.
If no delegate executor is provided, for example we submit all of this work with a null `Callback` making it fire-and-forget, then no delegation happens but we could instead still make the error available:
```cpp
void doWork(Sender<vector<int>> vec) {
  std::move(vec) |
    on(BoundedQueueExecutor{1}) |
    transform(std::execution::par, [](int i) {return i+1;}) |
    on_error([](Ex&& ex) {/* Handle the error */}) |
    eagerify();
}
```

Finally, note that this is objectively more flexible than throwing an exception on enqueue - the `execute` operation on both of the above uses of `BoundedQueueExecutor` can always succeed, removing the risk of trying to handle an enqueue failure from the caller.
One of them will propagate an error inline, the other gives the `Executor` the decision of whether to propagate work or not.
That allows it to make the decision to delegate after a *successful* enqueue - for example when the queue has accepted work but the executor realizes some property cannot be satisfied, like it has to join its last remaining thread under some system constraint.

## Synchronous operations
Providing the same guarantee as the synchronous transform:
```cpp
vector<int> vec{1, 2, 3};
vector<int> out(vec.size(), 0);
std::transform(
  std::execution::par,
  begin(vec),
  end(vec),
  begin(out),
  [](int i){return i+1;} // s0
  ); // s1
```

May be achieved by something like:
```cpp
vector<int> doWork(Sender<vector<int>> vec) {
  vector<int> output;
  ManualExecutor m;
  std::move(vec) |
    transform(std::execution::par, [](int i) {return i+1;}) |
    on_value([&output](vector<int> v){output = std::move(v);}) |
    on(m);
  m.drain();
  return output;
}
```

That is that the forward progress of the transform, if it is unable to satisfy the requirement, may delegate to the `ManualExecutor`. This is an executor that may be driven by the calling thread. So we have attracted the delegation of work by providing an executor to delegate to, but donating the current thread’s forward progress to that executor until all the work is complete.

This would of course be wrapped in a wait algorithm in practice:
```cpp
vector<int> doWork(Sender<vector<int>> vec) {
  return blocking_wait(std::move(vec) |
    transform(std::execution::par, [](int i) {return i+1;}));
}
```

# Impact on the Standard
 * The `Callback` concept described in P1660 and P0443 may be paired with an optional `get_executor` method, returning an Executor, of concept `CallbackWithExecutor`.
 * A given `Sender`, including an `Executor` may place an additional requirement on the `Callback` passed to `submit` such that it supports `get_executor`.
 * A coroutine awaiting a Sender that places such a requirement, will provide a Callback that carries an Executor.
 * The `sync_wait` algorithm that waits for the result of an executor will, in its generic implementation, pass a `CallbackWithExecutor` to the passed Sender.
 * All standard asynchronous algorithms should, if passed a `CallbackWithExecutor` call `get_executor` on it and provide a `CallbackWithExecutor` when submitting work either to an executor or to the input Sender.

Taking the P1660 definition of a `Callback`:
```cpp
// A CallbackSignal is something with .done() and
// .error() member functions.
template<class T, class E = exception_ptr>
concept CallbackSignal =
  requires (T&& t, E&& e) {
    { ((T&&) t).done() } noexcept;
    { ((T&&) t).error(((E&&) e)) } noexcept;
  };
// A Callback is an Invocable that is also a
// CallbackSignal.
template<class T, class... An>
concept Callback =
  Invocable<T, An...> &&
  CallbackSignal<T>;
// axiom: if invoking T exits by throwing an exception, it is well-defined to
// call .error(E)
```

We add two concepts:
```cpp
template<class C>
concept WithExecutor =
  requires (C&& c) {
    { ((C&&) c).get_executor() } noexcept;
  };

template<class C>
concept CallbackWithExecutor =
  Callback<C> &&
  WithExecutor<C>;
```

We modify the definition of customization point `sync_wait` to say (roughly):

The name `std::sync_wait` denotes a customization point object that calls `std::execution::sync_wait` if no customization exists.

```cpp
namespace std::execution {
T sync_wait(Sender<T>&& s);
}
```
Constructs an implementation-defined callback `c` that satisfies the `CallbackWithExecutor` constraint and calls `std::submit(std::move(s), c)`.
Waits for a call to any of `done`, `value` or `error` on `c`.

The thread that invokes `sync_wait` will block with forward progress delegation on completion of the work where supported by the implementation of `s`, or will eventually execute any work delegated from the implementation of `s` onto the `sync_wait`-provided executor returned by any call to `c.get_executor`.`

Given a standard lazy asynchronous algorithm that takes a `Sender` and returns a `Sender`, for example `transform`:
```cpp
namespace std::execution {
Sender<T2> transform(Sender<T> s, F);
}
```

If `s` requires `CallbackWithExecutor` on its `submit` operation, then the `Sender` returned by `transform` must also.
If `submit` is called on the returned `Sender` and a `CallbackWithExecutor` is passed, then a `CallbackWithExecutor` should be passed to `submit` on `s`, constructed as necessary wrapping the `Executor` returned by `get_executor`.


# Future Work: adding algorithm-level forward progress definitions
We have definitions in the standard for forward progress guarantees for threads of execution.
These apply to the workers in the parallel algorithms fairly clearly.

It is not clear:
 * Whether it is safe to call executor operations under weak forward progress.
    * This will be important for launching nested work.
    * It may be important simply for having one worker trigger the next.
 * Whether it is safe to build a chain of work in a weak agent.
 * What the forward progress guarantee may be of the completion of a parallel algorithm.

All of these are relatively well-defined for synchronous algorithms as they stand.
These are blocking algorithms.
Therefore:
```cpp
vector<int> vec{1, 2, 3};
vector<int> out(vec.size(), 0);
transform(
  std::execution::par_unseq,
  begin(vec),
  end(vec),
  begin(out),
  [](int i){
    vector<int> innervec{1, 2, 3};
    vector<int> innerout(vec.size(), 0);
    transform(
      std::execution::par_unseq,
      begin(innervec),
      end(innervec),
      begin(innerout),
      [](int i){
        return i+1;
      });
    return innerout[0];
  });
```

is not valid.
However, it is not clear that
```cpp
vector<int> vec{1, 2, 3};
vector<int> out(vec.size(), 0);
transform(
  std::execution::par_unseq,
  begin(vec),
  end(vec),
  begin(out),
  [](int i){
    auto s = makeReadySender(0);
    auto resultSender = std::execution::transform(
      std::move(s)
      [](int i){
        return i+1;
      });
    std::execution::submit(
      std::move(resultSender),
      NullCallback{});
    return 0;
  });
```

is not valid,.
This code is entirely inline - blocking, but trivially so because no executor is involved.
Can we make a claim that building and running this operation satisfies `par_unseq`'s guarantee?
Could we if it had an executor involved - would submit on that executor satisfy the guarantee?

I believe that we need to start to think through these problems.
At a minimum, `submit` on an executor may have to make a statement about the guarantee it makes.
`Sender`s may then propagate that guarantee, and that of the upstream sender, down a chain.

This is food for thought.
I don't have a concrete design in mind, but I would like us as a group to be thinking about how asynchronous algorithms and the forward progress definitions interact in general.



---
references:
  - id: P0443R11
    citation-label: P0443R11
    title: "A Unified Executors Proposal for C++"
    issued:
      year: 2019
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0443r11.html
  - id: P1660R0
    citation-label: P1660R0
    title: "A Compromise Executor Design Sketch"
    issued:
      year: 2019
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1660r0.pdf
  - id: P1897R0
    citation-label: P1897
    title: "Towards C++23 executors: A proposal for an initial set of algorithms"
    issued:
      year: 2019
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1897r0.pdf
---
