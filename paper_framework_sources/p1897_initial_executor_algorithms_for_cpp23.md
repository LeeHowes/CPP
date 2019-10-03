---
title: "Towards C++23 executors: A proposal for an initial set of algorithms"
document: D1897R0
date: 2019-09-14
audience: SG1
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---

# Introduction
In [@P0443R11] we have included the fundamental principles described in [@P1660R0], and the fundamental requirement to customize algorithms.
In recent discussions we have converged to an understanding of the `submit` operation on a `sender_to` acting as a fundamental interoperation primitive, and algorithm customization giving us full flexibility to optimise, to offload and to avoid synchronization in chains of mutually compatible algorithm customizations.

As a starting point, in that paper we only include a `bulk_execute` algorithm, that satisfies the core requirement we planned with P0443 to provide scalar and bulk execution.
To make the C++23 solution completely practical, we should extend the set of algorithms, however.
This paper suggests an expanded initial set that enables early useful work chains.
This set is intended to act as a discussion focus for us to discuss one by one, and to analyse the finer constraints of the wording to make sure we do not over-constrain the design.

In the long run we expect to have a much wider set of algorithms, potentially covering the full set in the current C++20 parallel algorithms.
The precise customization of these algorithms is open to discussion: they may be individually customised and individually defaulted, or they may be optionally individually customized but defaulted in a tree such that customizing one is known to accelerate dependencies.
It is open to discussion how we achieve this and that is an independent topic, beyond the scope of this paper.

# Impact on the Standard
Starting with [@P0443R11] as a baseline we have the following customization points:

 * `execute(executor, Invocable) -> void`
 * `submit(sender_to, Callback) -> void`
 * `schedule(Scheduler) -> Sender`
 * `done`
 * `error`
 * `value`

and the following Concepts:

 * `executor`
 * `scheduler`
 * `callback_signal`
 * `callback`
 * `sender_to`

First, we should add wording such that the sender algorithms are defined as per range-adapters such that:

 * `algorithm(sender, args...)`
 * `algorithm(args...)(sender)`
 * `sender | algorithm(args...)`

are equivalent.
In this way we can use `operator|` in code to make the code more readable.

We propose immediately discussing the addition of the following algorithms:

 * `is_noexcept_sender(sender_to) -> bool`
 * `just(T) -> sender_to`
 * `on(sender_to, scheduler) -> sender_to`
 * `sync_wait(sender_to) -> T`
 * `transform(sender_to, invocable) -> sender_to`
 * `bulk_transform(sender_to, invocable) -> sender_to`
 * `handle_error(sender_to, invocable) -> sender_to`

Details below are in loosely approximated wording and should be made consistent with [@P0443R11] and the standard itself when finalized.

## is_noexcept_sender
### Summary
Queries whether the passed sender will ever propagate an error when treated as an r-value to `submit`.

### Wording
The name `execution::is_noexcept_sender` denotes a customization point object.
The expression `execution::is_noexcept_sender(S)` for some subexpression `S` is expression-equivalent to:

 * `S.is_noexcept()`, if that expression is valid.
 * Otherwise, `is_noexcept_sender(S)`, if that expression is valid, with overload resolution performed in a context that includes the declaration
```
        template<class S>
          void is_noexcept_sender(S) = delete;
```
    and that does not include a declaration of `execution::is_noexcept_sender`.

 * Otherwise, `false`.

If possible, `is_noexcept_sender` should be `noexcept`.

If `execution::is_noexcept_sender(s)` returns true for a `sender_to` `s` then it is guaranteed that `s` will not call `error` on any `callback` `c` passed to `submit(s, c)`.

## just
### Summary
Returns a sender that propagates the passed value inline when `submit` is called.
This is useful for starting a chain of work.

### Wording
The expression `execution::just(t)` returns a sender_to, `s` wrapping the value `t`.

 * If `t` is nothrow movable then `execution::is_noexcept_sender(s)` shall be constexpr and return true.
 * When `execution::submit(s, c)` is called for some `c`, and r-value `s` will call `execution::value(c, std::move(t))`, inline with the caller.
 * When `execution::submit(s, c)` is called for some `c`, and l-value `s` will call `execution::value(c, t)`, inline with the caller.
 * If moving of `t` throws, then will catch the exception and call `execution::error(c, e)` with the caught `exception_ptr`.


## sync_wait

### Summary
Blocks the calling thread to wait for the resulting sender to complete.
Returns a std::optional of the value value or throws if an exception is propagated.
On propagation of the `done()` signal, returns an empty optional.

### Wording
The name `execution::sync_wait` denotes a customization point object.
The expression `execution::sync_wait(S)` for some subexpression `S` is expression-equivalent to:

 * `S.sync_wait()` if that expression is valid.
 * Otherwise, `sync_wait(S)`, if that expression is valid with overload resolution performed in a context that includes the declaration
 ```
         template<class S>
           void sync_wait(S) = delete;
 ```
   and that does not include a declaration of `execution::sync_wait`.

 * Otherwise constructs a callback, `c` over an implementation-defined synchronization primitive and passes that callback to `execution::submit(S, c)`.
   Waits on the synchronization primitive to block on completion of `S`.

   * If `value` is called on `c`, returns a `std::optional` wrapping the passed value.
   * If `error` is called on `c`, throws the error value as an exception.
   * If `done` is called on `c`, returns an empty `std::optional`.

