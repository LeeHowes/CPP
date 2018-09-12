# A lazy simplification of P0443

## Summary

This paper seeks to add support for lazy task creation and deferred execution to P0443, while also simplifying the fundamental concepts involved in asynchronous execution. It seeks to do this as a minimal set of diffs to P0443. It achieves this by replacing P0443's six `Executor::*execute()` member functions with two lazy task constructors that each return a lazy Future-like type known as a "Sender". Work is actually enqueued by calling `submit()` on a Sender.

## Background

P0443 presents a unified abstraction for agents of asynchronous execution. It is the result of a long collaboration between experts from many subdomains of concurrent and parallel execution, and achieved consensus within SG1 and, to some degree, LEWG. Although there were known gaps in the abstraction, there were papers in flight to address them, and for all intents and purposes P0443 seemed on-target for a TS or, possibly even C++20.

At the Spring 2018 meeting in Rapperswil, a significant design problem was identified: poor support for lazy execution, whereby executors participate in the efficient construction and optimal composition of tasks _prior_ to those tasks being enqueued for execution. A solution was put forward by P1055: "senders" (called "deferred" in that paper) and "receivers". Senders and receivers could be freely composed into richer senders and receivers, and only when a receiver was "submitted" to a sender would that task be scheduled for execution.

P1055 represented a radical departure from -- and a conceptual simplification of -- the P0443 design. LEWG in particular found the promise of a simplified conceptualization in this space too good to pass up, and deferred the decision to merge P0443 until the alternative had been explored. There are, however, good reasons to be circumspect: a significant redesign now would almost certainly push Executors, and possibly the Networking TS which depends on it, out past C++20. Also, the authors of P1055 had not yet proved to the satisfaction of the experts in SG1 that senders and receivers could efficiently address all the use cases that shaped the design of P0443.

This paper seeks a middle way: a _minimal_ set of changes to P0443 that add proper support for lazy execution and brings a reduction in overall complexity from the point of view of both implementors of executors and their generic consumers. The changes are limited to such a degree that it should be obvious from inspection that no functionality has been lost.

## High-level changes

The root of the problem with P0443 and lazy asynchronous execution is that the Executor concepts' six execute member functions (oneway, twoway, and then_execute, and bulk variants) combined two things: task construction and work submission. Twoway and then_execute return futures, which are necessarily handles to already-running tasks.

* Replace the six Executor `execute` member functions with two (potentially lazy) task creation functions and one `submit` function.
* The task creation functions accept "senders" and callables, and return new senders.
* The `submit` function takes a receiver.

Additionally, define a new

## Why is lazy execution important?




## In what specific was was P0443 failing to address the lazy execution scenario?

## What are Senders and Receivers, and how do they help?

## Why is this not the radical change it appears to be?

Although senders and receivers seem like a new and unproven abstraction, they are really just a minor reformulation of concepts that already appear in P0443 and related papers.

### Senders are Futures

Four of the the six `execute` functions from P0443 return a type that satisfies the as-yet-unspecified `Future` concept. A `Future`, presumably, is a handle to an already-queued work item to which additional work can be chained by passing it back to an executor's `(bulk_)?then_execue` function, along wih a continuation that will execute when the queued work completes.

A sender is a generalization of a future. It _may_ be a handle to already queued work, or it may represent work that will be queued when a continuation has been attached, which is accomplished by passing a "continuation" to the sender's `submit()` member function.

### Receivers are Continuations

In P0443, a continuation was a simple callable, but that didn't give a convenient place for errors to go if the preceding computation failed. This shortcoming had already been recognized, and was the subject of P1054, which recommended _promises_ -- which have both a value and an error channel -- as a useful abstraction for continuations. A receiver is little more than a promise, with the addition of a cancellation channel.

In short, if you squint at P0443 and P1054, the sender and receiver concepts are already there. They just weren't fully spelled out.

### Task construction/submission is a decomposition of execution

The _real_ change in this proposal is to break the `execute` functions up into two steps: task creation (`s = ex.make_(bulk_)?value_task(...)`) and work submission (`s.submit(...)`). It is not hard to see how the reformulation is almost completely isometric with the original:

```c++
auto fut2 = ex.then_execute(fut1, f);
```

maps cleanly to

```c++
auto sender2 = ex.make_value_task(sender1, f);
```

with the option to submit the work later with some continuation (possibly a promise `p`):

```c++
sender2.submit(p);
```

There is one way in which the decomposition of `execute` into `make_value_task`/`submit` does not quite reach parity. That way, and its mitigation in the suggested compromise design, is discussed below.

# Fundamental differences between the compromise proposal and P0443

* P0443 `execute` functions return a future. The type of the future is under the executor's control. By splitting `execute` into lazy task costruction and a (`void`-returning) work submission API, we enable lazy futures because the code returning the future can rely on the fact that submit will be called by the caller. With that knowledge, the lazy future is safe to return because we can rely on it being run.
* We optionally lose the ability to block on completion of the task at task construction time. As submit is to be called anyway (except for the pure oneway executor case where submit is implicit) it is cleaner to apply the blocking semantic at this point if we are to have it at all. In particular, this approach allows us to build executors that return senders that block on completion but are still lazy.

# A lazy simplification of P0443
This document arose out of offline discussion largely between Lee, Bryce and David, as promised during the 2018-08-10 executors call.

The discussed compromise is that we should replace the enqueue  functions (`ex.*execute()`) with a limited set of lazy task factories. Any syntactic cost of providing a trivial output receiver to these operations can be hidden in wrapper functions. We do not expect a runtime cost assuming we also provide a trivial parameter-dropping receiver in the standard library against which the implementation can optimize.

