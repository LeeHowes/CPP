#pragma once

#include <optional>

#include "SimpleAwaitable.h"

template<class T>
struct CoreBase {
    virtual ~CoreBase() {}
    virtual T get() = 0;
};

// This core wraps an arbitrary Awaitable into a future without a promise involved
template<class T, class AwaitableT>
struct AwaitableCore : CoreBase<T> {
    AwaitableCore(AwaitableT&& awaitable) : awaitable_(std::move(awaitable)) {}
    virtual T get() {
        return sync_await(std::move(awaitable_));
    }

    AwaitableT awaitable_;
};

// TODO: This is the core that would be shared with a promise
template<class T>
struct ValueCore : CoreBase<T> {
};

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

    std::optional<T> value_;
    std::shared_ptr<CoreBase<T>> core_;
};

template<class T>
Future<T> make_future(T value) {
    return Future(std::move(value));
}

template<class T, class AwaitableT>
Future<T> make_awaitable_future(AwaitableT awaitable) {
    auto core = std::make_shared<AwaitableCore<T, AwaitableT>>(std::move(awaitable));
    return Future<T>(std::move(core));
}

