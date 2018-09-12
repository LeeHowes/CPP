# A firm specification of bulk execution

## Summary
Bulk execution as defined in P0443 is a little vague:
* It is inconsistent with non-bulk tasks in terms of when information is available.
* It is hard to know for sure when different parts of it run.
* It is not clear whether the shared factory produced globally or locally shared data, and what its limitations are in terms of copy/move constructibility.
* The result factory also produces a shared value, that is also unclear in its copy/move constructibility, and that ends up in the output future.
* When and how does the result move into the output future?
* Where is the result stored and what is its lifetime?
* What is the lifetime of the shared state?
* How do we know what to pass as a shape?
  * The input data is not available at this point.
  * It is algorithm-specific, rather than executor-specific, and we have no concrete executor queries about hardware concurrency or vector size to choose usefully.
* The shape type is under-specified and it is not clear what would be a valid type. Most discussion appears to imply that it is integral, but need it be?

## The request
We propose that we improve the definition of bulk:
* Define bulk execution in terms of a clearly defined algorithm.
* Make the sequence of operations and memory orderings between them clear.
* Maintain some flexibility in the algorithm to allow for optimisations.

In addition we propose modifying the interface slightly to improve the flexibility and adaptation to input data.

Take an algorithm like:
```
v = future<vector<bool>>
v2 = filter(v, ==true)
v3 = transform(v2, [](bool val){return 1;});
```

To implement transform using `bulk_then_execute` we would need to serialize these operations in order to know how big to make the output array, and potentially also to parameterise the shape.
To implement filter using `bulk_then_execute` is harder, but if order does not matter we could use an atomic counter to track the index of the last element and a concurrency-safe vector as the shared output structure.
One downside of this is that that concurrency-safe vector is not an implementation-detail, but would have to be exposed in the type of `v3` because the result type in `bulk_then_execute` is shared state and a result value.

This shows an interesting inconsistency with non-bulk tasks. For example, if I implement the above using non-bulk tasks:
```
v = future<vector<bool>>
v2 = v.then([v](vector<bool> v){
  v2 = vector<bool>();
  for(element : v) {
    v2.push_back(element);
  }
  return v2;
  })
v3 = v2.then([](vector<bool> v2){
  v3 = vector<int>(v2.size());
  for(idx : v2.range()) {
    v3[idx] = 1;
  }
  return v2;
  })
```

Creating output data at each stage dependent on the input is trivial, and the obvious design to use. The current bulk design does not allow this because the result factory has no access to that input, or at least, it does not allow it purely by chaining executor tasks. We can implement something like the above `.then` using bulk executors, but it is the future code handling the asynchronous behaviour. With a future GPU implementation that is capable of allocating on-device, or at least by efficiently enqueueing user-side CPU code inside its queuing infrastructure, we have lost information in the queuing model to support that functionality.

## Proposal
To more concretely define the algorithm, and to allow more flexibility of use, we replace the current definition of `bulk_then_execute` as below, noting that this set of changes is defined only in terms of the change to P0443, and is independent of parallel discussions about redefining in terms of senders and receivers.

 * `x` denotes a (possibly const) executor object of type `X`,
 * `shape` denotes a shape object of type `Shape` such that `executor_shape_t<X>` is `Shape` and that `Shape` satisfies concept `RandomAccessRange<Shape>`
 * `fut` denotes a future object satisfying the `Future` requirements,
 * `val` denotes any object of type `V` provided to the execution by `fut` on nonexceptional completion of the task associated with `fut`,
 * `e` denotes any object provided to the execution by `fut` on exceptional completion of the task associated with `fut`
 * `shape_factory` denotes a `CopyConstructible` function object with zero arguments, or with one argument of type `const V&`, and whose result type is `Shape`,
 * `shared_factory` denotes a `CopyConstructible` function object two argument of type `const Shape&` and `const V&`, and whose result type is `S`,
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
     * invoke `shared_factory(shape, val)` to produce value `s`.
   * If `fut` is nonexceptionally ready and if `NORMAL` is a well-formed expression:
     * creates `std::distance(begin(shape), end(shape))` agents
     * invokes `NORMAL` at most once for each value in range `begin(shape)` to `end(shape)` with the call to `DECAY_COPY` being evaluated in the thread that called `bulk_then_execute`.
   * If `fut` is exceptionally ready and if `EXCEPTIONAL` is a well-formed expression:
     * creates `std::distance(begin(shape), end(shape))` agents
     * invokes `EXCEPTIONAL` at most once for each value in range `begin(shape)` to `end(shape)` with the call to `DECAY_COPY` being evaluated in the thread that called `bulk_then_execute`.
   * If neither `NORMAL` nor `EXCEPTIONAL` is a well-formed expression, the invocation of `bulk_then_execute` shall be ill-formed
   * The invocation of `bulk_then_execute` synchronizes with (C++Std [intro.multithread]) the invocations of `f`.
 * If `result_selector` is provided
   * If `fut` is nonexceptionally ready:
     * Calls `result_selector(std::move(s), output_fut)` in some agent owned by executor `X`   
   * If `fut` is exceptionally ready:
     * Calls `result_selector(e, std::move(s), exception_arg, output_fut)` in some agent owned by executor `X`
 * Otherwise:
   * If `fut` is nonexceptionally ready:
     * Implements `result_selector` as-if by `output_fut = std::move(s)`
   * If `fut` is exceptionally ready:
     * Implements `result_selector` as-if by `std::rethrow_exception(e)`
 * All instances of `f` synchronize with the call to `result_selector`.
 * The task's, and s's destructors run in a context owned by executor `x`.

Note that to ensure that we do not harm performance by increasing the flexibility in this way we carefully define the algorithm such that we can hoist the calls to `shape_factory` and `shared_factory` as early as possible, and not make them dependent on readiness of the input value.

Note also that we assume here that the executor will package a thrown exception correctly into the output future. If we have a well-defined way to put a value or exception in the output then the exceptional case for `result_selector` can be defined without a throw.

In this model, the pseudocode for the simple filter example above, if input-taking construction functions are passed, looks more like:
```
v = future<vector<bool>>
v2 = v.then([v](vector<bool> v){
  s = shape_factory(v)
  v2, atomic_size = shared_factory(s, v) // v2 is v in size
  parallel_for(i : s) {
    v2[atomic_size++] = element;
  }
  return result_selector(v2, atomic_size); // take atomic_size elements from v2
  })
v3 = v2.then([](vector<bool> v2){
  s = shape_factory(v2)
  v3 = shared_factory(s, v2) // v3 is v2 in size but a vector of ints
  parallel_for(idx : s) {
    v3[idx] = v2[idx]
  }
  return result_selector(v2); // just move the result out
  })
```

In this case I oversized the shared version of the vector in the filter task and trimmed it on output. Another approach would be to use a thread-safe growing vector type in the shared state.

This is much closer to what we do for the scalar case than bulk currently allows, and hence provides more information to the runtime. Current bulk would mean hoisting the shape, shared and result factories to the top, and returning an oversized result factory from stage 1. There are valid optimisations arising from that design, but it makes bulk a very specific model that is optimised for a use case rather than being a generic parallel version of a scalar task.
