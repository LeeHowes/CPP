---
title: "Bulk schedule"
document: D2203R0
date: 2020-05-16
audience: SG1
author:
  - name: Lee Howes
    email: <lwh@fb.com>
  - name: Lewis Baker
    email: <lbaker@fb.com>
  - name: Kirk Shoop
    email: <kirkshoop@fb.com>
  - name: Eric Niebler
    email: <eniebler@fb.com>
toc: false
---


# Introduction
The design of bulk execution for executors has continued to evolve.
In [@P2181] Jared Hoberock and Michael Garland have proposed the `bulk_schedule` operation.
This approximates an idea that had been circulating around SG1 for a while, in discussions with Daisy Hollman and others.
In the form in the paper it falls a little short on a couple of fronts, but is moving in the right direction we hope to satisfy all parties.
This paper serves as an addendum to that design that we hope leads to wider agreement.

## Properties of Sender/Receiver
The sender/receiver work, in its most general discussed form in the group, aims for a few goals:

 * Laziness by assumption: to allow implementations to remove shared state and synchronization.
 * Inline allocated where possible: `connect`/`start` APIs so that all asynchronous work can be inlined into the caller's state if that makes sense, rather than assuming heap allocations.
 * Sequenced: `set_value` calls represent sequencing between asynchronous operations.
 * Optimisable: `set_value` calls are not necessary if two chained operations can customise on each others; types.
 * Composable: rather than building out variants of an algorithm (`post`/`defer`, allocator parameter, different function signatures, per-algorithm shared state, timeouts) composition allows us to build more sophisticated algorthms from simple ones. With inline allocation and laziness we can do this at no cost.
 * Interoperatable: by defining first the hooks between a `sender` and a `receiver` we define interfaces between different implementors' libraries in the same way that a `range` defines a simple shared concept, irrespective of how an algorithm is implemented. This includes generic support for error handling, value propagation and cancellation.
 * Cancellable: supporting cancellation in both bulk and non-bulk algorithms is important to a lot of use cases. In [@P0443] the downstream part of cancellation is explicit in the inclusion of `set_done`. The upstream is a query on the receiver.

`bulk_schedule`, compared with `bulk_execute` gives us most of these features.
This is its power.
As a trivial example, if we want a blocking operation, we compose bulk_schedule with `sync_wait`:
```
sync_wait(bulk_transform(bulk_schedule(ex, 10), [](){...}));
```
this requires the `bulk_transform` algorithm, but that is a trivial pass through much like [@P1897]'s `transform`.

If we want to wait with a timeout here, we can add that algorithm:
```
timed_wait(bulk_transform(bulk_schedule(ex, 10), [](){...}), 10s);
```




The gap in the [@P2181] definition of `bulk_schedule` is in sequencing


# Senders and schedule
If we look at `schedule`, that returns a `Sender` as in [@P0443] we see all of the above.

Going briefly through the list:

 * Laziness: until we call `start()` we can assume that no `set_*` function is called on the receiver passed to `connect()`.
   This greatly simplifies the protection we have to apply in the implementation.
   Note that this does not mean that the sender we call `connect` on has not started - eager execution is fine, although it makes little sense for the `sender` returned straight from `schedule`.
 * Inline allocated where possible: the `operation_state` returned by `connect` is not moveable, and contains all the state necessary for the algorithms.
   This means that we can, for example, implement a linked-list of tasks intrusively on the stack with no heap allocation even for complex compound algorithms.
 * Sequenced: a `Sender` notifies its completion via a call to a well-defined set of operations on the `Receiver`, allowing work to be chained.
   With knowledge of the relationship between types of receiver the intermediate calls may be removed if two chained `sender`s behave the same with or without the call.
 * Interoperable: for an unknown pairing of two `sender`s, the inteface on the `Receiver` must be obeyed.
   This is the mechanism through which two arbitrary execution contexts from different vendors can cleanly interoperate.
 * Cancellable: `set_done` is a notification to the `Receiver` that work was cancelled and that the state of prior work cannot be assumed.
   This is defined in [@P0443].
   In addition, a `get_stop_token` CPO is assumed to be available on a `Receiver` to propagate cancellation information from receiver to sender.
   This is the same mechanism that is used to propagate `schedulers` in [@1898] and maps well to coroutines where a `co_await`ing coroutine may pass allocators, executors and other state from caller to callee.

