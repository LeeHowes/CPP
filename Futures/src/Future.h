#pragma once

#include <optional>

#include "SimpleAwaitable.h"
#include "AsyncAwait.h"
#include "MyAsyncLibrary.h"

struct VirtualAwaitable {
    ~VirtualAwaitable() {}
    virtual bool await_ready() = 0;
    virtual void await_suspend(std::experimental::coroutine_handle<>) = 0;
    virtual void await_resume() = 0;
};

template<class AwaitableT> 
struct VirtualAwaitableImpl : public VirtualAwaitable {
    VirtualAwaitableImpl(AwaitableT& awaitable) : awaitable_{awaitable} {}
    bool await_ready() override {
        return awaitable_.await_ready();
    }

    void await_suspend(std::experimental::coroutine_handle<> h) override {
        awaitable_.await_suspend(h);
    }

    void await_resume() override {
        awaitable_.await_resume();
    }

    AwaitableT& awaitable_;
};

template<class T>
struct CoreBase {
    virtual ~CoreBase() {}
    virtual T get() = 0;
    virtual void setExecutor(std::shared_ptr<DrivenExecutor> exec) = 0;
    virtual std::shared_ptr<DrivenExecutor> getExecutor() = 0;
    virtual void setCallback(std::function<void(T)>) = 0;
    virtual bool isAwaitable() = 0;
    virtual VirtualAwaitable& getAwaitable() = 0;

    std::shared_ptr<DrivenExecutor> exec_;
};

// This core wraps an arbitrary Awaitable into a future without a promise involved
// This might be one option for hiding a communication-style future from an asynchronous programming future
// but more efficiently so that co_await works well with it only dependent on virtual function calls except
// where synchronization is absolutely necessary.
template<class T, class AwaitableT>
struct AwaitableCore : CoreBase<T> {
    AwaitableCore(AwaitableT&& awaitable) : awaitable_(std::move(awaitable)), vAwaitable_{awaitable_} {}

    T get() override {
        return sync_await(std::move(awaitable_));
    }

    void setExecutor(std::shared_ptr<DrivenExecutor> exec) override {
        this->exec_ = std::move(exec);
    }

    void setCallback(std::function<void(T)> cb) override {
        async_await(this->exec_, std::move(awaitable_), std::move(cb));
    }
 
    std::shared_ptr<DrivenExecutor> getExecutor() override {
        return this->exec_;
    }

    bool isAwaitable() override {
        return true;
    }

    VirtualAwaitable& getAwaitable() override {
        return vAwaitable_;
    }

    AwaitableT awaitable_;
    VirtualAwaitableImpl<AwaitableT> vAwaitable_;
};

template<class T>
struct ValueCore : CoreBase<T> {
    T get() override {
        std::lock_guard<std::mutex> lg{mtx_};
        if(!value_) {
            throw std::logic_error("Value not set on promise");
        }
        return *std::move(value_);
    }

    void set_value(T value) {
        std::lock_guard<std::mutex> lg{mtx_};
        if(callback_) {
            if(!this->exec_) {
                throw std::logic_error("Callback set but executor not set.");
            }
            this->exec_->execute([cb = std::move(callback_), val = std::move(value)](){
                cb(std::move(val));
            });
        } else {
            value_ = std::move(value);
        }
    }

    void setExecutor(std::shared_ptr<DrivenExecutor> exec) override {         
        std::lock_guard<std::mutex> lg{mtx_};
        this->exec_ = std::move(exec);
    }
 
    std::shared_ptr<DrivenExecutor> getExecutor() override {
        std::lock_guard<std::mutex> lg{mtx_};
        return this->exec_;
    }

    void setCallback(std::function<void(T)> callback) override {
        std::lock_guard<std::mutex> lg{mtx_};
        if(!this->exec_) {
            throw std::logic_error("Setting a callback without an executor is invalid");
        }
        if(value_) {
            // Promise already satisfied so enqueue a callback that captures the value
            this->exec_->execute([val = *std::move(value_), cb = std::move(callback)](){
                    cb(std::move(val));
                });
        } else {
            // Promise not already satisfied to just store the callback
            callback_ = std::move(callback);
        }
    }