| Aside: The Story |
|--------|
| The story here is that the `execute` functions from [P0443](http://wg21.link/P0443) were conflating two things: task creation and work queuing. The conflation interfered with lazy task submission, which was the thrust of [P1055](http://wg21.link/P1055). By teasing these two responsibilities apart and allowing an executor to customize them separately, we lose nothing and gain first-class support for lazy execution models. |
| |

By passing a full set of parameters to task construction functions, any task we place in a task graph may be type erased with no loss of efficiency. There may still be some loss of efficiency if the executor is type erased before task construction because the compiler may no longer be able to see from the actual executor into the passed functions. The earlier we perform this operation, however, the more chance there is of making this work effectively.

## The fundamental executor concepts
An executor should either be a sender of a sub executor, or a one way fire and forget entity.
These can be implemented in terms of each other, and so can be adapted if necessary, potentially with some loss of information.

We may `require` or `prefer` to switch between these.

### SenderExecutor
A SenderExecutor is a Sender and meets the requirements of Sender. The interface of the passed receiver should be noexcept.

| Function | Semantics |
|----------|-----------|
| `void submit(Receiver<` `SenderExecutor::SubExecutorType>) noexcept` |  At some future point calls `on_value(subExecutor)` on the receiver on success, where `subExecutor` is `*this` or some subset of `*this` as useful. At some future point calls `on_error(ErrorType)` on failure. |
| `ExecutorType executor() noexcept` | Returns an executor. By default, `*this`. |

`submit` and `executor` are required on an executor that has task constructors. `submit` is a fundamental sender operation that may be called by a task at any point.

Methods on the `Receiver` passed to `submit` will execute in some execution context owned by the executor. The executor should send a sub-executor to `on_value` to provide information about that context. The sub-executor may be itself. No sub-executor will be passed to `on_error`, and a call to `on_error` represents a failed enqueue.

To avoid deep recursion, a task may post itself directly onto the underlying executor, giving the executor a chance to pass a new sub executor. For example, if a prior task completes on one thread of a thread pool, the next task may re-enqueue rather than running inline, and the thread pool may decide to post that task to a new thread. Hence, at any point in the chain the sub-executor passed out of the executor may be utilized.

### OneWayExecutor
The passed function may or may not be `noexcept`. The behavior on an exception escaping the passed task is executor-defined.

| Function | Semantics |
|----------|-----------|
| `void execute(void(void))` | At some future point calls the passed callable. |

## Sender and Receiver concepts
Required additional concepts from P1055:
* `Receiver<To>`: A type that declares itself to be a receiver by responding to the `receiver_t` property query.
* `ReceiverOf<To, E, T...>`: A receiver that accepts an error of type `E` and the (possibly empty) tuple of values `T...`. (This concept is useful for constraining a `Sender`'s `submit()` member function.)
* `Sender<From>`: A type that declares itself to be a sender by responding to the `sender_t` property query.
* `TypesSender<From>`: A type that declares itself to be a sender by responding to the `sender_t` property query, returning a `sender_desc<E, T...>` sender descriptor that declares what types are to be passed through the receiver's error and value channels.
* `SenderTo<From, To>`: A `Sender` and `Receiver` that are compatible.

### Customization points

These concepts are defined in terms of the following global customization point objects (shown as free functions for the purpose of exposition):

#### Receiver-related customization points:

These customization-points are defined in terms of an exposition-only *`_ReceiverLike`* concept that checks only that a type responds to the `receiver_t` property query.

| Signature | Semantics |
|-----------|-----------|
| `template < _ReceiverLike To > ` <br/> `void set_done(To& to);` | **Cancellation channel:**<br/>Dispatches to `to.set_done()` if that expression is well-formed; otherwise, dispatches to (unqualified) `set_done(to)` in a context that doesn't contain the `std::set_done` customization point object. |
| `template < _ReceiverLike To, class E > ` <br/> `void set_error(To& to, E&& e);` | **Error channel:**<br/>Dispatches to `to.set_error((E&&) e)` if that expression is well-formed; otherwise, dispatches to (unqualified) `set_error(to, (E&&) e)` in a context that doesn't contain the `std::set_error` customization point object. |
| `template < _ReceiverLike To, class... Vs > ` <br/> `void set_value(To& to, Vs&&... vs);` | **Value channel:**<br/>Dispatches to `to.set_value((Vs&&) vs...)` if that expression is well-formed; otherwise, dispatches to (unqualified) `set_value(to, (Vs&&) vs...)` in a context that doesn't contain the `std::set_value` customization point object. |

#### Sender- and executor-related customization points:

These customization-points are defined in terms of an exposition-only *`_SenderLike`* concept that checks only that a type responds to the `sender_t` property query; and *`_Executor`* represents a refinement of `TypedSender` that is a light-weight handle to an execution context, and that sends a single value through the value channel representing another executor (or itself).

| Signature | Semantics |
|-----------|-----------|
| `template < _SenderLike From > ` <br/> `_Executor&& get_executor(From& from);` | **Executor access:**<br/>asks a sender for its associated executor. Dispatches to `from.get_executor()` if that expression is well-formed and returns a *`_SenderLike`*; otherwise, dispatches to (unqualified) `get_executor(from)` in a context that doesn't include the `std::get_executor` customization point object and that does include the following function:<br/><br/> &nbsp;&nbsp;`template<_Executor Exec>`<br/>&nbsp;&nbsp;` Exec get_executor(Exec exec) {`<br/>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;` return (Exec&&) exec;`<br/>&nbsp;&nbsp;` }` |
| `template < _Executor Exec, Sender Fut, class Fun >`<br/>`SenderOf<T'> make_value_task(` `Exec& exec, SenderOf<T...> fut, T'(T...) fun);` | **Task construction (w/optional eager submission):**<br/>Dispatches to `exec.make_value_task((Fut&&)fut, (Fun&&)fun)` if that expression is well-formed and returns a `Sender`; otherwise, dispatches to (unqualified) `make_value_task(exec, (Fut&&)fut, (Fun&&)fun)` in a context that doesn't include the `std::make_value_task` customization point object.<br/><br/>Logically, `make_value_task` constructs a new sender `S` that, when submitted with a particular receiver `R`, effects a transition to the execution context represented by `Exec`. In particular:<br/>&nbsp;&nbsp;* `submit(S,R)` constructs a new receiver `R'` that wraps `R` and `Exec`, and calls `submit(fut, R')`.<br/>&nbsp;&nbsp;* If `fut` completes with a cancellation signal by calling `set_done(R')`, then `R'`'s `set_done` method effects a transition to `exec`'s execution context and propagates the signal by calling `set_done(R)`.<br/>&nbsp;&nbsp;* Otherwise, if `fut` completes with an error signal by calling `set_error(R', E)`, then `R'`'s `set_error` method _attempts_ a transition to `exec`'s execution context and propagates the signal by calling `set_error(R, E)`. (The attempt to transition execution contexts in the error channel may or may not succeed. A particular executor may make stronger guarantees about the execution context used for the error signal.)<br/> &nbsp;&nbsp;* Otherwise, if `fut` completes with a value signal by calling `set_value(R', Vs...)`, then `R'`'s `set_value` method effects a transition to `exec`'s execution context and propagates the signal by calling `set_value(R, fun(Vs...))` -- or, if the type of `fun(Vs...)` is `void`, by calling `fun(Vs...)` followed by `set_value(R)`.<br/><br/>**Eager submission:**<br/> `make_value_task` may return a lazy sender, or it may eagerly queue work for submission. In the latter case, the task is executed by passing to `submit` an eager receiver such as a `promise` of a continuable `future` so that the returned sender may still have work chained to it.<br/><br/>**Guarantees:** <br/>The actual queuing of work happens-after entry to `make_value_task` and happens-before `submit`, when called on the resulting sender (see below), returns. |
| `template < _Executor Exec, Sender Fut, class Fun >`<br/>`SenderOf<T'> make_bulk_value_task(` `Exec& exec, SenderOf<T...> fut, F fun, ShapeFactory shf, SharedFactory sf, ResultFactory rf);` | **Task construction (w/optional eager submission):**<br/>Dispatches to `exec.make_bulk_value_task((Fut&&)fut, (Fun&&)fun, shf, sf, rf)` if that expression is well-formed and returns a `Sender`; otherwise, dispatches to (unqualified) `make_bulk_value_task(exec, (Fut&&)fut, (Fun&&)fun, shf, sf, rf)` in a context that doesn't include the `std::make_bulk_value_task` customization point object.<br/><br/>Logically, `make_bulk_value_task` constructs a new sender `S` that, when submitted with a particular receiver `R`, effects a transition to the execution context represented by `Exec`. In particular:<br/>&nbsp;&nbsp;* `submit(S,R)` constructs a new receiver `R'` that wraps `R` and `Exec`, and calls `submit(fut, R')`.<br/>&nbsp;&nbsp;* If `fut` completes with a cancellation signal by calling `set_done(R')`, then `R'`'s `set_done` method effects a transition to `exec`'s execution context and propagates the signal by calling `set_done(R)`.<br/>&nbsp;&nbsp;* Otherwise, if `fut` completes with an error signal by calling `set_error(R', E)`, then `R'`'s `set_error` method _attempts_ a transition to `exec`'s execution context and propagates the signal by calling `set_error(R, E)`. (The attempt to transition execution contexts in the error channel may or may not succeed. A particular executor may make stronger guarantees about the execution context used for the error signal.)<br/> &nbsp;&nbsp;* Otherwise, if `fut` completes with a value signal by calling `set_value(R', Vs...)`, then `R'`'s `set_value` method effects a transition to `exec`'s execution context and propagates the signal as if by executing the algorithm:<br/>`auto shr = sf();`<br/>`auto res = rf();`<br/>`for(idx : shf()) {`<br/>`fun(idx, t..., sf, rf);`<br/>`}`<br/>`set_value(R, std::move(res));`<br/> -- or, if the type of `RF()` is `void` or no `RF` is provided, as if by executing:<br/>`auto shr = sf();`<br/>`for(idx : shf()) {`<br/>`  fun(idx, t..., sf);`<br/>`}`<br/>`set_value(R);`.<br/><br/>**Eager submission:**<br/> `make_bulk_value_task` may return a lazy sender, or it may eagerly queue work for submission. In the latter case, the task is executed by passing to `submit` an eager receiver such as a `promise` of a continuable `future` so that the returned sender may still have work chained to it.<br/><br/>**Guarantees:** <br/>The actual queuing of work happens-after entry to `make_bulk_value_task` and happens-before `submit`, when called on the resulting sender (see below), returns. |
| `template < _SenderLike From, Receiver To >`<br/>`void submit(From& from, To to);` | **Work submission.**<br/>  |