A fire-and-forget operation like `executor.execute(func)` offers a more limited set of options.
Mutating the executor is one way to pass in allocators, and potentially a stop-token.
Making the operation blocking, or embedding chaining of work directly in the passed function, allows for chaining.
Chaining through blocking is a significant limitation for an algorithm using an executor.
Chaining through the function itself loses optimisation opportunities because it requires the user to embed the mechanism, removing the opportunity for the context to optimise back to back operations, for example through using an in-order queue rather than enqueue-by-callback.

However, none of this is to say that a one-way execute algorithm is valueless.
In a known environment it works well, and indeed is the common way for building tasking environments at this time.




# bulk_execute/bulk_schedule
Bulk execution scales directly from simple scalar execution.
We should design bulk operations with the same goals in mind, for interoperability, efficiency and to support efficient compound algorithms.
Some aspects become more complicated than for scalar algorithms: in particular, sequencing is more difficult to support efficiently, but this makes it more important to consider.

## Sequencing

A sequence of executes is clearly a valuable thing. Let's assume here that `func2` depends on `func1`. We want to support chaining of operations in some fashion:
```
bulk_execute(exec, func);
bulk_execute(exec, func2);
```

One way is to make these blocking, either directly, or by manually using blocking algorithms to make it clearer at the call site:
```
sync_wait(bulk_execute(exec, func));
sync_wait(bulk_execute(exec, func2));
```

Blocking has a huge disadvantage because it is viral.
If we have some algorithm that calls these two:
void alg(...) {
  sync_wait(bulk_execute(executor, func));
  sync_wait(bulk_execute(executor, func2));
}

then alg has to be blocking.
For some algorithms this may be a valid design, but for asynchronous algorithms it is not, or at least not without launching a thread for each algorithm to do the blocking, so in general this is a poor solution.
It certainly cannot be the only way we provide work chaining.

Another approach is that we implicitly sequence, such that `executor` maintains an in-order queue in order of call to bulk_execute.
This is a common approach in runtime systems.
The problem with it is that it does not interoperate, so it fails the *Interoperatable* goal.
We need to add a second mechanism to bridge queues from different implementations.

We can sequence by nesting, where the implementation of `func` triggers the enqueue of `func2`.
For scalar work this makes a lot of sense, being practical apart from losing the potential in-order queue optimisation:
```
executor.execute([executor](){
    func();
    executor.execute(func2);
  });
```

For a bulk algorithm this is more complicated:
```
executor.bulk_execute([executor](){
    func();
    if(is_last_task) {
      executor.bulk_execute(func2);
    }
  });
```
where we need some way to compute `is_last_task`, and we need to be sure we can enqueue more work from within the bulk task.
That may be limited for weak forward progress situations.

We could instead explicitly order on a barrier:
```
some_barrier_type barrier(num_elements);
bulk_execute(executor.schedule(), [](){
    func();
    barrier.signal();
  });
bulk_execute(executor.schedule(), [](){
    barrier.wait();
    func();
  });
```

this both has lifetime issues to consider with respect to `barrier`, potentially solved by reference counting.
It also risks launching a wide fan out, blocking task, onto an execution context which is an easy path to deadlock.

Finally, we can chain bulk algorithms the same way we chain scalar algorithms.
This is the design we discussed in Prague for [@P0443] where bulk_execute takes and returns a `Sender`:
```
auto s1 = execute(executor.schedule(), func);
auto s2 = execute(s1, func2);
```

In this design the work has a well-defined generic underlying mechanism for signalling, using a `set_value` call when all instances of `func` complete and when `func2` should run.
As this `set_value` call is well-defined as an interface, we can mix and match algorithms and mix and match authors without problems.

As for the scalar case, we can also optimise away the `set_value` call when we know all these types and customise the algorithm, using the sequential queue underneath, and even when this is not available, we can let the runtime system optimise the context on which `set_value` is called to ensure it is valid.
If necessary a driver-owned thread might call `set_value` even if an accelerator runs the calls to `func`.
By making this up to the implementation, rather than up to the user to inject code into the passed function, offers scope for more efficient implementations.

