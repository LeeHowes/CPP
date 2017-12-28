#pragma once

#include <experimental/coroutine>

#include "Executor.h"

template<class T>
struct AsyncAwaitAwaitable {
    struct promise_type;
    using handle = std::experimental::coroutine_handle<promise_type>;

    AsyncAwaitAwaitable(AsyncAwaitAwaitable&& rhs) : coroutine_handle_{std::move(rhs.coroutine_handle_)} {
        rhs.coroutine_handle_ = {};
    }
    AsyncAwaitAwaitable(handle&& rhs) : coroutine_handle_{std::move(rhs)} {
    }
    ~AsyncAwaitAwaitable() {
        if(coroutine_handle_) {
             coroutine_handle_.destroy();
        }
    } 
    struct promise_type {
            T value{};
            std::shared_ptr<DrivenExecutor> executor;
            std::function<void(T)> callback;

            // Sync awaitable has an executor that will be driven inline in the caller
            promise_type() {
            }

            struct final_suspend_result : std::experimental::suspend_always {
                promise_type *promise_;
                final_suspend_result(promise_type* promise) : promise_{promise} {}
                bool await_ready(){ return false; }
                void await_suspend(std::experimental::coroutine_handle<>) {
                    promise_->executor->execute(
                        [cb = std::move(promise_->callback), val = promise_->value](){
                            cb(std::move(val));
                        });
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
                return AsyncAwaitAwaitable{handle::from_promise(*this)};
            }

            void unhandled_exception() {
            }
    };

    void setExecutor(std::shared_ptr<DrivenExecutor> exec) {
        coroutine_handle_.promise().executor = exec;
    }

    void setCallback(std::function<void(T)> callback) {
        coroutine_handle_.promise().callback = std::move(callback);
    }

    handle coroutine_handle_;
};

// async_await will call callback on exec when the awaitable completes.
template<class Awaitable, class T = std::decay_t<decltype(std::declval<Awaitable>().await_resume())>, class F>
void async_await(
        std::shared_ptr<DrivenExecutor> exec, 
        Awaitable&& aw,
        F&& callback) {
    auto a = [](Awaitable aw) -> AsyncAwaitAwaitable<T> {
            int val = co_await aw;
            co_return val;
        };
    auto spa = std::make_shared<AsyncAwaitAwaitable<T>>(a(std::forward<Awaitable>(aw)));
    spa->setExecutor(exec);
    // Ensure that spa stays alive at least until the callback completes
    spa->setCallback(std::function<void(T)>{[spa, cb = std::move(callback)](T&& val){
            cb(std::move(val));
        }});
    // Move awaitable onto the heap to extend its lifetime, enqueue a task that resumes it
    spa->coroutine_handle_.promise().executor->execute([spa](){
            spa->coroutine_handle_.resume();});
    spa.reset();
}


