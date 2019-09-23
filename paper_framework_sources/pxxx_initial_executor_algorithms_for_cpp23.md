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


# Impact on the Standard
Starting with [@P1660] as a baseline we have the following algorithms or variants of algorithms, which may be free functions or be described using methods in the final definition:

 * `execute(Executor, Invocable) -> void`
 * `execute(Executor, Callback) -> void`
 * `submit(Executor, Callback) -> void`
 * `submit(Sender, Callback) -> void`
 * `schedule(Scheduler) -> Sender`

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

 * `scalar_execute`
   * takes an `Invocable` that is run or enqueued to an executor when the input `Sender` completes with **success**, passing a success signal on to the output `Sender` and propagates a `done()` or `error(E)` otherwise.
   * `scalar_execute(Sender<void>, void(void)) -> Sender<void>`
 * `scalar_transform`
   * takes an `Invocable` that applies that function to the success result of the input `Sender` and passes the transformed value to the output sender on success, but propagates `done()` or `error(E)` otherwise.
   * `scalar_transform(Sender<T>, T2(T)) -> Sender<T2>`
 * `bulk_execute`
   *
   * `bulk_execute(Sender<void>, Range<Idx>, SharedFactory, void(const Idx&, s&)) -> Sender<void>`
   * given `SharedFactory sf; auto s = sf()`
   * the `Invocable` `f` will be invoked as `f(r, s)` for each r in the passed range.
 * `bulk_transform`
 * `handle_error_and_succeed`
 * `handle_error_and_cancel`
 * `on`
   * takes a `Sender` and an `Executor` and returns a `Sender` that triggers the same signal on the passed executor.
   * `on(Sender<T>, Executor) -> Sender<T>`

These algorithms are all defined as customization point objects.
That is that the name, for example, `std::tbd::scalar_execute` denotes a customization point object that calls `std::tbd2::scalar_execute` if no customization exists.
In this fashion we may specialize the behaviour of any algorithm for a class of `Senders` that will be created by an executor under our control.

## Customizing
% TODO: How does this work?


# An example
% Chain starting with Schedule using custom types


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
