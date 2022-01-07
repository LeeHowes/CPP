---
title: "System execution context"
document: PXXXXR0
date: 2021-12-30
audience: SG1
author:
  - name: Lee Howes
    email: <lwh@fb.com>
toc: false
---


# Introduction
[@P2300] describes a rounded set of primitives for asynchronous and parallel execution that give a firm grounding for the future.
However, the paper lacks a standard execution context and scheduler.
As noted in [@P2079R1], the earlier `static_thread_pool` had many shortcomings and was not included in [@P2300] for that reason.
The global execution context proposed in [@P2079R1] was an important start, but needs updating to match [@P2300].

This paper proposes a specific solution: parallel execution context and scheduler.
Lifetime management and other functionality is delegated to other papers, primarily to the `async_scope` defined in [@P2519].

This execution context is of undefined size, supporting explicitly *parallel forward progress*.
It can build on top of a system thread pool, or on top of a static thread pool, with flexible semantics depending on the constraints that the underlying context offers.

By requiring only parallel forward progress, any created parallel context is able to be a view onto the underlying shared global context.
All instances of the `system_context` share the same underlying execution context.
If the underlying context is a static thread pool, then all `system_context`s should reference that same static thread pool.
This is important to ensure that applications can rely on constructing `system_context`s as necessary, without spawning an ever increasing number of threads.

The minimal extensions to basic parallel forward progress are to support fundamental functionality that is necessary to make parallel algorithms work:

 * Cancellation: work submitted through the parallel context must be cancellable.
 * Forward progress delegation: we must be able to implement a blocking operation that ensures forward progress of a complex parallel algorithm without special cases.

# Design
## system_context

The `system_context` creates a view on some underlying execution context supporting *parallel forward progress*.
A `system_context` must outlive any work launched on it.

```cpp
class system_context {
public:
  system_context();
   ~system_context();

  system_context(const system_context&) = delete;
  system_context(system_context&&) = delete;
  system_context& operator=(const system_context&) = delete;
  system_context& operator=(system_context&&) = delete;

  system_scheduler get_scheduler();
};
```

 - On construction, the `system_context` may initialize a shared system context, if it has not been previously initialized.
 - The `system_context` is non-copyable and non-moveable.
 - The `system_context` must outlive work launched on it. If there is outstanding work at the point of destruction, `std::terminate` will be called.
 - The `system_context` must outlive schedulers obtained from it. If there are outstanding schedulers at destruction time, this is undefined behavior.
 - `get_scheduler` returns a `system_scheduler` instance that holds a reference to the `system_context`.

## system_scheduler

A `system_scheduler` is a copyable handle to a `system_context`. It is the means through which agents are launched on a `system_context`.
The `system_scheduler` instance does not have to outlive work submitted to it.

```cpp
class system_scheduler {
public:
  system_scheduler() = delete;
  ~system_scheduler();

  system_scheduler(const system_scheduler&);
  system_scheduler(system_scheduler&&);
  system_scheduler& operator=(const system_scheduler&);
  system_scheduler& operator=(system_scheduler&&);

  bool operator==(const system_scheduler&) const noexcept;

  friend implementation-defined-system_sender tag_invoke(
    std::execution::schedule_t, const system_scheduler&) noexcept;
  friend std::execution::forward_progress_guarantee tag_invoke(
    std::execution::get_forward_progress_guarantee_t,
    const system_scheduler&) noexcept;
  friend implementation-defined-bulk-sender tag_invoke(
    std::execution::bulk_t,
    const system_scheduler&,
    Sh&& sh,
    F&& f) noexcept;
};
```

 - `system_scheduler` is not independely constructable, and must be obtained from a `system_context`.
   It is both move and copy constructable and assignable.
 - Two `system_scheduler`s compare equal if they share the same underlying `system_context`.
 - A `system_scheduler` has reference semantics with respect to its `system_context`.
   Calling any operation other than the destructor on a `system_scheduler` after the `system_context` it was created from is destroyed is undefined behavior, and that operation may access freed memory.
 - The `system_scheduler`:
   - satisfies the `scheduler` concept and implements the `schedule` customisation point to return an `implementation-defined` `sender` type.
   - implements the `get_forward_progress_guarantee` query to return `parallel`.
   - implements the `bulk` CPO to customise the `bulk` sender adapter such that:
     - When `execution::set_value(r, args...)` is called on the created `receiver`, an agent is created with parallel forward progress on the
       underlying `system_context` for    each `i` of type `Shape` from `0` to `sh` that calls `f(i, args...)`.
 - `schedule` calls on a `system_scheduler` are non-blocking operations.
 - If the underlying `system_context` is unable to make progress on work created through `system_scheduler` instances, and the sender retrieved
   from `scheduler` is connected to a `receiver` that supports the `get_delegee_scheduler` query, work may scheduled on the `scheduler`
   returned by `get_delegee_scheduler` at the time of the call to `start`, or at any later point before the work completes.

