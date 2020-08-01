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
 * Propagating the forward progress requirements of `set_value` via a `get_execution_policy` query on the `many_receiver`, consistent with `get_scheduler` as defined in [@P1898].
 * Propagating the forward progress requirements of `set_next` via a `get_bulk_execution_policy` query on the `many_receiver`, consistent with `get_scheduler` as defined in [@P1898].
 * Propagating a stop_token via a `get_stop_token` query on the `many_receiver`, as defined in [@P2175] and allowing the `many_sender` to abort early on cancellation.


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


# The changes
To maintain sequencing, we need `set_value` to have the same definition as in `sender` for the type returned by `bulk_schedule`.
It should not be possible to accidentally chain scalar work onto bulk work.

The list of clarifications necessary:

 * `set_value` will, like `set_error` or `set_done` be called at most once on the receiver.
 * We add a `set_next` operation, that differentiates a `many_sender` or `many_receiver` from their original forms, that is called once for each element of the iteration space, and passes the index.
 * After all necessary calls to `set_next` return, `set_value` will be called.
 * `set_error` or `set_done` indicate that some `set_next` calls may not have completed successfully.
 * A query, `get_bulk_execution_policy` called on a `many_receiver` will return the policy that that receiver requires to be able to safely call `set_next` on the receiver.
 * A query, `get_execution_policy` called on a `many_receiver` will return the policy that that receiver requires to be able to safely call `set_value` on the receiver.
   This may or may not be utilised by the scheduler, but it should be propagated through a chain of receivers and upgraded to a tighter policy if necessary.
 * A query, `get_stop_token` called on a `many_receiver` will return a `stop_token` associated with that receiver such that the receiver or some downstream receiver may request cancellation of the operation.
 * A `many_receiver` is-a `receiver` and when passed to a single `sender` behaves as if an iteration space of 0 was executed.
 * A `sender` is-a `many_sender` by the same interpretation. It is a `many_sender` that does not call `set_next`.\
 * `bulk_schedule(s, size)` returns a `many_sender` that once connected to a `many_receiver` `r` will call `set_next(r, idx)` once for each in the range `0 <= idx < size` and subsequently call `set_value(r)`.
 * A `many_sender` **may** obtain a `stop_token` via a call to `get_stop_token` on a `many_receiver` `r`.
   If stop is requested the `many_sender` **may** elide subsequent calls to `set_next` and call `set_done(r)`.


# Impact on the Standard
## execution::set_next
The name execution::set_next denotes a customization point object. The expression `execution::set_next(R, Idx, Vs...)` for some subexpressions `R`, `Idx` and `Vs...` is expression-equivalent to:

`R.set_next(Idx, Vs...)`, if that expression is valid. If the function selected does not send the value(s) `Idx` and `Vs...` to the `many_receiver` `R`’s next operation, the program is ill-formed with no diagnostic required.

Otherwise, `set_next(R, Idx, Vs...)`, if that expression is valid, with overload resolution performed in a context that includes the declaration
```
  void set_next();
```
and that does not include a declaration of `execution::set_next`. If the function selected by overload resolution does not send the value(s) `Idx` and `Vs...` to the receiver `R`’s next channel, the program is ill-formed with no diagnostic required.

Otherwise, `execution::set_next(R, Idx, Vs...)` is ill-formed.

[Editorial note: We should probably define what “send the value(s) Vs... to the many_receiver R’s next channel” means more carefully. –end editorial note]

## execution::bulk_schedule
The name `execution::bulk_schedule` denotes a customization point object. For some subexpressions `s` and `size`, let `S` be a type such that `decltype((s))` is `S` and `Size` be a type such that `decltype((size))` is `Size`. The expression `execution::bulk_schedule(s, size)` is expression-equivalent to:

`s.bulk_schedule(size)`, if that expression is valid and its type models sender.

Otherwise, schedule(s), if that expression is valid and its type models sender with overload resolution performed in a context that includes the declaration
```
  void bulk_schedule();
```
and that does not include a declaration of `execution::bulk_schedule`.

Otherwise, `execution::bulk_schedule(s)` is ill-formed.

[NOTE: The defition of the default implementation of bulk_schedule is open to discussion]

## Concept many_receiver_of
A `many_receiver` represents the continuation of an asynchronous operation formed from a potentially unordered sequence of indexed suboperations.
An asynchronous operation may complete with a (possibly empty) set of values, an error, or it may be cancelled.
A `many_receiver` has one operation corresponding to one of the set of indexed suboperations: `set_next`. Like a `receiver`, a `many_receiver` has three principal operations corresponding to the three ways an asynchronous operation may complete: `set_value`, `set_error`, and `set_done`. These are collectively known as a `many_receiver`’s completion-signal operations.

```
    template<class T, class Idx, class... An, class... Nn...>
    concept many_receiver_of =
      receiver_of<T, An...> &&
      requires(remove_cvref_t<T>&& t, Idx idx, An&&... an, Nn&&... nn) {
        execution::set_value(std::move(t), (An&&) an...);
        execution::set_next(t&, idx, (Nn&&) nn...);
      };
```
The `many_receiver`’s completion-signal operations have semantic requirements that are collectively known as the `many_receiver` contract, described below:

None of a `many_receiver`’s completion-signal operations shall be invoked before `execution::start` has been called on the operation state object that was returned by `execution::connect` to connect that `many_receiver` to a `many_sender`.

