# A firm specification of bulk execution

## Summary
Bulk execution as defined in P0443 is a little vague:
* It is hard to know for sure when different parts of it run.
* People have asked whether we have shared memory in the CUDA sense.
  * It isn't entirely clear if that is the purpose of the shared factory or if that storage is globally shared.
  * The result factory also results in something that is shared, but also ends up as the result.
* When and how does the result move into the output future?
* Where is the result stored?
* How do we know what to return from the shape factory?
  * It is not parameterised with the input, so we cannot depend on the input data in any clean way.
  * It is algorithm-specific, rather than executor-specific, and we have no concrete executor queries about hardware concurrency or vector size to choose usefully.
* The shape type is under-specified and it is not clear what would be a valid type.

## The request
We propose that we improve the definition of bulk:
* Define bulk execution in terms of a clearly defined algorithm.
* Make the sequence of operations and memory orderings between them clear.
* Define all of this as-if, to allow optimisations.

In addition we propose modifying the interface slightly to improve the flexibility and adaptation to input data.

Take an algorithm like:
```
v = some vector of bools
v2 = filter(v, ==true)
v3 = transform(v2, [](bool val){return 1;});
```

To implement transform using `bulk_then_execute` we would need to serialize these operations in order to know how big to make the output array, and potentially also to parameterise the shape.
To implement filter using `bulk_then_execute` is harder, but if order does not matter we could use an atomic counter to track the index of the last element and a concurrency-safe vector as the shared output structure.
One downside of this is that that concurrency-safe vector is not an implementation-detail, but would have to be exposed in the type of `v3` because the result type in `bulk_then_execute` is shared state and a result value.

## Proposal
To more concretely define the algorithm, and to allow more flexibility of use, we replace the current definition of `bulk_then_execute` as below, noting that this set of changes is defined only in terms of the change to P0443, and is independent of parallel discussions about redefining in terms of senders and receivers.

 * `x` denotes a (possibly const) executor object of type `X`,
 * `shape` denotes a shape object of type `Shape` such that `executor_shape_t<X>` is `Shape` and that `Shape` satisfies concept `RandomAccessRange<Shape>`
 * `fut` denotes a future object satisfying the `Future` requirements,
 * `val` denotes any object of type `V` provided to the execution by `fut` on nonexceptional completion of the task associated with `fut`,
 * `e` denotes any object provided to the execution by `fut` on exceptional completion of the task associated with `fut`
 * `shape_factory` denotes a `CopyConstructible` function object with zero arguments, or with one argument of type `const V&`, and whose result type is `Shape`,
 * `shared_factory` denotes a `CopyConstructible` function object one argument of type `const Shape&`, and whose result type is `S`,
 * `result_selector` denotes an optional `CopyConstructible` function object with one argument of type `S&&` and result type `void`,
 * `i` denotes a (possibly const) object whose type is `Shape::value_type`,
 * `shared` denotes an object whose type is `S`,
 * `NORMAL` denotes the expression `DECAY_COPY(std::forward<F>(f))(i, val, s)`,
 * `EXCEPTIONAL` denotes the expression `DECAY_COPY(std::forward<F>(f))(i, exception_arg, e, s)`,

For the expression:
```
output_fut x.bulk_then_execute(fut, func, shape_factory, shared_factory, result_selector)
```

With a return value type that satisfies the `Future` requirements of value type `R`.

The semantics are in two phases:
 * At some point ordered after the call to `bulk_then_execute` and before any call to `NORMAL` or `EXCEPTIONAL`:
   * If `shape_factory` takes no arguments then:
     * invoke `shape_factory()` to produce value `shape`.
     * invoke `shared_factory(shape)` to produce value `s`.
 * At some point after `val` is provided by completion of `fut`:
   * If `shape_factory` takes one argument then:
     * invoke `shape_factory(val)` to produce value `shape`.
     * invoke `shared_factory(shape)` to produce value `s`.
   * If `fut` is nonexceptionally ready and if `NORMAL` is a well-formed expression:
     * creates `std::distance(begin(shape), end(shape))` agents
     * invokes `NORMAL` at most once for each value in range `begin(shape)` to `end(shape)` with the call to `DECAY_COPY` being evaluated in the thread that called `bulk_then_execute`.
   * If `fut` is exceptionally ready and if `EXCEPTIONAL` is a well-formed expression:
     * creates `std::distance(begin(shape), end(shape))` agents
     * invokes `EXCEPTIONAL` at most once for each value in range `begin(shape)` to `end(shape)` with the call to `DECAY_COPY` being evaluated in the thread that called `bulk_then_execute`.
   * If neither `NORMAL` nor `EXCEPTIONAL` is a well-formed expression, the invocation of `bulk_then_execute` shall be ill-formed
   * The invocation of `bulk_then_execute` synchronizes with (C++Std [intro.multithread]) the invocations of `f`.
 * If `result_selector` is provided:
   * Calls `result_selector(std::move(s), output_fut)` in some agent owned by executor `X`
 * Otherwise:
   * Implements `result_selector` as-if by `output_fut = std::move(s)`
 * All instances of `f` synchronize with the call to `result_selector`.

Note that to ensure that we do not harm performance by increasing the flexibility in this way we carefully define the algorithm such that we can hoist the calls to `shape_factory` and `shared_factory` as early as possible, and not make them dependent on readiness of the input value.

Note also that we assume here that from `s` we have a clean way to produce either an exceptionally ready, or a nonexceptionally ready `output_future`.
