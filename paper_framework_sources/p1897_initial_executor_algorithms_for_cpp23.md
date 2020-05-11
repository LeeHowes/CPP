---
title: "Towards C++23 executors: A proposal for an initial set of algorithms"
document: P1897R3
date: 2020-04-30
audience: SG1
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---

# Changelog
## Differences between R2 and R3
 * Rename `just_via` to `just_on`.
 * Rename `via` to `on`.
 * Add `share`.
 * Add note on the feedback about `indexed_for` in Prague. Removed `indexed_for` from the paper of initial algorithms.
 * Add `let`.
 * Tweaked `handle_error` wording to be more similar to `let`, and renamed `let_error` for naming consistency.
 * Renamed `let` to `let_value` for naming consistency.
 * Updated to use P0443R13 as a baseline.
 * Improves the wording to be closer to mergable wording and less pseudowording.
 * Modified `sync_wait` to terminate on done rather than throwing.

## Differences between R1 and R2
 * Add `just_via` algorithm to allow type customization at the head of a work chain.
 * Add `when_all` to fill missing gap in the ability to join sender chains.
 * Add `indexed_for` based on feedback during the Belfast meeting to have a side-effecting algorithm.
 * Propose question on replacing `bulk_execute` with `indexed_for` for the Prague meeting.

## Differences between R0 and R1
 * Improve examples to be clearer, and fully expanded into function call form.
 * Add reference to range.adapter.
 * Remove `is_noexcept_sender`.
 * Remove `just_error`.
 * Clarified use of parameter packs of values and errors.
 * Removed confusing use of `on` in addition to `via` in the final example.

# Introduction
In [@P0443R13] we have included the fundamental principles described in [@P1660R0], and the fundamental requirement to customize algorithms.
In recent discussions we have converged to an understanding of the `submit` operation on a `sender` and its more fundamental primitives `connect` and `start` supporting general interoperation between algorithms, and algorithm customization giving us full flexibility to optimize, to offload and to avoid synchronization in chains of mutually compatible algorithm customizations.

As a starting point, in [@P0443R13] we only include a `bulk_execute` algorithm, that satisfies the core requirement to provide scalar and bulk execution.
To make the C++23 solution completely practical, we should extend the set of algorithms, however.
This paper suggests an expanded initial set that enables early useful work chains.
This set is intended to act as a discussion focus for us to discuss one by one, and to analyze the finer constraints of the wording to make sure we do not over-constrain the design.

This paper does not attempt to propose the mapping of the C++20 parallel algorithms into an asynchronous environment.
Once we have basic primitives, we can describe default implementations of the parallel algorithms, as well as `std::async`, in terms of these.

In the long run we expect to have a much wider set of algorithms, potentially covering the full set in the current C++20 parallel algorithms.
The precise customization of these algorithms is open to discussion: they may be individually customized and individually defaulted, or they may be optionally individually customized but defaulted in a tree such that customizing one is known to accelerate dependencies.
It is open to discussion how we achieve this and that is an independent topic, beyond the scope of this paper.

## Summary
Starting with [@P0443R13] as a baseline we have the following customization points:

 * `connect(sender, receiver) -> operation_state`
 * `start(operation_state) -> void`
 * `submit(sender, receiver) -> void`
 * `schedule(scheduler) -> sender`
 * `execute(executor, invocable) -> void`
 * `set_done`
 * `set_error`
 * `set_value`

and the following Concepts:

 * `scheduler`
 * `receiver`
 * `receiver_of`
 * `sender`
 * `sender_to`
 * `typed_sender`
 * `operation_state`
 * `executor`
 * `executor_of`

We propose immediately discussing the addition of the following algorithms:

 * `just(v...)`
   * returns a `sender` of the value(s) `v...`
 * `just_on(sch, v...)`
   * a variant of the above that embeds the `on` algorithm
 * `on(s, sch)`
   * returns a sender that propagates the value or error from `s` on `sch`'s execution context
 * `sync_wait(s)`
   * blocks and returns a `T` of the value type of the sender, throwing on error, and terminates on done.
 * `when_all(s...)`
   * returns a sender that completes when all Senders `s...` complete, propagating all values
 <!--* `indexed_for(s, policy, rng, f)`
   * returns a sender that applies `f` for each element of `rng` passing that element and the values from the incoming sender, completes when all `f`s complete propagating s's values onwards-->
 * `transform(s, f)`
   * returns a sender that applies `f` to the value passed by `s`, or propagates errors or cancellation
 <!--* `bulk_transform(s, f)`
   * returns a sender that applies `f` to each element in a range sent by `s`, or propagates errors or cancellation-->
 * `let_value(s, f)`
   * Creates an async scope where the value propagated by `s` is available for the duration of another async operation produced by `f`.
     Error and cancellation signals propagate unmodified.
 * `let_error(s, f)`
   * Creates an async scope where an error propagated by `s` is available for the duration of another async operation produced by `f`.
     Value and cancellation propagate unmodified.
 * `share(s)`
   * Eagerly submits `s` and returns a sender that may be used more than once, propagating the value by reference.

## Examples

#### Simple example
A very simple example of applying a function to a propagated value and waiting for it.

```cpp
auto  just_sender =       // sender_to<int>
  just(3);

auto transform_sender =  // sender_to<float>
  transform(
    std::move(just_sender),
    [](int a){return a+0.5f;});

int result =              // value: 3.5
  sync_wait(std::move(transform_sender));
```

