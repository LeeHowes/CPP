#include <iostream>
#include <experimental/coroutine>

struct ZeroOverheadAwaitable {
    struct promise_type;
    using handle = std::experimental::coroutine_handle<promise_type>;
    struct promise_type {
            int value = 0;
            
            auto initial_suspend() {
                return std::experimental::suspend_never{};
            }

            auto final_suspend() {
                return std::experimental::suspend_never{};
            }

            void return_value(int&& val) {
                value = std::move(val);
            }

            auto get_return_object() {
                return ZeroOverheadAwaitable{handle::from_promise(*this)};
            }

            void unhandled_exception() {
            }
    };
    bool await_ready() { return false; }
    void await_suspend(std::experimental::coroutine_handle<>) {
    }
    auto await_resume() {
        return coroutine_handle_.promise().value;
    }


    void run() {
        coroutine_handle_.resume();
    }
    handle coroutine_handle_;
};


ZeroOverheadAwaitable adder(int value) {
    std::cout << "Adder\n";
    co_return (value + 3);
}

ZeroOverheadAwaitable entryPoint(int value) {
    std::cout << "entryPoint\n";
    auto v = co_await(adder(value));
    std::cout << "v: " << v << "\n";
    co_return v + 5;
}

int main() {
    std::cout << "Hello world\n";
    auto aw = entryPoint(1);
    aw.run();
    std::cout << "Value: " << aw.await_resume() << "\n";
    return 0;
}