Once `execution::start` has been called on the operation state object, `set_next` shall be called for each index in some iteration space and parameterised with that index, under the restriction of the policy returned by a call to `get_execution_policy` on the `many_receiver`.

All of the `many_receiver`'s `set_next` calls shall happen-before any of the `many_receiver`'s completion-signal operations.
Exactly one of the receiver’s completion-signal operations shall complete non-exceptionally before the receiver is destroyed.

If any call to `execution::set_next` or `execution::set_value` exits with an exception, it is still valid to call `execution::set_error` or `execution::set_done` on the `many_receiver`.
If all calls to `execution::set_next` complete successfully, it is valid to call `execution::set_value` on the `many_receiver`.

Once one of a `many_receiver`’s completion-signal operations has completed non-exceptionally, the `many_receiver` contract has been satisfied.


## Concepts many_sender and many_sender_to
XXX TODO The many_sender and many_sender_to concepts…
```
    template<class S>
      concept many_sender =
        move_constructible<remove_cvref_t<S>> &&
        !requires {
          typename sender_traits<remove_cvref_t<S>>::__unspecialized; // exposition only
        };

    template<class S, class R>
      concept many_sender_to =
        many_sender<S> &&
        many_receiver<R> &&
        requires (S&& s, R&& r) {
          execution::connect((S&&) s, (R&&) r);
        };
```

None of these operations shall introduce data races as a result of concurrent invocations of those functions from different threads.

A `many_sender` type’s destructor shall not block pending completion of the submitted function objects. [Note: The ability to wait for completion of submitted function objects may be provided by the associated execution context. –end note]


# Why use get_(bulk_)execution_policy on the receiver?
There are two questions embedded in this:

 * Why pass the policy into the bulk operation at all (with reference to the discussion point in [@P2181])?
 * Why do it this way rather than as a parameter?


## Why pass a policy into the bulk API?
For a compound algorithm, it is unreasonable to assume that the policy passed to the algorithm need be the one applied to the executor.
This is for a variety of reasons, but primarily that the author of the algorithm may not be in a position to match policies perfectly.
We might implement sort:
```
output_sender std::execution::sort(input_sender, executor, policy, comparison_function, range);
```

where the policy passed to match comparison_function is `par_unseq` but where a complex multi-stage sort needs par internally.
Here we end up with a question: do we have to restrict the interface to match the implementation? That might be hard.
The alternative is to transform the executor inside the algorithm.
Potentially using `require`, or a `with_policy` wrapper.
The problem is that this is semantically identical to passing the policy with the continuation, but syntactically worse:

```
output_sender std::execution::algorithm(input_sender, executor, policy) {
  auto seq_executor = with_policy(executor, seq);
  auto s1 = alg_stage_1(input_sender, seq_executor)

  auto par_executor = with_policy(executor, par);
  return alg_stage_2(s1, par_executor)
}
```
it is syntactically worse because the executor has been transformed and stored - it may drift far from the point of use, and thus create a risk of UB introduced during maintenance.

The alternative would appear structurally similar, but note that if we have attached a policy to the algorithm *if the executor is incapable of executing that way it can report the error*.
Whichever way we do this we have to decide what an executor is allowed to reject in terms of itself being associated with a policy, and the work being associated with a policy.


## Why use get_execution_policy on the receiver instead of a parameter?
This is a question both of scaling of compound algorithms and into the future.
A compound algorithm may need to make policy decisions at each stage.
For example, using async versions of `std::sort` and `std::transform`
```
output_sender std::execution::algorithm(input_sender, executor, policy) {
  auto s1 = sort(input_sender, executor, policy)
  return transform(s1, executor, policy)
}
```

based on prior work, each of these user-visible algorithms would take a policy.
They are all user-facing as well as implementation-details.
In the `get_execution_policy` model each of these can communicate a policy to the prior algorithm.

These algorithms are communicating single values rather than bulk.
If we want to be sure that we can implement a completion signal safely, that is useful because it allows the executor to guarantee safe communication with the next executor.

If `set_value` is to be called on the last completing task, and we know that the next algorithm constructed a receiver that is safe to call in a `par_unseq` agent, then the prior agent is safe to call it from its last completing `par_unseq` agent.
If not, then the executor has to setup a `par` agent to make that call from, because that is the most general method for chaining work across different contexts.

This is why we need `get_bulk_execution_policy` and `get_execution_policy`, representing the `set_next` and `set_value` calls separately, for their different meanings.
Both propagate through a receiver chain via `tag_invoke` forwarding.

Another future use case reason for doing it this way is the potential for compiler help.
Imagine something like:
```
bulk_schedule(exec, size, bulk_transform([]() [[with_compiler_generated_policy]] {...}))
```

where the compiler is allowed to analyse that code and wrap the lambda in a way that will expose the `get_execution_policy` query to `bulk_transform` and of course to `bulk_schedule`.
If the compiler sees a `std::mutex` in here, it might strengthen the policy requirement such that executor can report a mismatch.
We have provided a general mechanism, that future compilers can hook into, especially when generating custom accelerator code, to reduce the risk of mistakes on the part of the developer.



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
  - id: P2175
    citation-label: P2175
    title: "Composable cancellation for sender-based async operations"
    issued:
      year: 2020
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p2175r0.pdf
---
