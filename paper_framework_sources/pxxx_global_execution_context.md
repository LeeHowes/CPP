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
```cpp
class parallel_scheduler {
public:
  parallel_scheduler() = delete;
  ~parallel_scheduler();

  parallel_scheduler(const parallel_scheduler&);
  parallel_scheduler(parallel_scheduler&&);
  parallel_scheduler& operator=(const parallel_scheduler&);
  parallel_scheduler& operator=(parallel_scheduler&&);

  friend _sender tag_invoke(std::execution::schedule_t, const inline_scheduler&) noexcept;
  friend _sender tag_invoke(std::execution::get_completion_scheduler_t<set_value_t>, const inline_scheduler&) noexcept;
  friend _sender tag_invoke(std::execution::get_completion_scheduler_t<set_error_t>, const inline_scheduler&) noexcept;
  friend _sender tag_invoke(std::execution::get_completion_scheduler_t<set_done_t>, const inline_scheduler&) noexcept;
  friend _sender tag_invoke(std::execution::get_forward_progress_guarantee_t, const inline_scheduler&) noexcept;
  friend _sender tag_invoke(std::execution::bulk_t, const inline_scheduler&) noexcept;
  friend _sender tag_invoke(std::execution::with_delegee_scheduler_t, const inline_scheduler&) noexcept;
  friend _sender tag_invoke(std::execution::sync_wait_t, const inline_scheduler&) noexcept;
};
```

TODO:

 * Bulk customisation
 * Priorities
 * with_delegee_scheduler to make sure that FP delegation works. Give async_scope example. see: https://github.com/brycelelbach/wg21_p2300_std_execution/issues/294
 * Link-time replacement. Should this be up to the standard library?
 * Cancellation
 * Get forward progress guarantee
 * Blocking property
 * sync_wait to support FP delegation
 * What should behaviour of parallel_scheduler's sender be if the context is destroyed. Is that UB or should that safely cancel on submit? If schedulers hold simple references to the context UB is the only option. If the context ref counts an underlying state we could cancel. There is a tradeoff there.