The point here is that **sequence points matter** to bulk algorithms.
By encoding sequence points in the abstraction we put them under the control of the execution context.
By default, because one of our goals is that this code be *Interoperable* of course this uses `set_value`, `set_done` or `set_error` because that's the interface we defined.
In practice, though, we can customise on the intermediate sender types and avoid that cost.
So a default of well-defined sequencing with optimisation is more practical as a model than no sequencing, custom sequencing for each executor type or blocking algorithms.














OLD:
------------

We can block on this either by making `execute` itself blocking, or by using an algorithm that blocks:
```
sync_wait(executor.execute(func));
```

why might we do it this way?
First, the implementation here would be very similar to the implementation of a generic blocking version of execute, so there should be no latency or allocation change.

However, there are advantages:

 * Orthogonality: We can change the style of waiting independently of the algorithm. `timed_wait` that makes a `stop_token` available to the executor becomes easy to build without duplicating the `execute` algorithm (note that this matters with the multiplicative effect of having multiple algorithms).
 * Thread-safety: This was brought up in one of the review calls. If execute is blocking we have an interesting question if two threads operation on the executor such that one is blocking in `execute` and the other is assigning to the executor. This definition is much cleaner if we decouple the blocking operation and move it to a single-shot `sender` returned from the algorithm instead.

Sequencing this using blocking calls suffers the same way as for execute.
We need not elaborate.
Using an ordered queue in the executor similarly.
There is nothing special about bulk.

Sequencing using nesting is different, however.
We can nest the child call, dispatching it only by the "last instance" using some definition for the precise operation being defined.
We could, for example, have a simple atomic that counts up to the size of the dispatch:
```
auto s1 = bulk_execute(executor.schedule(), [](){
    func();
    if(last_instance){} bulk_execute(s1, func2); }
  });
sync_wait(s2);
```

This works for a fire and forget structure, if there is no problem calling `bulk_execute` from within an agent on another `bulk_execute`.
It becomes very hard to structure in such a way as to maintain a handle to the computation, however.
Unlike the scalar case we cannot really structure this such that we return a value from one instance, which means that `sync_wait` in this case is not waiting for the nested instance.

We could instead explicitly order on a barrier:
```
some_barrier_type barrier(num_elements);
auto s1 = bulk_execute(executor.schedule(), [](){
    func();
    barrier.signal();
  });
auto s1 = bulk_execute(executor.schedule(), [](){
    barrier.wait();
    func();
  });
sync_wait(s2);
```

but now we are forcing the actual workers to block, of which there may be many, which makes our need for concurrent forward progress and allocation of more threads even more important.

Again then, the most practical way of dealing with this is to have explicit sequence points via chaining, which comes back to the sender model:
```
auto s1 = bulk_execute(executor.schedule(), func);
auto s2 = bulk_execute(s1, func2);
sync_wait(s2);
```

this is what we agreed to as a modification to `bulk_execute` in Prague (along with some minor changes we exclude here).

The point here is that sequence points matter to bulk algorithms.
By encoding sequence points in the abstraction we put them under the control of the execution context.
By default, because one of our goals is that this code be *Interoperatable* of course this uses `set_value`, `set_done` or `set_error` because that's the interface we defined.
In practice, though, we can customise on the intermediate sender types and avoid that cost.
So a default of well-defined sequencing with optimisation is more practical as a model than no sequencing, custom sequencing for each executor type or blocking algorithms.


## Allocation
In a true fire-and-forget mode of `execute`, we have to hand work to an allocator, which will generically heap allocate storage for the function.
In the `sender` model of an execute algorithm, we do not.
Given:
```
auto f = executor.execute(func);
sync_wait(f);
```

where `execute` itself is **not** blocking (we realise the example as a whole is for simplicity) by the time we call `sync_wait` `func` and all state needed to manage `func` may still be pending.
This is a lazy operation by default.
In this case we wait one line of code, and then `sync_wait` calls `connect` on f, stores that state locally on its stack, calls `start()` and waits for the result.
Everything is allocated inline in the caller.

Now of course, if we want `execute` to run eagerly, we still have to heap allocate.
This seems unfortunate, but may be the right implementation decision in some cases where eagerness matters.
However, we don't *have* to do that.