    bool isAwaitable() override {
        return false;
    }

    VirtualAwaitable& getAwaitable() override {
        // TODO: Maybe this does make sense and we can construct one by value?
        throw std::logic_error("makes no sense for this path");
    }

    std::mutex mtx_;
    std::optional<T> value_;
    std::function<void(T)> callback_;
};

template<class T>
class Future;

template<class T>
Future<T> make_future(T);
template<class T, class AwaitableT>
Future<T> make_awaitable_future(AwaitableT);


template<class T>
class Promise {
public:
    Promise() : core_{std::make_shared<ValueCore<T>>()} {
    }

    void set_value(T value) {
        core_->set_value(std::move(value));
    }

    Future<T> get_future() {
        return Future<T>{std::shared_ptr<CoreBase<T>>{core_}};
    }

private:
    std::shared_ptr<ValueCore<T>> core_;
};

template<class T>
class ContinuableFuture;

template<class T>
class Future {
public:
    T get() {
        if(value_) {
            return *std::move(value_);
        }
        if(core_) {
            return core_->get();
        }
        throw std::logic_error("Incomplete future");
    }
    
    // TODO: via should check if the core is an Awaitable or SemiAwaitable, if the latter 
    // the via call should forward to the core.
    ContinuableFuture<T> via(std::shared_ptr<DrivenExecutor>);

private:
    // Construct a future from a core
    Future(std::shared_ptr<CoreBase<T>> core) : core_(std::move(core)) {
    }

    // Construct a ready future from a value
    Future(T value) : value_{std::move(value)} {}

    template<class FriendT> friend 
    Future<FriendT> make_future(FriendT);
    template<class FriendT, class AwaitableT> friend
    Future<FriendT> make_awaitable_future(AwaitableT);
    friend class Promise<T>;

    std::optional<T> value_;
    std::shared_ptr<CoreBase<T>> core_;
};

template<class T>
class ContinuableFuture {
public:
    T get() {
        if(core_) {
            return core_->get();
        }
        throw std::logic_error("Incomplete future");
    }

    // TODO: Generalise type
    ContinuableFuture<T> then(std::function<T(T)> callback) {
        // This is the simple future/promise pair for a continuable core
        Promise<T> prom;
        auto f = prom.get_future().via(core_->getExecutor());
        core_->setCallback([p = std::move(prom), cb = std::move(callback)](T val) mutable {
                auto v = cb(std::move(val));
                p.set_value(std::move(v));
            });
        return f;
    }

    // TODO: Generalise type
    template<class F>
    ContinuableFuture<T> thenAwait(F&& callback) {
        if(core_->isAwaitable()) {
            // TODO: Actual awaitable used here could differ based on executor pair
            // or a single ThenAwaitable could handle that (ie do callback directly if
            // on same executor).
            // TODO: AsyncAwaitable is fixed to int
            using AA = MyLibrary::AsyncAwaitable; 
            auto coroutine = [core = this->core_, cb = std::forward<F>(callback)]() -> AA {
                auto& aw = core->getAwaitable();
                auto v = co_await aw; 
                co_return cb(v);
            };
            return make_awaitable_future<T>(coroutine);
        } else {
            return then(std::forward<F>(callback));
        }
    }

private:
    ContinuableFuture(std::shared_ptr<CoreBase<T>> core) : core_{std::move(core)} { 
    }

    friend class Future<T>;
    std::shared_ptr<CoreBase<T>> core_;
};

template<class T>
ContinuableFuture<T> Future<T>::via(std::shared_ptr<DrivenExecutor> exec) {
    if(value_) {
       throw std::logic_error("Attempted to convert an immediate future to continuable.");
    } else {
        core_->setExecutor(std::move(exec));
        return ContinuableFuture<T>{core_};
    }
}

template<class T>
Future<T> make_future(T value) {
    return Future(std::move(value));
}

template<class T, class AwaitableT>
Future<T> make_awaitable_future(AwaitableT awaitable) {
    auto core = std::make_shared<AwaitableCore<T, AwaitableT>>(std::move(awaitable));
    return Future<T>(std::move(core));
}

