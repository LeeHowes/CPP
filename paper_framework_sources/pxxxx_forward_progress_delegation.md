---
title: "Forward progress delegation for executors"
document: PXXXR0
date: 2019-09-14
audience: SG1, Library Evolution
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---

# Introduction

This is an early proposal to sound out one approach to forward progress delegation for executors, based on folly’s `DeferredExecutor`.

We propose adding a `get_executor` method on a `Callback` that allows and executor to be propagated from downstream to upstream asynchronous tasks. A given `Executor`, and therefore any `Sender` created off that executor may require the availability of such a method on `Callback`s passed to the `submit()` method.

An executor may delegate any tasks it is unable to perform to the executor it acquires from the downstream `Callback`.

A `blocking_wait` operation encapsulates an `Executor` that is iterated directly in the calling thread. This executor will make progress and as such can act as the executor-of-last-resort when the C++20 guarantee is required in blocking code.

We also propose that a set of definitions for forward progress of asynchronous graph nodes be provided.

# Motivation and Scope
## Forward progress delegation in C++20.
The standard parallel algorithms support weak forward progress guarantees.
For example, with the code:
```
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
```
Awaitable<vector<int>> doWork(vector<int> vec) {
  co_return co_await transform(std::execution::par, vec, [](int i) {return i+1;});
}
```

Or a declarative formulation:
```
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) | transform(std::execution::par, [](int i) {return i+1;});
}
```

There is no guaranteed context to delegate work too. We cannot block the caller in either case - the body itself is asynchronous code. We need a more general approach.

## Delegation in the folly futures library
In Facebook’s folly library we have a limited, but extreme, form of forward progress delegation in the `DeferredExecutor`. As folly has evolved from an eager Futures library and added laziness, and because folly’s executors are passed by reference, this is special-cased in folly and is a hidden implementation detail of the `SemiFuture` class, however the principle would apply the same if it were made public, with appropriate lifetime improvements.

`DeferredExecutor` is a single-slot holder of a callback. It will never run tasks itself, and represents no execution context of its own. The `DeferredExecutor` instance will acquire an execution context when it is passed a downstream executor.

So, for example take this simple chain of work:
```
SemiFuture<int> f1 = doSomethingInterestingEagerly();
SemiFuture<int> f2 = std::move(f1)
  .deferValue([](int i){return i+1;}); // s3
Future<int> f3 = f2.via(&aRealExecutor);
```

Even if `f1` is complete at the point that `s3` is enqueued onto the SemiFuture, `f2` will never become ready. The `deferValue` enqueues work and ensures that there is a `DeferredExecutor` on the future (whatever the executor was that completed the work resulting in `f1`).

When `f1` completes it will pass callback `s3` to the executor’s `execute` method, and that callback will be held there, with no context to run it on.

With the creation of `f3`, an executor is attached and is passed to the `DeferredExecutor`’s `set_executor` method. At that point, if `f1` was complete, `s3` would be passed through to `aRealExecutor`. All forward progress here has been delegated from the `DeferredExecutor` to `aRealExecutor`.

Note that a blocking operation:
```
SemiFuture<int> f1 = doSomethingInterestingEagerly();
SemiFuture<int> f2 = std::move(f1)
  .deferValue([](int i){return i+1;}); // s3