What if we have a `launch` algorithm that `connect`s `f`, makes it run immediately hence removing any latency concern, but returns a stack allocated handle to the state?
```
auto f = executor.execute(func);
auto state = launch(f);
// Do other things, we don't need to block
sync_wait(state);
```

now we are:
 * Immediately launching `func`, eagerly, with no added latency.
 * We benefit from the lazy model by making an explicit decision to stack allocate all state.
   We could, for example, implement `executor`'s queue as an intrusive list across stack-allocated objects.
 * We can do work concurrently while the object is live on the stack.

This is a common pattern for nested algorithms, where we don't really stack allocate, but we do collapse operations into some parent algorithm's state such that there is a heap allocation, but only one heap allocation for some relatively complicated set of work.



## Bulk cancellation
Earlier we mentioned the orthonality of factoring out blocking into a separate algorithm:
```
sync_wait(executor.execute(func));
timed_wait(executor.execute(func));
```

`timed_wait` in this case would provide a `stop_token` to the operation through the receiver, such that if it is sitting in a queue too long, the flag can be set and the executor can drop the work.
This is a fundamental property that you gain by holding some handle to the work.
Note that while you can cancel by checking for cancellation inside `func`, doing so while work is in the queue or integrating cancellation into a network operation benefits from integration into the dependencies.

For a bulk operation this becomes even more interesting.
Some parallel algorithms construct work in such a way that they exit early to avoid enqueuing extra work.
By structuring code with a handle in this way, we can control that cancellation cleanly.

So, for example, if we allow an algorithm to set a `stop_token` in the work chain, and make it available to the tasks, we could abort work early.
For example, a `find_if` that stops other tasks when it finds the first:
```
std::optional<type> output;
timed_wait(bulk_execute(with_stop_token(executor.schedule()), [](stop_source& source) {
  ...
  if(found) {
    output = val;
    source.request_stop();
  }
}));
```

and this code can:
 1) time out and propagate the stop token into `bulk_execute` through the receiver
 2) add stop token chaining to the injected token
 3) have the bulk operation aware of the stop token and cancel unlaunched work before it is launched

This came up as an important use case in the Microsoft implementatino of the parallel algorithms.


# bulk_schedule in P2181
This all brings us back to `bulk_schedule`, which becomes the core of how we do the above, in extension to `schedule()` which we already know about in [@P0443].

It should be clear now that for all of the reasons above, we do not believe that the one-way fire-and-forget version of `bulk_execute` is a good API to include.
 * It is difficult to use as part of a more complex algorithm.
 * It loses the ability to do cancellation and generic task ordering.
 * It will depend on blocking work in too many cases, and this is problematic outside of the current blocking algorithms, and is probably not an efficient design inside them.

[@P2181] also talked about `bulk_schedule`.
Having some bulk version of schedule is an excellent foundational component, that fits into the sender/receiver model cleanly and gives us the properties we need.
The version in [@P2181], however, is missing one core feature: it does not provide *Sequenced* operations.
It sequences *after* some prologue sender, which is potentially useful although for consistency with `schedule` probably not necessary at this level of the API.
The problem is that it does not provide a sequence point after it completes.

As described, and shown in the example
```
auto increment =
    bulk_schedule(executor, vec.size(), just(ints)) |
    transform([](size_t idx, std::vector<int>& ints)
    {
        ints[i] += 1;
    });
```

`bulk_schedule` in this form is built on `bulk_execute`, by calling `set_value` for each instance.
That is it suffers from the same chaining problems we talk about above.
We cannot say when the whole operation completes, and thus when to execute a subsequent task.
Note that the Prague-agreed version of `bulk_execute` does not suffer from this: `bulk_execute` in that case is fork/join with sequence points before and after, calling `set_value` just once as a standard sender to chain work.

We infer from this that we need to distinguish the forking calls from the `set_value` call.

# bulk_schedule in summary
That leads us to a version of `bulk_schedule` that is a clear extension of `schedule`.
This requires a matching extension of `sender` and `receiver`.

A `many_receiever` adds a `set_next` operation that takes the sender's values by reference, and an index.
```
struct example_many_receiver {
  template<typename IdxT, typename V...>
  void set_next(IdxT idx, V... values) noexcept;
  template<typename V...>
  void set_value(V... values) noexcept;
  void set_error(exception_ptr ex) noexcept;
  void set_done() noexcept;
};
```