In this very simple example we:

 * start a chain with the value three
 * apply a function to the incoming value, adding 0.5 and returning a sender of a float.
 * block for the resulting value and assign the `float` value `3.5` to `result`.

Using `operator|` as in ranges to remove the need to pass arguments around, we can represent this as:
```cpp
float f = sync_wait(
  just(3) | transform([](int a){return a+0.5f;}));
```

<!--
#### Using indexed_for
We propose that indexed_for be the cleaned up version of bulk_execute, this shows how it fits into a work chain, with a parameter pack of inputs

```cpp
auto  just_sender =       // sender_to<int>
  just(std::vector<int>{3, 4, 5}, 10);

auto indexed_for_sender =  // sender_to<float>
  indexed_for(
    std::move(just_sender),
    std::execution::par,
    ranges::iota_view{3},
    [](size_t idx, std::vector<int>& vec, const int& i){
      vec[idx] = vec[idx] + i;
    });

auto transform_sender = transform(
  std::move(indexed_for_sender), [](vector<int> vec, int /*i*/){return vec;});

vector<int> result =       // value: {13, 14, 15}
  sync_wait(std::move(transform_sender));
```

In this less simple example we:

 * start a chain with a vector of three ints and an int
 * apply a function for each element in an index space, that receives the vector and int by reference and modifies the vector
 * specifies the most relaxed execution policy on which we guarantee the invocable and range access function to be safe to call
 * applies a transform to filter out the int result, demonstrating how indexed_for does not change the structure of the data
 * block for the resulting value and assign vector {13, 14, 15} to `result`

Using `operator|` as in ranges to remove the need to pass arguments around, we can represent this as:
```cpp
vector<int> result_vec = sync_wait(
  just(std::vector<int>{3, 4, 5}, 10) |
  indexed_for(
    std::execution::par,
    ranges::iota_view{3},
    [](size_t idx, vector<int>&vec, const int& i){vec[idx] = vec[idx] + i;}) |
  transform([](vector<int> vec, int /*i*/){return vec;}));
```
-->

#### Using when_all
when_all joins a list of incoming senders, propagating their values.

```cpp
auto  just_sender =       // sender_to<int>
  just(std::vector<int>{3, 4, 5}, 10);
auto  just_float_sender =       // sender_to<int>
  just(20.0f);

auto when_all_sender = when_all(
  std::move(just_sender), std::move(just_float_sender));

auto transform_sender(
  std::move(when_all_sender),
  [](std::vector<int> vec, int /*i*/, float /*f*/) {
    return vec;
  })

vector<int> result =       // value: {3, 4, 5}
  sync_wait(std::move(transform_sender));
```

This demonstrates simple joining of senders:

 * start a chain with a pack of a vector and an int
 * start a second chain with a float
 * join the two to produce a pack of a vector, an int and a float
 * applies a transform to filter out the vector result
 * block for the resulting value and assign vector {3, 4, 5} to `result`

Using `operator|` as in ranges to remove the need to pass arguments around, we can represent this as:
```cpp
vector<int> result_vec = sync_wait(
  when_all(just(std::vector<int>{3, 4, 5}, 10), just(20.0f)) |
  transform([](vector<int> vec, int /*i*/, float /*f*/){return vec;}));
```

#### With exception
A simple example showing how an exception that leaks out of a transform may propagate and be thrown from sync_wait.

```cpp
int result = 0;
try {
  auto just_sender = just(3);
  auto via_sender = via(std::move(just_sender), scheduler1);
  auto transform_sender = transform(
    std::move(via_sender),
    [](int a){throw 2;});
  auto skipped_transform_sender = transform(
    std::move(transform_sender).
    [](){return 3;});

  result = sync_wait(std::move(skipped_transform_sender));
} catch(int a) {
 result = a;                                     // Assign 2 to result
}
```

In this example we:

 * start a chain with an int value `3`
 * switch the context to one owned by scheduler1
 * apply a transformation to the value `3`, but this transform throws an exception rather than returning a transformed value
 * skip the final transform because there is an error propagating
 * block for the resulting value, seeing an exception thrown instead of a value returned
 * handle the exception

As before, using `operator|` as in ranges to remove the need to pass arguments around, we can represent this more cleanly:

```cpp
int result = 0;
try {
 result = sync_wait(
    just(3) |
    via(scheduler1) |
    transform([](int a){throw 2;}) |
    transform([](){return 3;}));
} catch(int a) {
 result = a;                                     // Assign 2 to result
}
```

#### Handle an exception
Very similar to the above, we can handle an error mid-stream

```cpp
auto just_sender = just(3);
auto via_sender = via(std::move(just_sender), scheduler1);
auto transform_sender = transform(
  std::move(via_sender),
  [](int a){throw 2;});
auto skipped_transform_sender = transform(
  std::move(transform_sender).
  [](){return 3;});
auto error_handling_sender = let_error(
  std::move(skipped_transform_sender),
  [](exception_ptr e){return just(5);});

auto result = sync_wait(std::move(error_handling_sender));
```

In this example we:

 * start a chain with an int value `3`
 * switch the context to one owned by scheduler1
 * apply a transformation to the value `3`, but this transform throws an exception rather than returning a transformed value
 * skip the final transform because there is an error propagating
 * handle the error channel, applying an operation to an `exception_ptr` pointing to the value `2`
 * in handling the error we return a sender that propagates the value `5`, thus recovering from the error
 * block for the resulting value, assigning `5` to `result`


 As before, using `operator|` as in ranges to remove the need to pass arguments around, we can represent this more cleanly:
```cpp
auto s = ;
int result = sync_wait(
  just(3) |
  via(scheduler1) |
  transform([](float a){throw 2;}) |
  transform([](){return 3;}) |
  let_error([](auto e){
   return just(5);}));
```


# Impact on the standard library

## Sender adapter objects
Taking inspiration from [range adaptors](http://eel.is/c++draft/range.adaptor.object) define sender adapters.

Wording to be based on [range.adaptors] with the basic requirement that:

 * `operator|` be overloaded for the purpose of creating pipelines over senders
 * That the following are equivalent expressions:
   * `algorithm(sender, args...)`
   * `algorithm(args...)(sender)`
   * `sender | algorithm(args...)`
 * that `algorithms(args...)` is a *sender adaptor closure object*
 * TBD where sender adapters are declared


Details below are in loosely approximated wording and should be made consistent with [@P0443R11] and the standard itself when finalized.
We choose this set of algorithms as a basic set to allow a range of realistic, though still limited, compositions to be written against executors.



<!-- ## execution::is_noexcept_sender
### Summary
Queries whether the passed sender will ever propagate an error when treated as an r-value to `submit`.

### Signature

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

If `execution::is_noexcept_sender(s)` returns true for a `sender` `s` then it is guaranteed that `s` will not call `error` on any `callback` `c` passed to `submit(s, c)`. -->


## execution::just

### Overview
`just` creates a `sender` that propagates a value inline to a submitted receiver.

Signature:
```cpp
template <typename T>
concept moveable-value = // exposition only
  move_constructible<remove_cvref_t<T>> &&
  constructible_from<remove_cvref_t<T>, T>;

template <movable-value... Ts>
see-below just(Ts&&... ts) noexcept(see-below);
```

*[ Example:*
```cpp
int r = sync_wait(just(3));
// r==3
```
*- end example]*

### Wording
The expression `execution::just(t...)` returns a sender, `s` wrapping the values `t...`.


<!-- * If `t...` are nothrow movable then `execution::is_noexcept_sender(s)` shall be constexpr and return true.-->
 * When `execution::connect(s, r)` is called resulting in `operation_state` `o` containing `rCopy` with type `remove_cvref_t<decltype(r)>` and initialized with `r` and followed by `execution::start(o)` for some `r`, will call `execution::set_value(r, std::move(t)...)`, inline with the caller.
 * If moving of `t` throws, then will catch the exception and call `execution::set_error(r, e)` with the caught `exception_ptr`.


## execution::just_on

### Overview
`just_on` creates a `sender` that propagates a value to a submitted receiver on the execution context of a passed `scheduler`.
Semantically equivalent to `on(just(t), s)` if `just_on` is not customized on `s`.
Providing `just_on` offers an opportunity to directly customise the algorithm to control allocation of the value `t` at the head of a custom pipeline.

Signature:
```cpp
template <execution::scheduler Sch, movable-value... Ts>
see-below just_on(Sch sch, Ts&&... ts) noexcept(see-below);
```

*[ Example:*
```cpp
MyScheduler s;
int r = sync_wait(just_on(s, 3));
// r==3
```
*- end example]*

### Wording
The name `execution::just_on` denotes a customization point object.
For some subexpressions `sch` and `ts...` let `Sch` be a type such that `decltype((sch))` is `Sch` and let `Ts...` be a pack of types such that `decltype((ts))...` is `Ts...`.
 The expression `execution::just_on(s, ts...)` is expression-equivalent to:

  * `sch.just_on(ts...)` if that expression is valid and if `sch` satisfies `scheduler`.
  * Otherwise, `just_on(sch, ts...)`, if that expression is valid, if `sch` satisfies `scheduler` with overload resolution performed in a context that includes the declaration
 ```
    void just_on() = delete;
 ```
   and that does not include a declaration of `execution::just_on`.
 * Otherwise returns the result of the expression: `execution::on(execution::just(ts...), sch)`

<!--
## execution::just_error

### Summary
Returns a sender that propagates the passed error inline when `submit` is called.
This is useful for starting a chain of work.

### Wording
The expression `execution::just_error(e)` returns a sender, `s` wrapping the error `e`.

 * If `t` is nothrow movable then `execution::is_noexcept_sender(s)` shall be constexpr and return true.
 * When `execution::submit(s, r)` is called for some `r`, and r-value `s` will call `execution::set_error(r, std::move(t))`, inline with the caller.
 * When `execution::submit(s, r)` is called for some `r`, and l-value `s` will call `execution::set_error(r, t)`, inline with the caller.
 * If moving of `e` throws, then will catch the exception and call `execution::set_error(r, e)` with the caught `exception_ptr`. -->


## execution::sync_wait

### Overview
Blocks the calling thread to wait for the passed sender to complete.
Returns `T` when passed a `typed_sender` that sends a `T` on the value channel, where `T` may be `void`, throws if an exception is propagated and calls `std::terminate` on propagation of the `set_done()` signal.

```cpp
template <execution::typed_sender S>
see-below sync_wait(S&& s);
template <class ValueType, execution::sender S>
ValueType sync_wait_r(S&& s);
```

*[ Example:*
```cpp
int r = sync_wait(just(3));
float r = sync_wait<float>(just(3.5f));
// r==3
```
*- end example]*


### Wording

The name `execution::sync_wait` denotes a customization point object.
For some subexpression `s` let `S` be a type such that `decltype((s))` is `S`.
The expression `execution::sync_wait(s)` is expression-equivalent to:

 * `s.sync_wait()` if that expression is valid and if `S` satisfies `sender`.
 * Otherwise, `sync_wait(s)`, if that expression is valid, if `S` satisfies `sender`, with overload resolution performed in a context that includes the declaration
 ```
      void sync_wait() = delete;
 ```
   and that does not include a declaration of `execution::sync_wait`.

 * Otherwise constructs a `receiver`, `r` over an implementation-defined synchronization primitive and passes `r` to `execution::connect(s, r)` returning some `operation_state` `os`.
   Waits on the synchronization primitive to block on completion of `s`.

   * If the operation completes by calling `set_value(r, t)` then `sync_wait()` will return a value, `x`, of type `remove_cvref_t<decltype(t)>`.
   * If the operation completes by calling `set_value(r)` then `sync_wait()` will return `void`.
   * If the operation completes by calling `set_error(r, e)` then `sync_wait()` calls `std::rethrow_exception(e)` if `decltype(e)` is `std::exception_ptr` or `throw e;` otherwise.
   * If the operation completes by calling `set_done(r)` then `sync_wait()` will call `std::terminate`.


<!--
If `execution::is_noexcept_sender(S)` returns true at compile-time, and the return type `T` is nothrow movable, then `sync_wait` is noexcept.
Note that `sync_wait` requires `S` to propagate a single value type.
-->

## execution::on

### Overview
Takes a `sender` and a `scheduler` and ensures that the `sender` operation is `connect`ed and `start`ed on the execution context associated with the `scheduler`, giving the programmer control over where the work encapsulated by `sender` is started.

```cpp
template <execution::sender S, execution::scheduler Sch>
see-below on(S s, Sch sch);
```

*[ Example:*
```cpp
auto r = sync_wait(just(3) | on(my_scheduler{}) | transform([](int v){return v+1;}));
// r==3
```
*- end example]*


### Wording
The name `execution::on` denotes a customization point object.
For some subexpressions `s` and `sch`, let `S` be a type such that `decltype((s))` is `S` and `Sch` be a type such that `decltype((sch))` is `Sch`
The expression `execution::on(s, sch)` is expression-equivalent to:

 * `s.on(sch)` if that expression is valid, if `S` satisfies `sender`
 * Otherwise, `on(s, sch)` if that expression is valid, and if `S` satisfies `sender` and if `Sch` satisfies `scheduler`, with overload resolution performed in a context that includes the declaration
 ```cpp
      void on() = delete;
 ```
   and that does not include a declaration of `execution::on`.

 * Otherwise:

   * Constructs a `sender` `s2` such that when `connect` is called with some `receiver` `output_receiver` as `execution::connect(s2, output_receiver)` resulting in an `operation_state` `os` which is stored as a subobject of the parent `operation_state`:
     * Constructs a receiver, `r` and passes `r` to `execution::connect(s, r)` resulting in an operation state `ros`, which is stored as a subobject of `os` such that:
      * When `set_value`, `set_error` or `set_done` is called on `r`, the parameter is copied and stored as a subobject of a receiver `r2` and `execution::connect(execution::schedule(sch), std::move(r2))` results in an `operation_state` `os2` which is stored as a subobject of `os` such that:
        * When `set_value` is called on `r2`, `os2`'s destructor will be called, the stored value is forwarded to `output_receiver` on the appropriate choice of `set_value`, `set_error` or `set_done` to match the operation performed on `r`.
        * When `set_error` or `set_done` is called on `r2` the parameters propagate to `output_receiver`.
      * If `connect` throws, the resulting exception is forwarded to `execution::set_error(output_receiver)`.
      * The destructor of `ros` is called.
     * If `connect` throws, the resulting exception is forwarded to `execution::set_error(output_receiver)`.
     * Calls `execution::start(os2)`.
   * When `execution::start` is called on `os`, call `execution::start(ros)`.
 * Otherwise, `execution::on(s, sch)` is ill-formed.


## execution::when_all

### Overview
`when_all` combines a set of *non-void* `senders`, returning a `sender` that, on success, completes with the combined values of all incoming `sender`s.
To make usage simpler, `when_all` is restricted to `typed_sender`s that each send only a single possible value type.

Signature:
```cpp
template <execution::typed_sender Ss...>
see-below when_all(Ss... ss);
```

*[ Example:*
```cpp
auto r =
  sync_wait(
    transform(
      when_all(just(3) | just(1.2f)),
      [](int a, float b){return a + b;}));
// r==4.2
```
*- end example]*

### Wording
The name `execution::when_all` denotes a customization point object.
For some subexpression `ss...`, let `Ss...` be a list of types such that `decltype((ss))...` is `Ss...`.
The expression `execution::when_all(ss...)` is expression-equivalent to:

 * `when_all(ss...)` if that expression is valid, and if each `Si` in `Ss` satisfies `typed_sender`, `sender_traits<Si>::value_types<T>` for some type `T` with overload resolution performed in a context that includes the declaration
   ```cpp
      void when_all() = delete;
   ```
   and that does not include a declaration of `execution::when_all`.
 * Otherwise, returns a `sender`, `s`, that, when `connect(s, output_receiver)` is called on the returned `sender`, for some `output_receiver`, constructs a `receiver` `ri` for each passed `sender`, `si` and calls `connect(si, ri)`, returning `operation_state` object `osi`. The `operation_state`s, `osi`, are stored as subobjects within the operation-state object returned from `connect(s, output_receiver)` such that:

    * if `set_value(ti)` is called on all `ri`, for some single value `ti` for each `ri` will concatenate the list of values and call `set_value(output_receiver, t0..., t1..., tn...)`.
    * if `set_done()` is called on any `ri`, will call `set_done(output_receiver)`, discarding other results.
    * if `set_error(e)` is called on any `ri` will call `set_error(output_receiver, e)` for some `e`, discarding other results.

  When `start` is called on the returned `sender`'s `operation_state`, call `execution::start(osi)` for each `operation_state` `osi`.

**Notes:**
 * Efficient execution here under exceptional conditions requires cancellation support. This will be detailed separately.
 * Explicitly transitioning onto a downstream execution context maintains correctness in the general case.


<!--
## execution::indexed_for

### Overview
`indexed_for` is a sender adapter that takes a `sender`, execution policy, a range and an invocable and returns a `sender` that propagates the input values but runs the invocable once for each element of the range, passing the input by non-const reference.

Signature:
```cpp
S<T...> indexed_for(
  S<T...>,
  execution_policy,
  range<Idx>,
  invocable<void(Idx, T&...));
```

where `S<T...>` represents implementation-defined sender types that send a value of type list `T...` in their value channels.
Note that in the general case there may be many types `T...` for a given `sender`, in which case the invocable may have to represent an overload set.

*[ Example:*
```cpp
int r = sync_wait(
  just(3) |
  indexed_for(
    std::execution::par,
    ranges::iota_view{6},
    [](int idx, int& v){v = v + idx;}));
// r==9
```

### Wording

The name `execution::indexed_for` denotes a customization point object.
The expression `execution::indexed_for(S, P, R, F)` for some subexpressions `S`, `P`, `R` and `F` is expression-equivalent to:

 * If `P` does not satisfy `std::is_execution_policy_v<P>`, then the expression is invalid.
 * If `R` does not satisfy either `range` then the expression is invalid.
 * If `P` is `std::execution::sequenced_policy` then `range` must satisfy `input_range` otherwise `range` must satisfy `random_access_range`.
 * If `F` does not satisfy `MoveConstructible` then the expression is invalid.
 * S.indexed_for(P, R, F), if that expression is valid.
 * Otherwise, `indexed_for(S, R, P, F)`, if that expression is valid with overload resolution performed in a context that includes the declaration
 ```
         template<class S, class R, class P, class F>
           void indexed_for(S, R, P, F) = delete;
 ```
   and that does not include a declaration of `execution::indexed_for`.

 * Otherwise constructs a receiver, `r` and passes that receiver to `execution::submit(S, r)`.

   * If `set_value` is called on `r` with some parameter pack `t...` then calls `F(idx, t...)` for each element `idx` in `R`.
     Once all complete calls `execution::set_value(output_receiver, v)`.

     * If any call to `set_value` throws an exception, then call `set_error(r, e)` with some exception from the set.

   * If `set_error(r, e)` is called, passes `e` to `execution::set_error(output_receiver, e)`.
   * If `set_done(r)` is called, calls `execution::set_done(output_receiver)`.

**Notes:**
 * If `P` is not `execution::seq` and `R` satisfies `random_access_range` then `indexed_for` may run the instances of `F` concurrently.
 * `P` represents a guarantee on the most relaxed execution policy `F` and the element access function of range `R`  are safe to run under, and hence the most parallel fashion in which the underlying `scheduler` may map instances of `F` to execution agents.
-->

## execution::transform


### Overview
`transform` is a sender adapter that takes a `sender` and an invocable and returns a `sender` that propagates the value resulting from calling the invocable on the value passed by the preceding `sender`.

Signature:
```cpp
template <execution::sender S, std::invocable F>
see-below transform(S s, F f);
```

*[ Example:*
```cpp
std::optional<int> r = sync_wait(just(3) | transform([](int v){return v+1;}));
// r==4
```
*- end example]*

### Wording

The name `execution::transform` denotes a customization point object.
For some subexpressions `s` and `f`, let `S` be a type such that `decltype((s))` is `S` and `decltype((f))` is `F`.
The expression `execution::transform(s, f)` is expression-equivalent to:

 * `s.transform(f)` if that expression is valid, `s` satisfies `sender`.
 * Otherwise, `transform(S, F)`, if that expression is valid, `s` satisfies `sender` with overload resolution performed in a context that includes the declaration
 ```
    void transform() = delete;
 ```
   and that does not include a declaration of `execution::transform`.

 * Otherwise constructs a `receiver`, `r` and passes that receiver to `execution::connect(s, r)` to return an `operation_state` `os` such that:

   When some `output_receiver` has been passed to `connect` on the returned `sender` to return some `operation_state` `os2`:
   * If `set_value(r, Ts... ts)` is called, calls `std::invoke(F, ts...)` and passes the result `v` to `execution::set_value(output_receiver, v)`.
   * If `f` throws, catches the exception and passes it to `execution::set_error(output_receiver, e)`.
   * If `set_error(r, e)` is called, passes `e` to `execution::set_error(output_receiver, e)`.
   * If `set_done(r)` is called, calls `execution::set_done(output_receiver)`.

  When `start()` is called on `os2` calls `execution::start(os)`.

 * Otherwise the expression `execution::transform(s, f)` is ill-formed.

<!--
If `execution::is_noexcept_sender(S)` returns true at compile-time, and `F(S1::value_types)` is marked `noexcept` and all entries in `S1::value_types` are nothrow movable, `execution::is_noexcept_sender(transform(S1, F))` should return `true` at compile time.
-->

<!--

## execution::bulk_transform

### Overview
`bulk_transform` is a sender adapter that takes a `sender` of a `range` of values and an invocable and returns a `sender` that executes the invocable for each element of the input range, and propagates the range of returned values.

Signature:
```cpp
S<range<T2>> bulk_transform(S<range<T>>, invocable<T2(T));
```

where `S<range<T>>` and `S<T2>` are implementation-defined types that is represent senders that send a value of type list `T` or `T2` respectively in their value channels.
Note that in the general case there may be many types `T` for a given `sender`, in which case the invocable may have to represent an overload set.

*[ Example:*
```cpp
std::vector<int> r = sync_wait(just(std::vector<int>{3, 4, 5}) | bulk_transform([](int v){return v+1;}));
// r=={4, 5, 6}
```
*- end example]*

Note: it is TBD how precisely we should represent the intermediate data types here. Intermediate vectors would require allocator support. Purely lazy ranges may be inadequate.

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

 * Otherwise constructs a receiver, `r`  and passes that receiver to `execution::submit(S, r)`.

   * If `S::value_type` does not model the concept `Range<T>` for some `T` the expression ill-formed.
   * If `set_value` is called on `r` with some parameter `input` applies the equivalent of `out = std::ranges::transform_view(input, F)` and passes the result `output` to `execution::set_value(output_receiver, v)`.
   * If `set_error(r, e)` is called, passes `e` to `execution::set_error(output_receiver, e)`.
   * If `set_done(r)` is called, calls `execution::set_done(output_receiver)`.
-->

## execution::let_error

### Overview
`let_error` is a sender adapter that takes a `sender` and an invocable and returns a `sender` that, on error propagation, keeps the error result of the incoming sender alive for the duration of the `sender` returned by the invocable and makes that value available to the invocable.


Signature:
```cpp
template <execution::sender S, std::invocable F>
see-below let_error(S s, F f);
```

*[ Example:*
```cpp
std::optional<float> r = sync_wait(
  just(3) |
  transform([](int v){throw 2.0f;}) |
  let_error([](float e){return just(e+1);}));
// r==3.0f
```

### Wording
The name `execution::let_error` denotes a customization point object.
For some subexpressions `s` and `f`, let `S` be a type such that `decltype((s))` is `S` and `decltype((f))` is `F`.
The expression `execution::let_error(s, f)` is expression-equivalent to:

 * s.let_error(f), if that expression is valid, if `s` satisfies `sender`.
 * Otherwise, `let_error(s, f)`, if that expression is valid,, if `s` satisfies `sender` with overload resolution performed in a context that includes the declaration
```
    void let_error() = delete;
```
  and that does not include a declaration of `execution::let_error`.
 * Otherwise returns a sender that when `connect()` is called on it constructs a `receiver`, `r`, and passes that receiver to `execution::connect(S, r)` returning an `operation_state` `os` such that
   When some `output_receiver` has been passed to `connect` on the returned `sender` returning some `operation_state` `os2`:

   * If `set_value(r, ts...)` is called, for some potentially empty list of values `ts...`, passes `ts...` to `set_value(output_receiver, ts...)`.
   * If `set_error(r, e)` is called, calls `std::invoke(f, e)` to return some `invoke_result`, and calls `execution::start(execution::connect(invoke_result, output_receiver))`.
   * If `f` throws, catches the exception and passes it to `set_error(output_receiver, e)`.
   * If `set_done(r)` is called, calls `set_done(output_receiver)`.
 * Otherwise, returns a `sender`, `s2`, that, when `connect(s, output_receiver)` is called on `s2`, for some `output_receiver`, returning an `operation_state` `os2`, constructs a `receiver` `r` and passes that receiver to `connect(s, r)`, returning `operation_state` object `os` and stores `os` as a subobject of `os2`:

   * If `set_value(r, ts...)` is called, passes `ts...` to `set_valus(output_receiver, ts...)`.
   * If `set_error(r, e)` is called:
     * copies `e` into `os2` as `e2`, calls `std::invoke(f, e2)` to return some `invoke_result`
     * calls `execution::connect(invoke_result, output_receiver)` resulting in some `operation_state` `os3`, stores `os3` as a subobject of `os2` and calls `execution::start(os3)`.
     * the destructor of `os2` must be sequenced after the completion of the operation represented by `invoke_result`.
     * If `f` or `connect()` throws, catches the exception as `e3` and passes it to `set_error(output_receiver, 3)`.
   * If `set_done(r)` is called, calls `set_done(output_receiver)`.

   When `start` is called on `os2`, call `execution::start(os)`.

 * Otherwise the expression `execution::let_error(s, f)` is ill-formed.


## execution::share

### Overview
`share` is a sender adapter that takes a `sender`, eagerly submits it and returns a `sender` that propagates the value by reference and can be used as an l-value.

Signature:
```cpp
template <execution::sender S>
see-below share(S s);
```

*[ Example:*
```cpp
auto s1 = just(3) | share();
auto s2 = s1 | transform([](const int& v){return v+1;}))
auto s3 = s1 | transform([](const int& v){return v+2;}))
std::optional<int> r = sync_wait(
  transform(
    when_all(s2, s3),
    [](int a, int b){return a+b;}));
// r==9
```

### Wording
The name `execution::share` denotes a customization point object.
For some subexpressions `s`, let `S` be a type such that `decltype((s))` is `S`.
The expression `execution::share(s, f)` is expression-equivalent to:

 * `s.share()` if that expression is valid and if `s` satisfies `sender`.
 * Otherwise, `share(s)`, if that expression is valid, if `s` satisfies `sender` with overload resolution performed in a context that includes the declaration
 ```
    void share() = delete;
 ```
   and that does not include a declaration of `execution::share`.

 * Otherwise constructs a receiver, `r` and passes that receiver to `execution::connect(s, r)` resulting in an `operation_state` `os`.
   Constructs some shared state, `shr` to store the completion result(s) of `s`.

   * If `set_value(r, ts)` is called stores `ts` in `shr`.
   * If `set_error(r, e)` is called, stores `e` in `shr`.
   * If `set_done(r)` is called stores the done result in `shr`.

   When some `output_receiver` has been passed to `connect` on the returned `sender`, resulting in an `operation_state` `os2` and one of the above has been called on `r`:
   * If `r` was satisfied with a call to `set_value`, call `set_value(output_receiver, ts...)`
   * If `r` was satisfied with a call to `set_error`, call `set_error(output_receiver, e)`.
   * If `r` was satisfied with a call to `set_done`, call `execution::set_done(output_receiver)`.

 * When `start` is called on `os2`, call `execution::start(os)`.

## execution::let_value

### Overview
`let_value` is a sender adapter that takes a `sender` and an invocable and returns a `sender` that keeps the completion result of the incoming sender alive for the duration of the algorithm returned by the invocable and makes that value available to the invocable.

Signature:
```cpp
template <execution::sender S, std::invocable F>
see-below let_value(S s, F f);
```

where `S<T...>` and `S<T2>` are implementation-defined types that is represent senders that send a value of type list `T...` or `T2` respectively in their value channels.
Note that in the general case there may be many types `T...` for a given `sender`, in which case the invocable may have to represent an overload set.

*[ Example:*
```cpp
std::optional<int> r = sync_wait(
  just(3) |
  let_value([](int& let_v){
    return just(4) | transform([&](int v){return let_v + v;})));
// r==7
```

### Wording
The name `execution::let_value` denotes a customization point object.
For some subexpressions `s` and `f`, let `S` be a type such that `decltype((s))` is `S` and `decltype((f))` is `F`.
The expression `execution::let_value(s, f)` is expression-equivalent to:

 * s.let_value(f), if that expression is valid, if `s` satisfies `sender` and `f` satisfies `invocable`.
 * Otherwise, `let_value(s, f)`, if that expression is valid,, if `s` satisfies `sender` and `f` satisfies `invocable` with overload resolution performed in a context that includes the declaration
```
    void let_value() = delete;
```
  and that does not include a declaration of `execution::let_value`.
 * Otherwise, returns a `sender`, `s2`, that, when `connect(s, output_receiver)` is called on `s2`, for some `output_receiver`, returning an `operation_state` `os2` which will be stored as a subobject of the parent `operation_state`, constructs a `receiver` `r` and passes that receiver to `connect(s, r)`, returning `operation_state` object `os` and stores `os` as a subobject of `os2`:

   * If `set_value(r, ts...)` is called:
     * copies `ts...` into `os2` as subobjects `t2s...`, calls `std::invoke(f, t2s...)` to return some `invoke_result`
     * calls `execution::connect(invoke_result, output_receiver)` resulting in some `operation_state` `os3`, stores `os3` as a subobject of `os2` and calls `execution::start(os3)`.
     * the destructor of `os2` must be sequenced after the completion of the operation represented by `invoke_result`.
     * If `f` or `connect()` throws, catches the exception and passes it to `set_error(output_receiver, e)`.
   * If `set_error(r, e)` is called, passes `e` to `set_error(output_receiver, e)`.
   * If `set_done(r)` is called, calls `set_done(output_receiver)`.

   When `start` is called on `os2`, call `execution::start(os)`.

 * Otherwise the expression `execution::let_value(s, f)` is ill-formed.



# Customization and example
Each of these algorithms, apart from `just`, is customizable on one or more `sender` implementations.
This allows full optimization.
For example, in the following simple work chain:

```
auto s = just(3) |                                        // s1
         on(scheduler1) |                                 // s2
         transform([](int a){return a+1;}) |              // s3
         transform([](int a){return a*2;}) |              // s4
         on(scheduler2) |                                 // s5
         let_error([](auto e){return just(3);});       // s6
std::optional<int> r = sync_wait(s);
```

The result of `s1` might be a `just_sender<int>` implemented by the standard library vendor.

`on(just_sender<int>, scheduler1)` has no customization defined, and this expression returns an `scheduler1_on_sender<int>` that is a custom type from the author of `scheduler1`, it will call `submit` on the result of `s1`.

`s3` calls `transform(scheduler1_on_sender<int>, [](int a){return a+1;})` for which the author of `scheduler1` may have written a customization.
The `scheduler1_on_sender` has stashed the value somewhere and build some work queue in the background.
We do not see `submit` called at this point, it uses a behind-the-scenes implementation to schedule the work on the work queue.
An `scheduler1_transform_sender<int>` is returned.

`s4` implements a very similar customization, and again does not call `submit`.
There need be no synchronization in this chain.

At `s5`, however, the implementor of `scheduler2` does not know about the implementation of `scheduler1`.
At this point it will call `submit` on the incoming `scheduler1_transform_sender`, forcing `scheduler1`'s sender to implement the necessary synchronization to map back from the behind-the-scenes optimal queue to something interoperable with another vendor's implementation.

`let_error` at `s6` will be generic in terms of `submit` and not do anything special, this uses the default implementation in terms of `submit`.
`sync_wait` similarly constructs a `condition_variable` and a temporary `int`, submits a `receiver` to `s` and waits on the `condition_variable`, blocking the calling thread.

`r` is of course the value 8 at this point assuming that neither scheduler triggered an error.
If there were to be a scheduling error, then that error would propagate to `let_error` and `r` would subsequently have the value `3`.

# Potential future changes

## when_all's context
Based on experience in Facebook's codebase, we believe that `when_all` should return a sender that requires an executor-provider and uses forward progress delegation as discussed in [@P1898R1].
The returned sender should complete on the delegated context.
This removes the ambiguity about which context it completes on.

## when_all for void types and mixed success
We should add a when_all variant that returns tuples and variants in its result, or some similar mechanism for to allow parameter packs, including empty packs in the form of void-senders, and mixed success/error to propagate.

## when_all and share both require cancellation and async cleanup to be fully flexible
Under error circumstances, `when_all` should cancel the other incoming work. This will be described separately.

`share` similarly needs to be updated to describe how it behaves in the presence of one downstream task being cancelled, and precisely when and where the shared state is destroyed.

# Proposed question for the Prague 2020 meeting
## Replace `bulk_execute` in P0443 with `indexed_for` as described above.

`indexed_for` as described above should replace bulk_execute during the merge of [@P0443R11] into C++23.
Suggest fine-tuning this wording and forwarding to LEWG.

The changes this leads to:

 * Renames the algorithm to fit in with a set of user-level algorithms rather than making it distinct and internal-only.
   We found it hard to define a difference between `bulk_execute` and `indexed_for` and so suggest we not try, instead we rename it.
 * Propagating the data from the incoming sender into the invocable by reference and out the other end.
   This allows the algorithm to be a side-effecting view on data, but because that data is in-band in the data stream it is safe from a lifetime point of view.
   More so that it would be if the data had to be captured by reference.
 * Replaces the max value with a range for the index space. This allows for more flexibility.
 * Adds the execution policy back in, defining the forward progress guarantee both the invocable and range accessor make.
   This is preferred because the policy is a statement the programmer makes about the capabilities of the invocable.
   An indexed_for that requires seq, and an executor that cannot execute seq can fail at this point.
   An invocable that requires seq run on an executor that cannot run seq algorithms would be invisible at the point of chaining the algorithm.
 * Does not add any sort of factory as [@P1993R0].
   These are not necessary if we carry the data in the stream.
   Data can be allocated to a device using, for example, a `device_vector`.
   This maintains full flexibility - we can add custom data management algorithms independently and keep `indexed_for` focused on its primary use cause: the asynchronous for loop itself.
 * Relaxes the CopyConstructible restriction that [@P0443R11], but also the standard algorithms in C++20 place.
   Wide discussion suggests that this restriction may not be necessary, and it could certainly be harmful.
   In an asynchronous world we cannot rely on scoped `reference_wrapper` semantics, and the cost of injecting `shared_ptr` would be high.
   If an implementation needs to copy, then that implementation should implement a wrapper that is custom for the algorthmic structure it is using.
   For example, a forking tree of threads may allocate once on the first thread by move and reference back to it, knowing the lifetime is safe.

## Result of discussion and Prague SG1 vote on P1897R2
Poll: We should add a sender argument and sender result to bulk execution functions (providing an opportunity to build shared state, established dependencies in/out)

SA: 17; F: 7; N: 0; A: 0; SA: 0

Consensus.


Poll: We should replace bulk_execute with indexed_for

SA: 4; F: 11; N: 3; A: 7; SA: 1

No consensus for change. Discussed in the room that indexed_for (and other algorithms by inference) should be build on top of bulk_execute.


The bulk_execute primitive should take an execution policy to constrain the invocable.

SA: 5; F: 7; N: 8; A: 3; SA: 1


R3 of this paper removes `indexed_for`. If `bulk_execute` is to remain, there is less urgent need to add `indexed_for`. Instead R3 focuses on the core set of algorithms. Something like `indexed_for`, or `for_each` will be in the async update of the parallel algorithms.



---
references:
  - id: P0443R11
    citation-label: P0443R11
    title: "A Unified Executors Proposal for C++"
    issued:
      year: 2019
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0443r11.html
  - id: P0443R13
    citation-label: P0443R13
    title: "A Unified Executors Proposal for C++"
    issued:
      year: 2020
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0443r13.html
  - id: P1660R0
    citation-label: P1660R0
    title: "A Compromise Executor Design Sketch"
    issued:
      year: 2019
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1660r0.pdf
  - id: P1898R1
    citation-label: P1898R1
    title: "Forward progress delegation for executors"
    issued:
      year: 2020
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1898r1.html
  - id: P1993R0
    citation-label: P1993R0
    title: "Restore factories to bulk_execute"
    issued:
      year: 2019
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1993r0.pdf
---
