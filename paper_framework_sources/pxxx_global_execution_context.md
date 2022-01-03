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
The global execution context proposed in [@P2079R1] was an important start, but needs updating to match [@P2300R2].

This paper proposes a specific solution: parallel execution context and scheduler.
Lifetime management and other functionality is delegated to other papers, primarily to the `async_scope` defined in [$XXXXR0].

This execution context is of undefined size, supporting explicitly parallel forward progress.
It can build on top of a system thread pool, or on top of a static thread pool, with flexible semantics depending on the constraints that the underlying context offers.
With only parallel forward progress, any created parallel context may be a view onto the underlying shared global context.
Multiple parallel contexts may share the same system thread pool.
The same parallel context may have access to separate system thread pools, or queues to those pools, with different priorities to support priorities of returned schedulers.

The minimal extensions to basic parallel forward progress are to support fundamental functionality that is necessary to make parallel algorithms work:

 * Cancellation: work submitted through the parallel context must be cancellable if the underlying system facilities support it.
 * Forward progress delegation: we must be able to implement a blocking operation that ensures forward progress of a complex parallel algorithm without special cases.

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
  parallel_scheduler get_scheduler_with_priority(scheduler_priority);
};
```

 - On construction, the `parallel_context` may initialize a shared system context, if it has not been previously initialized.
 - The `parallel_context` is non-copyable and non-moveable.
 - The `parallel_context` must outlive work launched on it. If there is outstanding work at the point of destruction, `std::terminate` will be called.
 - The `parallel_context` must outlive schedulers obtained from it. If there are outstanding schedulers at destruction time, this is undefined behavior.
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

  friend implementation-defined-parallel_sender tag_invoke(
    std::execution::schedule_t, const parallel_scheduler&) noexcept;
  friend std::execution::forward_progress_guarantee tag_invoke(
    std::execution::get_forward_progress_guarantee_t,
    const parallel_scheduler&) noexcept;
  friend implementation-defined-bulk-sender tag_invoke(
    std::execution::bulk_t,
    const parallel_scheduler&,
    Sh&& sh,
    F&& f) noexcept;
};
```

 - `parallel_scheduler` is not independely constructable, and must be obtained from a `parallel_context`.
   It is both move and copy constructable and assignable.
 - Two `parallel_scheduler`s compare equal if they share the same underlying `parallel_context` and if they have the same priority.
 - A `parallel_scheduler` has reference semantics with respect to its `parallel_context`.
   Calling any operation other than the destructor on a `parallel_scheduler` after the `parallel_context` it was created from is destroyed is undefined behavior, and that operation may access freed memory.
 - The `parallel_scheduler`:
   - satisfies the `scheduler` concept and implements the `schedule` customisation point to return an `implementation-defined` `sender` type.
   - implements the `get_forward_progress_guarantee` query to return `parallel`.
   - implements the `bulk` CPO to customise the `bulk` sender adapter such that:
     - When `execution::set_value(r, args...)` is called on the created `receiver`, an agent is created with parallel forward progress on the underlying `parallel_context` for each `i` of type `Shape` from `0` to `sh` that calls `f(i, args...)`.
 - `schedule` calls on a `parallel_scheduler` are non-blocking operations.
 - If the underlying `parallel_context` is unable to make progress on work created through `parallel_scheduler` instances, and the sender retrieved from `scheduler` is connected to a `receiver` that supports the `get_delegee_scheduler` query, work may scheduled on the `scheduler` returned by `get_delegee_scheduler` at the time of the call to `start`, or any time later.

## Parallel sender
```cpp
class implementation-defined-parallel_sender {
public:
  friend pair<std::execution::parallel_scheduler, delegee_scheduler> tag_invoke(
    std::execution::get_completion_scheduler_t<set_value_t>,
    const parallel_scheduler&) noexcept;
  friend pair<std::execution::parallel_scheduler, delegee_scheduler> tag_invoke(
    std::execution::get_completion_scheduler_t<set_done_t>,
    const parallel_scheduler&) noexcept;

  template&lt;receiver R>
        requires receiver_of&lt;R>
  friend implementation-defined-operation_state
    tag_invoke(execution::connect_t, implementation-defined-parallel_sender&& j, R && r);

  ...
};
```

`schedule` on a `parallel_scheduler` returns some implementation-defined `sender` type.

This sender satisfies the following properties:
  - Implements the `get_completion_scheduler` query for the value and done channel where it returns a type that is a pair of an object that compares equal to itself, and a representation of delegee scheduler that may be obtained from receivers connected with the sender.
  - If connected with a `receiver` that supports the `get_stop_token` query, if that `stop_token` is stopped, operations on which `start` has been called, but are not yet running (and are hence not yet guaranteed to make progress) should complete with `set_done`.
  - `connect`ing the `sender` and calling `start()` on the resulting operation state are non-blocking operations.


# Examples
