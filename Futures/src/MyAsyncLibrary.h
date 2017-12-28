#pragma once

#include <string>
#include <experimental/coroutine>

#include "Executor.h"

void where(std::string name);

namespace MyLibrary {

void init();
void shutdown();
std::shared_ptr<DrivenExecutor> getExecutor();

// Awaitable that always resumes the coroutine on a particular executor.
struct AsyncAwaitable {
    struct promise_type;
    using handle = std::experimental::coroutine_handle<promise_type>;

    AsyncAwaitable(AsyncAwaitable&& rhs) : coroutine_handle_{std::move(rhs.coroutine_handle_)} {
        rhs.coroutine_handle_ = {};
    }
    AsyncAwaitable(handle&& rhs) : coroutine_handle_{std::move(rhs)} {
    }
    ~AsyncAwaitable() {
        if(coroutine_handle_) {
           coroutine_handle_.destroy();
        }
    }
    struct promise_type {
            int value = 0;
            std::experimental::coroutine_handle<> waiter;
        
            std::shared_ptr<DrivenExecutor> executor;
            std::shared_ptr<DrivenExecutor> waiterExecutor;

            // For now, async awaitable can use the global executor
            promise_type() {
                executor = getExecutor();
            }

            struct final_suspend_result : std::experimental::suspend_always {
                promise_type *promise_;
                final_suspend_result(promise_type* promise) : promise_{promise} {}
                bool await_ready(){ return false; }
                void await_suspend(std::experimental::coroutine_handle<>) {
                    // Resume the waiter on its executor to give correct async behaviour
                    promise_->waiterExecutor->execute([promise = promise_](){promise->waiter.resume();});
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
                return AsyncAwaitable{handle::from_promise(*this)};
            }

            void unhandled_exception() {
            }
    };
    bool await_ready() { return false; }
    template<class PromiseType>
    void await_suspend(std::experimental::coroutine_handle<PromiseType> h) {
        coroutine_handle_.promise().waiter = h;
        coroutine_handle_.promise().waiterExecutor = h.promise().executor;
        // Resume this handle on its executor
        coroutine_handle_.promise().executor->execute([this](){
                coroutine_handle_.resume();});
    }
    auto await_resume() {
        return coroutine_handle_.promise().value;
    }
    
    handle coroutine_handle_;
};

} // namespace MyLibrary