## Parallel sender
```cpp
class implementation-defined-system_sender {
public:
  friend pair<std::execution::system_scheduler, delegee_scheduler> tag_invoke(
    std::execution::get_completion_scheduler_t<set_value_t>,
    const system_scheduler&) noexcept;
  friend pair<std::execution::system_scheduler, delegee_scheduler> tag_invoke(
    std::execution::get_completion_scheduler_t<set_done_t>,
    const system_scheduler&) noexcept;

  template&lt;receiver R>
        requires receiver_of&lt;R>
  friend implementation-defined-operation_state
    tag_invoke(execution::connect_t, implementation-defined-system_sender&&, R &&);

  ...
};
```

`schedule` on a `system_scheduler` returns some implementation-defined `sender` type.

This sender satisfies the following properties:
  - Implements the `get_completion_scheduler` query for the value and done channel where it returns a type that is a pair
    of an object that compares equal to itself, and a representation of delegee scheduler that may be obtained from
    receivers connected with the sender.
  - If connected with a `receiver` that supports the `get_stop_token` query, if that `stop_token` is stopped, operations
    on which `start` has been called, but are not yet running (and are hence not yet guaranteed to make progress) **must**
    complete with `set_done` as soon as is practical.
  - `connect`ing the `sender` and calling `start()` on the resulting operation state are non-blocking operations.

## Priorities
We implement priorities as a receiver query, `get_priority` that defaults to normal priority.
Each task submitted can carry its own priority and be placed in the appropriate queue.
The scheduler may maintain multiple queues for different priorities.

The `with_priority` sender adaptor sets the priority on the receiver passed to the adapted sender to allow the priority to be increased.

```cpp
enum class scheduler_priority : int {
  low,
  medium,
  high
};

struct get_priority_t {
  template<typename Ctx>
    requires tag_invocable<get_priority_t, const Ctx&>
  priority operator()(const Ctx& ctx) const noexcept {
    return tag_invoke(get_priority_t{}, ctx);
  }
  template<typename Ctx>
    requires (!tag_invocable<get_priority_t, const Ctx&>)
  priority operator()(const Ctx&) const noexcept {
    return priority::normal;
  }
};
inline constexpr get_priority_t get_priority{};
}

execution::sender auto with_priority(
    execution::sender auto,
    scheduler_priority
);
```

# Examples
As a simple parallel scheduler we can use it locally, and `sync_wait` on the work to make sure that it is complete.
With forward progress delegation this would also allow the scheduler to delegate work to the blocked thread.
This example is derived from the Hello World example in [@P2300]. Note that it only adds a well-defined context
object, and queries that for the scheduler.
Everything else is unchanged about the example.

```c++
using namespace std::execution;

system_context ctx;
scheduler auto sch = ctx.scheduler();

sender auto begin = schedule(sch);
sender auto hi = then(begin, []{
    std::cout << "Hello world! Have an int.";
    return 13;
});
sender auto add_42 = then(hi, [](int arg) { return arg + 42; });

auto [i] = this_thread::sync_wait(add_42).value();
```

In real examples we would not always be calling `sync_wait`, we would have more structured code.
For this we will often use the `async_scope` from [@P2519].
`async_scope` provides a generalised mechanism for safely managing the lifetimes of tasks
even when the results are not required.

```c++
using namespace std::execution;

system_context ctx;
int result = 0;

{
  async_scope scope;
  scheduler auto sch = ctx.scheduler();

  sender auto val = on(
    sch, just() | then([sch, &scope](auto sched) {

        int val = 13;

        auto print_sender = just() | then([val]{
          std::cout << "Hello world! Have an int with value: " << val << "\n";
        });
        // spawn the print sender on sched to make sure it
        // completes before shutdown
        scope.spawn_now(on(sch, std::move(print_sender)));

        return val;
    })
  ) | then([&result](auto val){result = val});

  scope.spawn_now(std::move(val));


  // Safely wait for all nested work
  this_thread::sync_wait(scope.empty());
};

// The scope ensured that all work is safely joined, so result contains 13
std::cout << "Result: " << result << "\n";

// and destruction of the context is now safe
```

---
references:
  - id: P2519
    citation-label: P2519R0
    title: "async_scope - Creating scopes for non-sequential concurrency"
    issued:
      year: 2022
  - id: P2300
    citation-label: P2300R3
    title: "std::execution"
    issued:
      year: 2021
    URL: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2300r3.html
---
