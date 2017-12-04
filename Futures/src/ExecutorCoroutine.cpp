#include <iostream>
#include <thread>
#include <experimental/coroutine>

#include "Executor.h"
#include "MyAsyncLibrary.h"

struct ZeroOverheadAwaitable {
    struct promise_type;
    using handle = std::experimental::coroutine_handle<promise_type>;

    ZeroOverheadAwaitable(ZeroOverheadAwaitable&& rhs) : coroutine_handle_{std::move(rhs.coroutine_handle_)} {
        rhs.coroutine_handle_ = {};
    }
    ZeroOverheadAwaitable(handle&& rhs) : coroutine_handle_{std::move(rhs)} {
    }
    ~ZeroOverheadAwaitable() {
        if(coroutine_handle_) {
           coroutine_handle_.destroy();
        }
    }
    struct promise_type {
            int value = 0;
            std::experimental::coroutine_handle<> waiter;
            std::shared_ptr<DrivenExecutor> executor;

            struct final_suspend_result : std::experimental::suspend_always {
                promise_type *promise_;
                final_suspend_result(promise_type* promise) : promise_{promise} {}
                bool await_reday(){ return false; }
                void await_suspend(std::experimental::coroutine_handle<>) {
                    promise_->waiter.resume();
                }
                void await_resume() {}
            };
            
            auto initial_suspend() {
                return std::experimental::suspend_always{};
            }

            auto final_suspend() {
                return final_suspend_result{this};
            }

            void return_value(int val) {
                value = std::move(val);
            }

            auto get_return_object() {
                return ZeroOverheadAwaitable{handle::from_promise(*this)};
            }

            void unhandled_exception() {
            }
    };
    bool await_ready() { return false; }
    template<class PromiseType>
    void await_suspend(std::experimental::coroutine_handle<PromiseType> h) {
        // Update executor in promise with that from handle's promise
        // Carry the executor through the zero overhead chain, though it will not be 
        // used by this awaitable
        coroutine_handle_.promise().waiter = h;
        coroutine_handle_.promise().executor = h.promise().executor;
        coroutine_handle_.resume();
    }
    auto await_resume() {
        return coroutine_handle_.promise().value;
    }
    
    handle coroutine_handle_;
};

template<class T>
struct SyncAwaitAwaitable {
    struct promise_type;
    using handle = std::experimental::coroutine_handle<promise_type>;

    SyncAwaitAwaitable(SyncAwaitAwaitable&& rhs) : coroutine_handle_{std::move(rhs.coroutine_handle_)} {
        rhs.coroutine_handle_ = {};
    }
    SyncAwaitAwaitable(handle&& rhs) : coroutine_handle_{std::move(rhs)} {
    }
    ~SyncAwaitAwaitable() {
        if(coroutine_handle_) {
             coroutine_handle_.destroy();
        }
    } 
    struct promise_type {
            T value{};
            std::shared_ptr<DrivenExecutor> executor;

            // Sync awaitable has an executor that will be driven inline in the caller
            promise_type() {
                executor = std::make_shared<DrivenExecutor>();
            }

            struct final_suspend_result : std::experimental::suspend_always {
                promise_type *promise_;
                final_suspend_result(promise_type* promise) : promise_{promise} {}
                bool await_reday(){ return false; }
                void await_suspend(std::experimental::coroutine_handle<>) {
                    // Final suspend should tell the executor to terminate to unblock it
                    promise_->executor->terminate();
                }
                void await_resume() {}
            };
 
            auto initial_suspend() {
                return std::experimental::suspend_always{};
            }

            auto final_suspend() {
                return final_suspend_result{this};
            }

            void return_value(T val) {
                value = std::move(val);
            }

            auto get_return_object() {
                return SyncAwaitAwaitable{handle::from_promise(*this)};
            }

            void unhandled_exception() {
            }
    };

    T get() {
        coroutine_handle_.promise().executor->execute([this](){coroutine_handle_.resume();});
        coroutine_handle_.promise().executor->run();
        return coroutine_handle_.promise().value;
    }

    handle coroutine_handle_;
};

template<class Awaitable, class T = std::decay_t<decltype(std::declval<Awaitable>().await_resume())>>
auto sync_await(Awaitable&& aw) -> T {
    return  
        [&]() -> SyncAwaitAwaitable<T> {
            int val = co_await std::forward<Awaitable>(aw);
            co_return val;
        }().get();
}

ZeroOverheadAwaitable adder(int value) {
    where("adder");
    co_return (value + 3);
}

ZeroOverheadAwaitable entryPoint(int value) {
    where("entryPoint");
    auto v = co_await(adder(value));
    auto v2 = co_await(adder(value+5));
    co_return v + v2;
}

MyLibrary::AsyncAwaitable asyncEntryPoint(int value) {
    where("asyncEntryPoint");
    auto v = co_await(adder(value));
    auto v2 = co_await(adder(value+5));
    co_return v + v2;
}

MyLibrary::AsyncAwaitable asyncAdder(int value) {
    where("asyncAdder");
    co_return (value + 4);
}

ZeroOverheadAwaitable entryPoint2(int value) {
    where("entryPoint2");
    auto v1 = co_await(asyncAdder(value));
    auto v2 = co_await(adder(value + 5));
    auto v3 = co_await(asyncAdder(value + 6));
    auto v4 = co_await [=]() -> MyLibrary::AsyncAwaitable {
        auto v1 = co_await adder(value);
        auto v2 = co_await(asyncAdder(value));
        co_return v1 + v2;
    }();
    co_return v1 + v2 + v3 + v4;
}


int main() {
    // Temporarily create this globally
    where("main()");

    {
        auto val = sync_await(entryPoint(1));
        std::cout << "Value: " << val << "\n";
    }
    
    MyLibrary::init();

    std::cout << "Before async after thread started\n";
    
    {
        auto val = sync_await(asyncEntryPoint(1));
        std::cout << "Value: " << val << "\n";
    }

    std::cout << "Before complex async\n";
    
    {
        auto val = sync_await(entryPoint2(17));
        std::cout << "Value: " << val << "\n";
    }
    
    MyLibrary::shutdown();
    std::cout << "END\n";


    return 0;
}
