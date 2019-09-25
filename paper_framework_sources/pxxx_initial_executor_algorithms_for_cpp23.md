---
title: "Towards C++23 executors: A proposal for an initial set of algorithms"
document: PXXXR0
date: 2019-09-14
audience: SG1, Library Evolution
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---

# Introduction
To make the interaction of Sender/Callback and the executors in [@P1660], [@P0443] and earlier papers concrete and usable, we should start with some minimal set of asynchronous algorithms.

The set of algorithms we propose here is intended to cover the original use cases of [@P0443], extended to include mutation of the value and error channels to fully utilize the asynchronous model.
It is by no means a complete set, and the set of error handling algorithms proposed here in particular is merely exemplary.
We fully expect that in the long run a full set of sort, reduction, group_by and other similar algorithms will be included.
The principles embodied here should carry cleanly into that wider algorithm set.

# Impact on the Standard
Starting with [@P1660] as a baseline we have the following algorithms or variants of algorithms, which may be free functions or be described using methods in the final definition:

 * `execute(Executor, Invocable) -> void`
 * `execute(Executor, Callback) -> void`
 * `submit(Executor, Callback) -> void`
 * `submit(Sender, Callback) -> void`
 * `schedule(Scheduler) -> Sender`
 * `sync_wait(Scheduler) -> T`

and the following Concepts:

 * `Executor`
 * `CallbackSignal`
 * `Callback`
 * `Sender`

For the the purposes of the code below `Sender<void>` represents a `Sender` that calls the `Callback`'s value channel with a void value type and `Sender<T>` would pass a `T` to that channel.
This will make chaining examples clearer.
An `Executor` when treated as a `Sender` is hence a `Sender<void>`, such that it will call the `Invocable`'s operator() with no value on success or will call `done()` or `error(E)` with some error type under implementation-defined conditions.

These cover basic one-way execution cases - they are fire and forget from the point of view of the statement.
They may or may not be asynchronous.