## Ordering guarantees
There are two strong guarantees made here, to allow eager execution:
 * Memory operations made before the call to a task constructor happen-before the execution of the task.
 * Completion of the task happens-before a call to `set_value` on the next receiver in the chain, including the implicit receiver implied by a task constructor.

A task may therefore run at any point between constructing it, and being able to detect that it completed, for example, by seeing side effects in a subsequent task. This allows tasks to run on construction, in an eager fashion, and does not allow a user to rely on laziness.

The definition of the API does, however, allow laziness and as such no sender is guaranteed to run its contained work until a receiver is passed to its `submit` method (or it is passed to a task constructor that calls `submit` implicitly).

As an optional extension, we may define properties that define whether tasks are guaranteed, or allowed, to run eagerly on construction, assuming that their inputs are ready.

## Before and after
In the table below, where a future is returned this is represented by the promise end of a future/promise pair in the compromise version. Any necessary synchronization or storage is under the control of that promise/future pair, rather than the executor, which will often allow an entirely synchronization-free structure.

| Before | After |
|--------|-------|
| `executor.execute(f)` | `executor.execute(f)` |
| `executor.execute(f)` | `executor.make_value_task(sender{}, f).submit(receiver{})` |
| `fut = executor.twoway_execute(f)` | `executor.make_value_task(sender{}, f).submit(futPromise)` |
| `fut' = executor.then_execute(f, fut)` | `executor.make_value_task(fut, f).submit(fut'Promise)` |
| `executor.bulk_execute(f, n, sf)` | `executor.make_bulk_value_task(sender{}, f, n, sf, []{}).submit(receiver{})` |
| `fut = executor.bulk_twoway_execute(f, n, sf, rf)` | `executor.make_bulk_value_task(sender{}, f, n, sf, rf).submit(futPromise{})` |
| `fut' = executor.bulk_then_execute(f, n, sf, rf, fut')` | `executor.make_bulk_value_task(fut, f, n, sf, rf).submit(fut'Promise{})` |

