---
title: "Global execution context"
document: PXXXXR0
date: 2021-12-30
audience: SG1
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---


# Introduction
[@P2300R2] describes a rounded set of primitives for asynchronous and parallel execution that give a firm grounding for the future.
However, the paper lacks a standard execution context and scheduler.
As noted in [@P2079R1], the earlier `static_thread_pool` had many shortcomings and was not included in [@P2300R2] for that reason.
The global execution context proposed in [@P2079R1] was an important start, but needs updating to match [@P2300R2] and we believe to simplify the design and focus on solving a very specific problem.

This paper proposes a specific solution: parallel execution context and scheduler. Lifetime management and other functionality is delegated to other papers.
This execution context is of undefined size, supporting explicitly parallel forward progress.
As a result, it can build on top of a system thread pool, or on top of a static thread pool, with flexible semantics depending on the constraints that the underlying context offers.
With only parallel forward progress, any created parallel context may be a view onto the underlying shared global context.

# Design
## parallel_context

The `parallel_context` creates a view on some underlying parallel forward progress execution context.
It may share a system context, or it may own a context of its own.
To support the owning implementation, a `parallel_context` must outlive any work launched on it.

```cpp
class parallel_context {
public:
  enum class scheduler_priority {
    low,
    medium,
    high
  };

  parallel_context();
   ~parallel_context();

  parallel_context(const parallel_context&) = delete;
  parallel_context(parallel_context&&) = delete;
  parallel_context& operator=(const parallel_context&) = delete;
  parallel_context& operator=(parallel_context&&) = delete;

  parallel_scheduler get_scheduler();
  parallel_scheduler get_scheduler_with_priotity(scheduler_priority);
};
```

 - The `parallel_context` is non-copyable and non-moveable.
 - The `parallel_context` must outlive work launched on it. If there is outstanding work at the point of destruction, `std::terminate` will be called.
 - `get_scheduler` returns a `parallel_scheduler` instance that holds a reference to the `parallel_context`.
 - `get_scheduler_with_priotity` returns a `parallel_scheduler` instance that holds a reference to the `parallel_context`. The scheduler is created with the specified priority. Priorities offer a layering of tasks, with no guarantee.

## parallel_scheduler

A `parallel_scheduler` is a copyable handle to a `parallel_context`. It is the means through which agents are launched on a `parallel_context`.
The `parallel_scheduler` instance does not have to outlive work submitted to it.

```cpp
class parallel_scheduler {
public:
  parallel_scheduler() = delete;
  ~parallel_scheduler();

  parallel_scheduler(const parallel_scheduler&);
  parallel_scheduler(parallel_scheduler&&);
  parallel_scheduler& operator=(const parallel_scheduler&);
  parallel_scheduler& operator=(parallel_scheduler&&);

  bool operator==(const parallel_scheduler&) const noexcept;

  scheduler_priority get_priority() const noexcept;

  friend implementation-defined-sender tag_invoke(
    std::execution::schedule_t, const parallel_scheduler&) noexcept;
  friend std::execution::parallel_scheduler tag_invoke(
    std::execution::get_completion_scheduler_t<set_value_t>,
    const parallel_scheduler&) noexcept;
  friend std::execution::forward_progress_guarantee tag_invoke(
    std::execution::get_forward_progress_guarantee_t,
    const parallel_scheduler&) noexcept;
  friend implementation-defined-bulk-sender tag_invoke(
    std::execution::bulk_t,
    const parallel_scheduler&,
    Sh&& sh,
    F&& f) noexcept;
  friend parallel_scheduler tag_invoke(
    std::execution::with_delegee_scheduler_t,
    const scheduler auto &) noexcept;
};
```

 - `parallel_scheduler` is not independely constructable, and must be obtained from a `parallel_context`.
It is both move and copy constructable and assignable.
 - Two `parallel_scheduler`s compare equal if they share the same underlying `parallel_context` and if they have the same priority.
 - The `parallel_scheduler`:
   - satisfies the `scheduler` concept and implements the `schedule` customisation point to return an `implementation-defined` `sender` type.
   - implements the `get_completion_scheduler` query for the value channel where it returns a type that compares equal to itself.
   - implements the `get_forward_progress_guarantee` query to return `parallel`.
   - implements the `bulk` CPO to customise the `bulk` sender adapter such that:
     - When `execution::set_value(r, args...)` is called on the created `receiver`, an agent is created with parallel forward progress on the underlying `parallel_context` for each `i` of type `Shape` from `0` to `sh` that calls `f(i, args...)`.
   - implements the `with_delegee_scheduler` scheduler mutator to pair the `parallel_scheduler` with a second `scheduler` that it will delegate to to ensure that it makes forward progress.



TODO:

 * Link-time replacement. Should this be up to the standard library?
 * Cancellation
 * Blocking property
 * What should behaviour of parallel_scheduler's sender be if the context is destroyed. Is that UB or should that safely cancel on submit? If schedulers hold simple references to the context UB is the only option. If the context ref counts an underlying state we could cancel. There is a tradeoff there.