We propose adding the following two-way algorithms as a foundational set covering the major use cases we saw in earlier versions of [@P0443] and that we expect in code using `folly::Future` or other, similar, asynchronous libraries ^[Naming TBD, we're using slightly long-winded names here to be completely clear on the purpose.].

The sender algorithms are defined as per range-adapters such that:

 * `algorithm(sender, args...)`
 * `algorithm(args...)(sender)`
 * `sender | algorithm(args...)`

are equivalent ^[24.7.1 Precise wording for this can come later.].

Sender algorithms are defined as customization point objects as follows:

 * `auto std::tbd::scalar_execute(S&& s, F&& f)`
   * denotes a customization point object that calls `return std::tbd2::scalar_execute(std::forward<S>(s), std::forward<F>(f))` if no customization exists.
   * *Remarks*: `F` is an invocable of the form `void(void)`.
   * *Remarks*: `S` is a `Sender<void>`
   * *Returns*: A `Sender<void>` representing:
      * On successful completion of `s` the execution of `f()` if `f()` completes successfully.
      * On `error` or `done` signals arising from `s` these signals will propagate.

 * `auto std::tbd::scalar_transform(S&& s, F&& f)`
   * denotes a customization point object that calls `return std::tbd2::scalar_transform(std::forward<S>(s), std::forward<F>(f))` if no customization exists.
   * *Remarks*: `F` is an invocable of the form `T2(T)`.
   * *Remarks*: `S` is a `Sender<T>`
   * *Returns*: A `Sender<T2>` representing:
      * On successful completion of `s` the execution of `f(t)` if `f(t)` completes successfully.
      * On `error` or `done` signals arising from `s` these signals will propagate.

 * `auto std::tbd::bulk_execute(S&& s, Range<Idx> r, SharedFactory sf, F&& f)`
   * denotes a customization point object that calls `return std::tbd2::bulk_execute(std::forward<S>(s), r, sf, std::forward<F>(f))` if no customization exists.
   * *Remarks*: `F` is an invocable of the form `void(Idx, invoke_result_t<SF>())`.
   * *Remarks*: `S` is a `Sender<void>`
   * *Returns*: A `Sender<void>` representing:
      * On successful completion of `s`, for some `sh = sf()` the execution of `f(i, t, sh)` for each `t` in `r` if `f(t)` completes successfully.
      * On `error` or `done` signals arising from `s` these signals will propagate.

 * `auto std::tbd::bulk_transform(S&& s, F&& f, SharedFactory sf)`
   * denotes a customization point object that calls `return std::tbd2::bulk_transform(std::forward<S>(s), std::forward<F>(f), sf)` if no customization exists.
   * *Remarks*: `F` is an invocable of the form `T2(Idx, T, invoke_result_t<SF>())`.
   * *Remarks*: `S` is a `Sender<Range<T>>`
   * *Returns*: A `Sender<Range<T2>>` representing:
     * On successful completion of `s`, for some `sh = sf()` a range resulting from the execution of `f(i, t, sh)` for each `t` in the input range if all `f(t)`s complete successfully.
     * On `error` or `done` signals arising from `s` these signals will propagate.

For complete description we also describe the following set of error handling options similarly.
We might decide to only include one initially, or to instead offer a single form that combines these.

 * `std::tbd::handle_error_and_succeed(S&& s, F&& f)`
   * denotes a customization point object that calls `return std::tbd2::handle_error_and_succeed(std::forward<S>(s), std::forward<F>(f))` if no customization exists.
   * *Remarks*: `F` is an invocable of the form `T2(E)`.
   * *Remarks*: `S` is a `Sender<T>`.
   * *Returns*: A `Sender<T>` representing:
      * On completion of `s` with a call to `error(e)` the result of the execution of `f(e)` if `f()` completes successfully, propagated as success.
      * On success or `done` signals arising from `s` these signals will propagate.

 * `std::tbd::handle_error_and_cancel(S&& s, F&& f)`
   * denotes a customization point object that calls `return std::tbd2::handle_error_and_cancel(std::forward<S>(s), std::forward<F>(f))` if no customization exists.
   * *Remarks*: `F` is an invocable of the form `void(E)`.
   * *Remarks*: `S` is a `Sender<T>`.
   * *Returns*: A `Sender<T>` representing:
      * On completion of `s` with a call to `error(e)` will execute `f(e)` and propagate a signal to `done()`.
      * On success or `done` signals arising from `s` these signals will propagate.

 * `std::tbd::transform_error(S&& s, F&& f)`
   * denotes a customization point object that calls `return std::tbd2::transform_error(std::forward<S>(s), std::forward<F>(f))` if no customization exists.
   * *Remarks*: `F` is an invocable of the form `E2(E)`.
   * *Remarks*: `S` is a `Sender<T>`.
   * *Returns*: A `Sender<T>` representing:
      * On completion of `s` with a call to `error(e)` will execute `f(e)` and propagate the result `e2` as `error(e2)`.
      * On success or `done` signals arising from `s` these signals will propagate.

 * `std::tbd::on(S&& s, Sch&& sch)`
   * denotes a customization point object that calls `return std::tbd2::on(std::forward<S>(s), std::forward<Sch>(sch))` if no customization exists.
   * *Remarks*: `S` is a `Sender<T>`.
   * *Remarks*: `Sch` is a `Scheduler`.
   * *Returns*: A `Sender<T>` representing:
      * On completion of `s` work will transition onto `sch`'s context.
      * Success signals *must* transition to `sch`'s context.
      * It is permissible for `error` and `done` signals to propagate on `s`'s context.s

  * `std::tbd::sync_wait(S&& s)`
    * denotes a customization point object that calls `return std::tbd2::sync_wait(std::forward<S>(s))` if no customization exists.
    * *Remarks*: `S` is a `Sender<T>`.
    * *Remarks*: Blocks the calling thread of execution until the result is available.
    * *Returns*:
      * A result `T` representing the value passed in the success channel.
      * or an exception representing either the error type passed in the `error` channel, or some representation of the `done` channel to be discussed ^[Converting the done channel to an exception is only one option for moving from the more flexible world of Senders to normal functions.]



The default implementations are defined in the `std::tbd2` namespace.
Their interfaces match those of the customization point objects, but the behaviour is more precise.


 * `std::tbd2::scalar_execute(S&& s, F&& f)`
   * Constructs a `Callback` `c` that wraps `f` in its success channel, and unit functions in the `error` and `done` channels. Calls `submit(std::forward<S>(s), c)`.
   * Returns a `Sender<void>` that is signaled on completion of `f()` in its success channel, and on propagation of `error` or `done` otherwise.
   * If `f` is not marked `noexcept` and throws, the exception must be caught and propagated on the `error` channel of the returned `Sender`.

 * `std::tbd2::scalar_transform(S&& s, F&& f)`
   * Constructs a `Callback` `c` that wraps `f` in its success channel, and unit functions in the `error` and `done` channels. Calls `submit(std::forward<S>(s), c)`.
   * Returns a `Sender<T2>` that is signaled on completion with the result of `f(value)` in its success channel, and on propagation of `error` or `done` otherwise.
   * If `f` is not marked `noexcept` and throws, the exception must be caught and propagated on the `error` channel of the returned `Sender`.

 * `std::tbd2::bulk_execute(S&& s, Range<Idx> r, SharedFactory sf, F&& f)`
   * Constructs a `Callback` `c` that wraps:
     * in its success channel a call to `sf()` producing some result `sfh`, followed by an unsequenced equivalent of the loop: `for(idx : r){f(idx, shf);}`.
     * `error` and `done` channels the unit function.
     Calls `submit(std::forward<S>(s), c)`.
   * Returns a `Sender` that signals the success channel of the returned `Sender<void>` when the unsequenced loop completes, and on propagation of success or `done` otherwise.
   * If `f` is not marked `noexcept` and throws, the exception must be caught and propagated on the `error` channel of the returned `Sender`.

 * `std::tbd2::bulk_transform(S&& s, F&& f, SharedFactory sf)`
   * Constructs a `Callback` `c` that wraps:
     * in its success channel a call to `sf()` producing some result `sfh`, followed by an unsequenced equivalent of the loop: `for(idx : iota_view(0, r.size())){or[idx] = f(r[idx], shf);}` where `r` is the range passed in the value channel of `c` and `or` is an identically sized output range of type `T` where `f(r[idx], shf) -> T`.
     * `error` and `done` channels the unit function.
     Calls `submit(std::forward<S>(s), c)`.
   * Returns a `Sender` that signals the success channel of the returned `Sender<T>` with `or` when the unsequenced loop completes, and on propagation of success or `done` otherwise.
   * If `f` is not marked `noexcept` and throws, the exception must be caught and propagated on the `error` channel of the returned `Sender`.

 * `std::tbd2::handle_error_and_succeed(S&& s, F&& f)`
   * Constructs a `Callback` `c` that wraps `f` in its `error` channel, and unit functions in the success and `done` channels. Calls `submit(std::forward<S>(s), c)`.
   * Returns a `Sender` that signals the success channel of the returned `Sender<T>` with the result of `f(err)`, and on propagation of success or `done` otherwise. The value type of the returned `Sender` is the same as the value type of the incoming `Sender` such that the success signal can propagate trivially.
   * If `f` is not marked `noexcept` and throws, the exception must be caught and propagated on the `error` channel of the returned `Sender`.

 * `std::tbd2::handle_error_and_cancel(S&& s, F&& f)`
   * Constructs a `Callback` `c` that wraps `f` in its `error` channel, and unit functions in the success and `done` channels. Calls `submit(std::forward<S>(s), c)`.
   * Returns a `Sender` that calls `f(err)` in its error channel and subsequently signals `done()` from the returned `Sender`, and on propagation of success or `done` otherwise.
   * If `f` is not marked `noexcept` and throws, the exception must be caught and propagated on the `error` channel of the returned `Sender`.

 * `std::tbd::transform_error(S&& s, F&& f)`
   * Constructs a `Callback` `c` that wraps `f` in its `error` channel, and unit functions in the success and `done` channels. Calls `submit(std::forward<S>(s), c)`.
   * Returns a `Sender` that is signaled on error with the result of `f(err)` in its error channel, and on propagation of success or `done` otherwise.
   * If `f` is not marked `noexcept` and throws, the exception must be caught and propagated on the `error` channel of the returned `Sender`.

 * `std::tbd2::on(S&& s, Sch&& sch)`
   * Constructs a `Callback` `c1` that triggers, on completion of `s`, wrapping the resulting value or error into a `Callback`, `c2` and calls `std::tbd::submit(std::tbd::schedule(std::forward<Sch>(sch)), std::move(c))`. Calls `submit(std::forward<S>(s), c1)`.

  * `std::tbd2::sync_wait(S&& s)`
   * Constructs a `Callback` `c` that satisfies some `condition_variable` `cv` and, on success, stores the value or exception in a local variable. Blocks the calling thread until the condition variable is ready and subsequently returns the value or throws the exception.

## Customizing
Any of the above algorithms may customize themselves on the passed Sender.
To avoid ADL issues this would preferably be achieved through the `tag_invoke` mechanism described in [@PTAGINVOKE].

Note that the above CPO descriptions do not specify that `submit` be called on the input `Sender`.
This is important because one benefit of customization is efficient chaining, and in such a chain we will lose efficiency if we have to fall back to a CPU synchronization, satisfying a `Callback` contract in the middle of the chain.
It would also preclude cancelling an entire optimised chain atomically on failure if the assumption was that intermediate nodes must see an error or `done()` signal.
To be efficient we need to be able to shift error handling to the end of an optimised work chain.

If I have a custom Sender, followed by an algorithm customized on that Sender type, I can use internal facilities to chain them.
One option would be a simple sequencing token managed by a runtime.

Only at the point where I am applying an algorithm that is not customised on the `Sender` type to a `Sender` must I fall back to constructing a `Callback` and passing it to the `submit` operation - in this way we achieve interoperability.


## An example

The expectation here would be that we have some chain of work (where `inline_sender` immediately bounces a call to the success channel of the passed callback `c` when `inline_sender::submit(c)` is called):
```
auto e1 = namespace1::executor{};
auto e2 = namespace2::executor;
auto s1 = inline_sender{} | on(namespace1::e1) |
  scalar_execute(f1);
auto s2 = std::move(s1) | on(namespace2::e2) |
  scalar_execute(f2);
auto s3 = std::move(s2) | on(namespace1::e1);
sync_wait(std::move(s2));
```

`on(namespace1::e1)` does not find a customization of the algorithm for the executor, or for the `inline_sender` and uses the default implementation, returning a standard Sender, say `tbd2::on_sender`. `scalar_execute(f1)` does not find a customization of scalar_execute for `tbd2::on_sender` and so calls `tbd2::scalar_execute(input_sender, f1)`, returning a standard Sender again: `tbd2::scalar_execute_sender`.

So we have:
```
auto e1 = namespace1::executor{};
auto e2 = namespace2::executor;
tbd2::scalar_execute_sender s1 = inline_sender{} | tbd2::on(namespace1::e1) |
  tbd2::scalar_execute(f1);
auto s2 = std::move(s1) | on(e2) |
  scalar_execute(f2);
auto s3 = std::move(s2) | on(e1);
sync_wait(std::move(s2));
```

`on(namespace2::e2)` here has two parameters, `s1` and `e2`.
There is no customization of `on` in this case on `tbd2::scalar_execute_sender`, this is a standard library type.
However, the implementor of `e2` has provided a customization in `namespace2`.
We therefore treat that as a call to `namespace2::on(s1, e2)`.
Its customization performs a normal submit to `s1`, because it knows nothing about `s1` and has to do that for interoperability, but it returns a custom `Sender` `namespace2::on_sender`.

`scalar_execute(f2)` is interesting.
This is a call to `scalar_execute(namespace2::on_sender, f2)` and we assume it is customised on `namespace2::on_sender`.
Let's say that this sender exposes a queue in an underlying runtime via an executor that also acts like a serialization token.
This call can be viewed as something like `return make_sender(s2.get_executor().enqueue_invocable(f2));`.
That is it bypasses the `submit` operation entirely, uses internal functionality to chain work directly onto the executor, and stores the return value in a new sender.

```
auto e1 = namespace1::executor{};
auto e2 = namespace2::executor;
tbd2::scalar_execute_sender s1 = inline_sender{} | tbd2::on(namespace1::e1) |
  tbd2::scalar_execute(f1);
auto s2a = std::move(s1) | on(e2);
namespace2::scalar_execute_sender s2 = namespace2::make_sender(s2a.get_executor().enqueue_invocable(f2));
auto s3 = std::move(s1) | on(e1);
sync_wait(std::move(s2));
```

In the next line we transition back to `e1`.
In this case on may customize on `s2`, but that implementation will not customize on e1.
It will return a `tbd2::scalar_execute_sender` again that implements everything in the fallback fashion, but the `submit` method may use an internal trick to transition back from the work queue.
Let's say that that transition method is called `enqueue_cpu_function`.
The `sync_wait` call will not be customized, will construct a `Callback` that triggers a standard condition variable^[Or a newer synchronization primitive, it is up to the standard library implementor.] and blocks on it.

Logically then we end up with something like:
```
auto e1 = namespace1::executor{};
auto e2 = namespace2::executor;
tbd2::scalar_execute_sender s1 = inline_sender{} | tbd2::on(namespace1::e1) |
  tbd2::scalar_execute(f1);
auto s2a = std::move(s1) | on(e2);
namespace2::scalar_execute_sender s2 = namespace2::make_sender(s2a.get_executor().enqueue_invocable(f2));
auto s3 = std::move(s1) | on(e1);
// Construct callback and pass to submit
s3.submit(make_callback([](){cv.notify();});
cv.wait();

// Implementation of s3.submit() uses internal operation to trigger transition to standard operations
void namespace2::scalar_execute_sender::submit(Callback&& c) {
  get_executor().enqueue_cpu_function([c](){c();});
}
```
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
  - id: PTAGINVOKE
    citation-label: PTAGINVOKE
    title: ""
    issued:
      year: 2019
    URL:



---