int result = std::move(f2).get();
```
Implicitly carries an executor driven by the caller, which is propagated similarly, and a coroutine:
```
int result = co_await std::move(f2);
```
Similarly propagates an executor that the coroutine carries with it. That is, unlike in the synchronous example, the coroutine is not blocking the caller, it is simply transferring work from an executor that is unable to perform it to the one the caller is aware of, that is.

## Generalising
Let us say that a given asynchronous algorithm promises some forward progress guarantee, that depends on its executor (and by requirement also on the execution property passed).

So in a work chain:
```
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) | on(DeferredExecutor{}) | transform(std::execution::par, [](int i) {return i+1;});
}
```

`DeferredExecutor` is an executor type known to make no forward progress guarantee for its algorithms. Indeed, quite the opposite, it is guaranteed to make no progress (note that this is laziness of runtime, but is independent of laziness of submission of a work chain).

As a result, the returned `Sender` also makes no progress. We may want to be able to query this about the resulting objects. What it certainly means, though, is that if we add a node to this chain, it must be able to provide an executor to this `Sender`, which can be enforced at compile time. The submit method on the returned `Sender` requires that the callback passed to it offers an executor. At worst, this executor might also be deferred, but it is a requirement that at the point this work is made concrete, the `DeferredExecutor` has an executor available to delegate to.

Now let’s say we explicitly provide an executor with a strong guarantee. A concurrent executor like `NewThreadExecutor`:
```
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) |
    on(DeferredExecutor{}) |
    transform(std::execution::par, [](int i) {return i+1;}) | // c2
    on(NewThreadExecutor{});
}
```

Now the callback `c2` is guaranteed to run on the `NewThreadExecutor`, it is guaranteed to make concurrent progress, which is enough to ensure that the `transform` starts and completes in reasonable time.

Note that this also provides an opportunity for an intermediate state. If instead of a `DeferredExecutor` we had a `BoundedQueueExecutor` with the obvious meaning and a queue size of 1:
```
Sender<vector<int>> doWork(Sender<vector<int>> vec) {
  return std::move(vec) |
    on(BoundedQueueExecutor{1}) |
    transform(std::execution::par, [](int i) {return i+1;}) | // c2
    on(NewThreadExecutor{});
}
```

But where transform will launch a task per element of `vec` and `vec` is larger than 1, the second task may be rejected because of the queue bound. One option is to propagate a failure error downstream, such that the `transform` would fail with an error. However, if `BoundedQueueExecutor` can make use of the executor provided by the downstream `Callback`, it can instead delegate the work, ensuring that even though it hit a limit internally, the second executor will complete the work.

Any given executor should then declare in its specification if it will delegate or not. If no delegate executor is provided, for example we submit all of this work with a null `Callback` making it fire-and-forget, then no delegation happens but we could instead still make the error available:
```
void doWork(Sender<vector<int>> vec) {
  std::move(vec) |
    on(BoundedQueueExecutor{1}) |
    transform(std::execution::par, [](int i) {return i+1;}) |
    on_error([](Ex&& ex) {/* Handle the error */}) |
    eagerify();
}
```

Finally, note that this is objectively more useful than throwing an exception on enqueue - the `execute` operation on both of the above uses of `BoundedQueueExecutor` can always succeed, removing the risk of trying to handle an enqueue failure from the caller. One of them will propagate an error inline, the other gives the `Executor` the decision of whether to propagate work or not. That allows it to make the decision to delegate after a *successful* enqueue - for example when the queue has accepted work but the executor realizes some property cannot be satisfied, like it has to join its last remaining thread under some system constraint.

## Synchronous operations
Providing the same guarantee as the synchronous transform:
```
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

May be achieved by something like:
```
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
```
vector<int> doWork(Sender<vector<int>> vec) {
  return blocking_wait(std::move(vec) |
    transform(std::execution::par, [](int i) {return i+1;}));
}
```

# Impact on the Standard
 * The `Callback` concept described in P0443 may be paired with an optional `get_executor` method, returning an Executor.
 * A given `Sender`, including an `Executor` may place an additional requirement on the `Callback` passed to `submit` such that it supports `get_executor`.
 * A coroutine awaiting a Sender that places such a requirement, will provide a Callback that carries an Executor.

We should also look at whether we need to define a forward progress guarantee at the algorithm level. That guarantee might be upgraded by delegation. For example, in the above example `transform` is allowed to run on a fixed size thread pool. It does not guarantee to start more workers to keep work going, if it did then the code would have been able to assume a stronger guarantee than `par`. What exactly is the meaning of this guarantee? When we delegate, we ensure that `transform` will complete - indeed we now actually guarantee that all of the workers will start, though we still do not guarantee that individual iterations in the transform will run concurrently. What does this mean? This will become more important once we have complex graphs - a merge point in a graph may require that the incoming branches will complete and that they be able to interact in some fashion.

At that point, we may consider having `Senders` expose the guarantee they make over their execution. This is future work.


# Proposed Wording

---
references:
  - id: P0443
    citation-label: P0443
    title: ""
    issued:
      year: 2019
    URL:
  - id: P1660
    citation-label: P1660
    title: ""
    issued:
      year: 2019
    URL:

---