If `execution::is_noexcept_sender(S)` returns true at compile-time, and the return type `T` is nothrow movable, then `sync_wait` is noexcept.
Note that `sync_wait` requires `S` to propagate a single value type.

## on
### Summary
Transitions execution from one executor to the context of a scheduler.
That is that:
```
auto sender2 = schedule(scheduler1);
sender1 | on(std::move(sender2)) | bulk_execute(f)...
```

will create return a sender that runs in the context of `sender2` such that `f` will run on the context of `scheduler1`, potentially customized, but that is not triggered until the completion of `sender1`.

`on(S1, S2)` may be customized on either or both of `S1` and `S2`.
For example if both the

### Wording
The name `execution::on` denotes a customization point object.
The expression `execution::on(S1, S2)` for some subexpressions `S1`, `S2` is expression-equivalent to:

 * `S1.on(S2)` if that expression is valid.
 * Otherwise, `on(S1, S2)` if that expression is valid with overload resolution performed in a context that includes the declaration
 ```
         template<class S1, class S2>
           void on(S1, S2) = delete;
 ```

 * Otherwise constructs a callback `c` such that when `value`, `error` or `done` is called on `c` the value(s) or error(s) are packaged, and a callback `c2` constructed such that when `execution::value(c2)` is called, the stored value or error is transmitted and `c2` is submitted to `S2`.
 * The returned sender_to's value types match those of `sender1`.
 * The returned sender_to's execution context is that of `sender2`.

## transform
### Summary
Applies a function `f` to the value channel of a sender such that some type list `T...` is consumed and some type `T2` returned, resulting in a sender that transmits `T2` in its value channel.
This is equivalent to common `Future::then` operations, for example:
```
 Future<float>f = folly::makeFuture<int>(3).thenValue(
   [](int input){return float(input);});
```

### Wording
The name `execution::transform` denotes a customization point object.
The expression `execution::transform(S, F)` for some subexpressions `S` and `F` is expression-equivalent to:

 * `S.transform(F)` if that expression is valid.
 * Otherwise, `transform(S, F)`, if that expression is valid with overload resolution performed in a context that includes the declaration
 ```
         template<class S, class F>
           void transform(S, F) = delete;
 ```
   and that does not include a declaration of `execution::transform`.

 * Otherwise constructs a callback, `c` over an implementation-defined synchronization primitive and passes that callback to `execution::submit(S, c)`.
   When some `output_callback` has been passed to `submit` on the returned `sender_to`.

   * If `value(c, Ts... ts)` is called, calls `std::invoke(F, ts...)` and passes the result `v` to `execution::value(output_callback, v)`.
   * If `F` throws, catches the exception and passes it to `execution::error(output_callback, e)`.
   * If `error(c, e)` is called, passes `e` to `execution::error(output_callback, e)`.
   * If `done(c)` is called, calls `execution::done(output_callback)`.


## bulk_transform
### Summary
`bulk_execute` is a side-effecting operation across an iteration space.
`bulk_transform` is a very similar operation that operates elementwise over an input range and returns the result as an output range.

### Wording
The name `execution::bulk_transform` denotes a customization point object.
The expression `execution::bulk_transform(S, F)` for some subexpressions S and F is expression-equivalent to:

 * S.bulk_transform(F), if that expression is valid.
 * Otherwise, `bulk_transform(S, F)`, if that expression is valid with overload resolution performed in a context that includes the declaration
 ```
         template<class S, class F>
           void bulk_transform(S, F) = delete;
 ```
   and that does not include a declaration of `execution::bulk_transform`.

 * Otherwise constructs a callback, `c` over an implementation-defined synchronization primitive and passes that callback to `execution::submit(S, c)`.

   * If `S::value_type` does not model the concept `Range<T>` for some `T` the expression ill-formed.   
   * If `value` is called on `c` with some parameter `input` applies the equivalent of `out = std::ranges::transform_view(input, F)` and passes the result `output` to `execution::value(output_callback, v)`.
   * If `error(c, e)` is called, passes `e` to `execution::error(output_callback, e)`.
   * If `done(c)` is called, calls `execution::done(output_callback)`.


## handle_error
### Summary
This is the only algorithm that deals with an incoming signal on the error channel of the `sender_to`.
Others only deal with the `value` channel directly.
For full generality, the formulation we suggest here applies a function `f(e)` to the error `e`, and returns a `sender_to` that may output on any of its channels.
In that way we can solve and replace an error, cancel on error, or log and propagate the error, all within the same algorithm.

### Wording
The name `execution::handle_error` denotes a customization point object.
The expression `execution::handle_error(S, F)` for some subexpressions S and F is expression-equivalent to:

 * S.handle_error(F), if that expression is valid.
 * Otherwise, `handle_error(S, F)`, if that expression is valid with overload resolution performed in a context that includes the declaration
```
        template<class S, class F>
          void handle_error(S, F) = delete;
```
  and that does not include a declaration of `execution::handle_error`.

 * Otherwise constructs a callback, `c` over an implementation-defined synchronization primitive and passes that callback to `execution::submit(S, c)`.

   * If `value(c, v)` is called, passes `v` to `execution::value(output_callback, v)`.
   * If `error(c, e)` is called, passes `e` to `f`, resulting in a `sender_to` `s2` and passes `output_callback` to `submit(s2, output_callback)`.
   * If `done(c)` is called, calls `execution::done(output_callback)`.

# Customization

# Chaining example with customization
TODO: Simple chain through just, on, transform, bulk_transform to sync_wait.
Point out that customization means that there is no submit() call in the middle.



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
---
