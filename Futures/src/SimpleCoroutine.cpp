#include <iostream>
#include <experimental/coroutine>

struct ZeroOverheadAwaitable {
    struct promise_type;
    using handle = std::experimental::coroutine_handle<promise_type>;
    struct promise_type {
            int value = 0;
            
            auto initial_suspend() {
                return std::experimental::suspend_always{};
            }

            auto final_suspend() {
                return std::experimental::suspend_never{};
            }

            void return_value(int val) {
                std::cerr << "\tSet return value: " << val << "\n";
                value = std::move(val);
            }

            auto get_return_object() {
                return ZeroOverheadAwaitable{handle::from_promise(*this)};
            }

            void unhandled_exception() {
            }
    };
    bool await_ready() { return false; }
    void await_suspend(std::experimental::coroutine_handle<> h) {
        std::cout << "await_suspend zero\n";
        coroutine_handle_.resume();
        std::cout << "Middle\n";
        h.resume();
        std::cout << "end await_suspend zero\n";
    }
    auto await_resume() {
        std::cout << "Await_resume\n";
        return coroutine_handle_.promise().value;
    }
    
    handle coroutine_handle_;
};

struct SyncAwaitAwaitable {
    struct promise_type;
    using handle = std::experimental::coroutine_handle<promise_type>;
    struct promise_type {
            int value = 0;
            
            auto initial_suspend() {
                return std::experimental::suspend_always{};
            }

            auto final_suspend() {
                return std::experimental::suspend_never{};
            }

            void return_value(int val) {
                std::cerr << "\tSet Sync return value: " << val << "\n";
                value = std::move(val);
            }

            auto get_return_object() {
                return SyncAwaitAwaitable{handle::from_promise(*this)};
            }

            void unhandled_exception() {
            }
    };

    int value() {
        return coroutine_handle_.promise().value;
    }

    void run() {
        coroutine_handle_.resume();
    }

    handle coroutine_handle_;
};

template<class Awaitable>
int sync_await(Awaitable&& aw) {
    auto syncAW = [&]() -> SyncAwaitAwaitable {
        std::cout << "\tSync await function\n";
        int val = co_await std::forward<Awaitable>(aw);
        std::cout << "\tSync await val: " << val << "\n";
        co_return val;
    }();
    std::cout << "Before run\n";
    syncAW.run();
    std::cout << "After run\n";
    return syncAW.value();
}

ZeroOverheadAwaitable adder(int value) {
    std::cout << "\t\t\tAdder with value " << value << "\n";
    co_return (value + 3);
}

ZeroOverheadAwaitable entryPoint(int value) {
    std::cout << "\t\tentryPoint with value " << value << "\n";
    auto v = co_await(adder(value));
    std::cout << "v: " << v << "\n";
    co_return v + 5;
}

int main() {
    std::cout << "Hello world\n";
    auto aw = entryPoint(1);
    std::cout << "After entryPoint\n";
    auto val = sync_await(std::move(aw));
    std::cout << "After sync\n";
    return 0;
}
