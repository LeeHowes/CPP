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

 * Lazy by assumption: to allow implementations to remove shared state and synchronization.
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
sync_wait_for(bulk_transform(bulk_schedule(ex, 10), [](){...}), 10s);
```
while maintaining a clearly written clue to what we are doing and where the caller will block.

The gap in the [@P2181] definition of `bulk_schedule` is in sequencing of operations.
This is what we aim to close.

## Proposal TL/DR
Make `bulk_schedule` symmetric with `schedule` by:

 * Renaming `set_value` to `set_next`, to remove a fundamentally different ordering semantic.
 * Add `set_value` back with the same meaning as in the sender returned from `schedule`.
 * Define `many_sender` and `many_receiver` to match these definitions.
 * Propagating the execution policy via a `get_execution_policy` query on the `bulk_receiver`, consistent with `get_scheduler` as defined in [@P1898].


# Sequencing
A sequence of executes is clearly a valuable thing. Let's assume here that `func2` depends on `func1`. We want to support chaining of operations in some fashion:
```
bulk_execution of func on exec
bulk_execution of func2 on exec
```

There are different ways we can enforce this ordering.

## Sequencing using blocking
One way is to make these blocking, either directly, or by manually using blocking algorithms to make it explicit at the call site:
```
sync_wait(bulk_execute(exec, func));
sync_wait(bulk_execute(exec, func2));
```

Blocking has a huge disadvantage because it is viral.
If we have some algorithm that calls these two:
```
void bulk_algorithm(...) {
  sync_wait(bulk_execute(executor, func));
  sync_wait(bulk_execute(executor, func2));
}
```

then `bulk_algorithm` has to be blocking.
For some algorithms this may be a valid design, but for asynchronous algorithms it is not, or at least not without launching a thread for each algorithm to do the blocking, so in general this is a poor solution.
Blocking the caller would, after all, make the algorithm synchronous.
We need true asynchronous algorithms, and so this cannot be the only way we provide work chaining.

## Sequencing using an implicit queue
Another approach is that we implicitly sequence, such that `executor` maintains an in-order queue in order of calls to bulk_execute.
This is a common approach in runtime systems.
The problem with it is that it does not interoperate, so it fails the *Interoperatable* goal.
We need to use a second mechanism to bridge queues from different implementations.

That is to say that in this code, the question remains open:
```
void bulk_algorithm(...) {
  bulk_execute(facebook_executor, func);
  bulk_execute(nvidia, func2);
}
```

## Sequencing by nesting
We can sequence by nesting, where the implementation of `func` triggers the enqueue of `func2`.
For scalar work this makes a lot of sense:
```
executor.execute([executor](){
    func();
    executor.execute(func2);
  });
```

It is a standard implementation strategy for Futures libraries, where the dependence graph is built outside of the executor library.
The actual enqueue does not happen until the previous task completes.
The implementation has no way to optimise this into an in-order queue, but for practical CPU libraries this generally works ok.
It is how folly's Futures are implemented, and it is how coroutines generally work.

For a bulk algorithm this is more complicated.
One way is that we maintain some mechanism to decide if we are in the last task to complete, maybe by incrementing an atomic:
```
executor.bulk_execute([executor](){
    func();
    if(is_last_task()) {
      executor.bulk_execute(func2);
    }
  });
```
We also need to be sure we can enqueue more work from within the bulk task.
That may be limited for weak forward progress situations - it has traditionally not been possible on GPUs, for example and is likely to disallow vectorisation in other cases, which would be unfortunate.

## Sequencing using synchronization primitives

We could instead explicitly order on a barrier.

Either by having the caller block:
```
some_barrier_type barrier(num_elements);
bulk_execute(executor.schedule(), [](){
    func();
    barrier.signal();
  });
barrier.wait();
bulk_execute(executor.schedule(), [](){
    func();
  });
```
But this is equivalent to blocking, with the same problems.

Or by blocking within the task:
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

this has lifetime issues to consider with respect to `barrier`, fairly easily solved by reference counting and heap allocation.
It also risks launching a wide fan out, blocking task, onto an execution context.
This is an easy path to deadlock and so this is equivalent to blocking, in practice.
Note that launching a scalar task in the middle is roughly the same, just moving the blocking elsewhere.

## Using senders
Finally, we can chain bulk algorithms the same way we chain scalar algorithms.
This is the design we discussed in Prague for [@P0443] where bulk_execute takes and returns a `Sender`:
```
auto s1 = bulk_execute(executor.schedule(), func);
auto s2 = bulk_execute(s1, func2);
```

In this design the work has a well-defined generic underlying mechanism for signalling, using a `set_value` call when all instances of `func` complete and when `func2` should run.
As this `set_value` call is well-defined as an interface, we can mix and match algorithms and mix and match authors without problems.

The way this differs from the nesting design is that it is up to `executor` to decide how `set_value` is called.
That means it may be fine to use the atomic-count last-worker-calls model.
It may be that the runtime attaches a callback to the work that is run on some runtime-controlled thread, however.
The `executor` is the right place to make that decision.

As for the scalar case, we can also optimise away the `set_value` call when we know all these types and customise the algorithm, using the sequential queue underneath.
For example, an OpenCL runtime might use events or an in-order queue to chain work on the same executor, and then use a host queue or an event callback to transition onto some other executor completely safely.
Making this up to the implementation, rather than up to the user to inject code into the passed function, offers scope for more efficient implementations.

### Summary
The point here is that **sequence points matter** to bulk algorithms.
By encoding sequence points in the abstraction we put them under the control of the execution context.
By default, because one of our goals is that this code be *Interoperable* of course this uses `set_value`, `set_done` or `set_error` because that's the interface we defined.
In practice, though, we can customise on the intermediate sender types and avoid that cost.
So a default of well-defined sequencing with optimisation is more practical as a model than no sequencing, custom sequencing for each executor type or blocking algorithms.

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