If a constructed task is type erased, then it may benefit from custom overloads for known trivial receiver types to optimize. If a constructed task is not type erased then the outgoing receiver will trivially inline.

We do not expect any performance difference between the two forms of the one way execute definition. The task factory-based design gives the opportunity for the application to provide a well-defined output channel for exceptions. For example, the provided output receiver could be an interface to a lock-free exception list if per-request exceptions are not required.

# Extensions
A wide range of further customization points should be expected. The above description is the fundamental set that matches the capabilities in P0443. To build a full futures library on top of this we will need, in addition:
* A blocking operation for use from concurrent execution contexts. For consistency we suggest defining this as an overload of the `sync_wait` proposal from Lewis Baker, D1171. That paper proposes `std::this_thread::sync_wait(Awaitable)`. We should define a customization point that allows `sync_wait(Sender)` to be implemented optimally.
* A share operation that takes a `Sender` and splits it such that when the input to the `Sender` is complete, multiple `Receivers` may be triggered.
* A join operation that takes multiple `Senders` and constructs a single `Sender` out of them that joins the values in some fashion.
* An unwrap operation that takes a `Sender<Sender<T>>` and returns a `Sender<T>`.
* A task type that supports both value and error handling: `make_value_error_task(Value'(Value), Error'(Error))`, for example.
* A task type that handles only errors and bypasses values: `make_error_task(Error'(Error))` for example.
* Potentially a wider range of parallel algorithm customizations.

By defining these as above in terms of a customization point that calls a method on the executor if that method call is well-formed we can relatively easily extend the API piecemeal with these operations as necessary.

# Q&A

## Q: What do lazy execution and eager execution mean?

- *Lazy execution* is when a node in a graph of operations is constructed by storing state and does not start any work.
- *Eager execution* is when a node in a graph of operations starts working as soon as it is constructed.

These can be demonstrated without adding concurrency using normal functions.

Lazy execution:

```cpp
auto f0 = []{ return 40; };
auto f1 = [f0]{ return 2 + f0(); }
// f0 has not been called.

// execute the graph
auto v1 = f1();
// f0 and f1 are complete
```

Eager execution:

```cpp
auto f0 = []{ return 40; };
auto f1 = [auto v0 = f0()]{ return 2 + v0; }
// f0 has been called.

// execute the graph
auto v1 = f1();
// f0 and f1 are complete
```

Without concurrency the only difference is the order of execution. This changes when concurrency is introduced.

## Q: How is eager execution more expensive than lazy execution when concurrency is introduced?

**shared_state and synchronization**

When concurrency is introduced, the result of an operation is sent as a notification. With coroutines that notification is hidden from users, but is still there in the coroutine machinery.

Often the notification is a callback function. As before, the effect of adding callbacks for eager and lazy execution can be demonstrated without adding concurrency using normal functions.

Lazy execution:

```cpp
auto f0 = [](auto cb0){ cb0(40); };
auto f1 = [f0](auto cb1){ f0([cb1](auto v){ cb1(2 + v); }); }
// f0 has not been called.

// execute the graph
f1([](auto v1){});
// f0 and f1 are complete
```

Eager execution:

```cpp
auto f0 = [](auto cb0){ cb0(40); };
auto sv0 = make_shared<int>(0);
auto f1 = [auto sv0 = (f0([sv0](int v0){ sv0 = v0; }), sv0)](auto cb1){
  cb1(2 + *sv0);
}
// f0 has been called.

// execute the graph
f1([](auto v1){});
// f0 and f1 are complete
```

The shared state is required in the eager case because `cb1` is not known at the time that `f0` is called. In the lazy case `cb1` is known at the time that `f0` is called and no shared state is needed.

The shared state in the eager case is already more expensive, but when concurrency is added to the eager case there is also a need to synchronize the `f0` setting `sv0` and the `f1` accessing `sv0`. This increases the complexity and expense dramatically.

When composing concurrent operations, sometimes eager execution is required. Any design for execution must support eager execution. Lazy execution is almost always the better choice and since lazy execution is also more efficient lazy execution is the right default.

## Q: What makes `std::promise`/`std::future` expensive?

**eager execution**

When it is possible to call `promise::set_value(. . .)` before `future::get()` then state must be shared by the future and promise. When concurrent calls are allowed to the promise and future, then the access to the shared state must be synchronized.

`std::promise`/`std::future` are expensive because the interface forces an implementation of eager execution.

## Q: How is `void sender::submit(receiver)` different from `future<U> future<T>::then(callable)`?

**termination**

`then()` returns another future. `then()` is not terminal, it always returns another future with another `then()` to call. To implement lazy execution `then()` would have to store the callable and not start the work until `then()` on the returned future was called. Since `then()` on the returned future is also not terminal, it either must implement expensive eager execution or must not start the work, which leaves no way to start the work without using an expensive eager execution implementation of `then()`

`submit()` returns void. returning void makes `submit()` terminal. When `submit()` is an implementation of lazy execution the work starts when `submit()` is called and was given the continuation function to use when delivering the result of the work. When `submit()` is an implementation of expensive eager execution the work was started before `submit()` is called and synchronization is used to deliver the result of the work to the continuation function.

## Q: How is `void sender::submit(receiver)` different from `void executor::execute(callable)`?

**signals**

`execute()` takes a callable that takes void and returns void. The callable will be invoked in an execution context specified by the executor. any failure to invoke the callable or any exception thrown by the callable after `execute()` has returned must be handled by the executor. there is no signal to the function on failure or cancellation or rejection. When the callable is a functor the destructor will be run on copy, move, success, failure and rejection with no parameters to distinguish them.

`submit()` takes a receiver that has three methods.

- `value()` will be invoked in an execution context specified by the executor. `value()` does any work needed.
- `error()` will be invoked in an execution context specified by the executor. any failure to invoke `value()` or any exception thrown by `value()` after `execute()` has returned is reported to the receiver by invoking `error()` and passing the error as a parameter
- `done()` will be invoked in an execution context specified by the executor. `done()` handles cases where the work was cancelled (cancellation is not an error).