A `many_sender` is a `sender` that takes a `many_receiver` in its connect operation.
It will call `set_next` some number of times with the sender's values and an index.
If it completes *all* calls to `set_next` successfully, it will call `set_value`.
Otherwise it will call `set_error` or `set_done` depending on the required semantics.
Vitally, after it makes all calls to `set_next`, it calls `set_value`, providing a sequence point.

`bulk_schedule` is simply an algorithm that takes a `scheduler` and an iteration space and returns a `many_sender` over that iteration space.
The returned `many_sender` will call `set_next(i)` for each index in the iteration space on the receiver connected to it.
If all complete, and the operation has not been cancelled, it will call `set_value` on the same receiver.

In libunifex we have a [very simple example](https://github.com/facebookexperimental/libunifex/blob/master/test/bulk_schedule_test.cpp) of this:
```
    unifex::sync_wait(
        unifex::bulk_join(
            unifex::bulk_transform(
                unifex::bulk_transform(
                    unifex::bulk_schedule(sched, count),
                    [count](std::size_t index) noexcept {
                        // Reverse indices
                        return count - 1 - index;
                    }, unifex::par_unseq),
                [&](std::size_t index) noexcept {
                    output[index] = index;
                }, unifex::par_unseq)));

    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_EQ(i, output[i]);
    }
```

`bulk_transform` in this case is an algorithm that applies a transformation to the `set_next` operation and passes it through, which is very similar to the [@P2181] transform example.
`bulk_join` here is simply a trivial algorithm with an empty `set_next` operation such that it triggers all `set_next` calls to return and the scheduler will call `set_value` as a sequencing operation.

Cancellation is provided by a `let_with_stop_source` algorithm:
```
    unifex::sync_wait(
        unifex::let_with_stop_source([&](unifex::inplace_stop_source& stopSource) {
            return unifex::bulk_join(
                unifex::bulk_transform(
                    unifex::bulk_schedule(sched, count),
                    [&](std::size_t index) noexcept {
                        // Stop after second chunk
                        if(index == compare_index) {
                            stopSource.request_stop();
                        }
                        output[index] = index;
                    }, unifex::seq));
        }));
```

that provides `stop_source` integration and in this case trivially cancels future invocations of `set_next` after a particular one has been reached.
As we passed the `seq` policy that ordering is guaranteed.
Like `let`, the `stop_source`, and the entire function object passed to the algorithm remain alive until the nested work completes.
`let_with_stop_source` also chains `stop_token`s such that if, instead of `sync_wait` an algorithm is used that might want to cancel upstream work, cancelling that token will also cancel the token that `stopSource` triggers.
Requesting cancellation of `stopSource` cancels as yet unexecuted instances of the `bulk_schedule`, but is propagated into the `bulk_schedule` entirely generically because `bulk_schedule` will request the `stop_token` from the receiver passed to it.

Of course, the full set of algorithms we define for C++23 and beyond is wide open. `bulk_transform` and `bulk_with_stop_source` are simply examples of the kind of algorithms we can hook up with the fundamental `bulk_schedule` primitive, in the same way that we aim to define asynchronous parallel algorithms on top of [@P0443].

# Examples

# Impact on the Standard

# Critique of [@P2181] discussion
## Heap allocations
bulk_execute as defaults requires two heap allocations in the general case where:

 * it is non-blocking (because while not all executors need be, all good QOI used by async algorithms must be).
 * we need some sort of inter-task communication
 * we need to know when the task has been cancelled

in these cases:
 * the bulk-execute call will have to heap allocate the function object
 * the shared state will have to be allocated

If we need to know that the task has been cancelled, so we can safely satisfy a barrier that something else might be waiting on for clean process shutdown, we need the function object to be passed by value.
If it is passed by reference, the caller either heap allocates or can keep it locally, but at the cost that the executor now has no generic way to tell the caller that the task will never happen.
This is a receipe for deadlock on process shutdown.

The sender-based model of this has no such problem:
 * connect/start removes the fundamental heap allocation
 * a clear sequence point on completion (success failure or cancellation) tells us when the task finishes, without the caller having to pessimise with reference counting

We should not be defining new APIs that default to a heap allocating reference-counting-heavy model of task management.

## Implementing bulk_execute on top of bulk_schedule is trivial
This should be pretty simple:
```
struct Executor {
  void bulk_execute(index_t size, Func&& func) {
    submit(bulk_schedule(size), as_bulk_receiver(func));
  }
  ...
};
```

So is `bulk_schedule` itself complicated to implement, assuming some internal bulk_execute-like API?
Marginally, but only to enforce the sequence point.

```
struct Scheduler {
  struct OS {
    struct Wrapper {
      Receiver r_;
      std::atomic<index_t> cnt_;
      ~Wrapper(){
        if(cnt_ == 0) {
          r_.set_next();
        } else {
          r_.set_done();
        }
      }
    };
    void start() {
      internal_bulk_execute([s = make_shared<Wrapper>(r_, size)](index_t idx){s->r.set_next(idx);});
    }
    index_t size;
    Receiver r_;
  };
  struct Sender {
    index_t size;
    OS connect(Receiver&& r) {
      return OS{size, r};
    }
  };
  Sender bulk_schedule(index_t size) {
    return Sender{size};
  }
  ...
};
```

Note how little real "laziness" there is in that sequence of calls?
Everything is trivial argument currying, of trivial values.
There is no added latency involved.

So the complexity is only that the executor is adding just enough state to know how to tell the caller that this bulk task completes - but the executor is the right place to do that:

 * It is the entity that knows what the safest form of memory allocation and reference counting is for that state.
 * It knows the right barrier type to use.
 * It knows if it actually has some sort of event model in the driver so that it can ignore reference counting completely and rely on a driver callback.

For good QOI as users of these APIs, and maintainers of the tooling that thousands of developers will use to (largely indirectly) use these APIs, we much prefer the idea that my executor implementor tweaks the above implementation to work well for the target runtime and architecture than to prefer a model that relies on the shared_ptr and heap allocated barrier pessimization.

## Not passing the policy
The question here is at what point we communicate the policy to the executor.
If we do it before we call an algorithm:
```
parallel_executor e;
algorithm(e, execution::seq, ...);
```

then the algorithm has one restriction placed on its worker functions, and another restriction arising from the concrete executor.
If part of the implementation of the algorithm uses a different policy, in the above case a more relaxed operation that can run `par_unseq` then it has no way to communicate this to the executor, which might be able to optimise its execution in that case.
We lose flexibility by fixing the executor's policy before passing it to the algorithm.

This is even more true in compound algorithms, that we might get from sender chaining:

```
parallel_scheduler e;
auto r1 = algorithm(e.schedule(), execution::par, ...);
auto r2 = algorithm(r1, execution::seq, ...);
```

now it is true that at this time we do not have a final decision on how the parallel algorithm API should be setup - and maybe each will take an executor explicitly.
This seems limiting, however.

The alternative is to allow the algorithm to apply modifications:
```
void algorithm(Executor e,...) {
  auto e2 = e.make_seq();
  // use e2 with seq
}
```

this would have to leak to the interface if we were to allow passing an executor that could not take this modification.
At that point we are almost as well off taking a list of executors for different parts of the algorithm.
More seriously, though, it means that if we want to slightly change the implementation to allow part of it to run in a more relaxed forward progress mode we cannot do it without changing the constraints on the API.

Even if we did allow for this option, and put the constraints appropriately on the API, or required the implementation to make decisions so that this never fails but only allows different implementations, it seems little different from passing it to execute anyway.
So what do we gain from the limitation?

---
references:
  - id: P0443
    citation-label: P0443R13
    title: "A Unified Executors Proposal for C++"
    issued:
      year: 2020
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0443R13.html
  - id: P1660
    citation-label: P1660R0
    title: "A Compromise Executor Design Sketch"
    issued:
      year: 2019
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1660r0.pdf
  - id: P1897
    citation-label: P1897
    title: "Towards C++23 executors: A proposal for an initial set of algorithms"
    issued:
      year: 2020
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1897r3.pdf
  - id: P1898
    citation-label: P1898
    title: "Forward progress delegation for executors"
    issued:
      year: 2020
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1898r1.pdf
  - id: P2181
    citation-label: P2181
    title: "Correcting the Design of Bulk Execution"
    issued:
      year: 2020
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2181r0.pdf
---
