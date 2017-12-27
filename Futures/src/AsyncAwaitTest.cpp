#include <iostream>
#include <thread>
#include <experimental/coroutine>
#include "AsyncAwait.h"
#include "SimpleAwaitable.h"

ZeroOverheadAwaitable adder(int value) {
    co_return (value + 3);
}

ZeroOverheadAwaitable entryPoint(int value) {
    auto v = co_await(adder(value));
    auto v2 = co_await(adder(value+5));
    co_return v + v2;
}

// TODO: Have an async version once the executor is passed to the asyncawait awaitable

int main() {
    auto aw = entryPoint(1);
    auto exec = std::make_shared<DrivenExecutor>();
    std::thread driver([&](){exec->run();});
    std::atomic<int> val{0};
    async_await(exec, std::move(aw), [&](int value) {
            val = value;
        });
    while(val == 0) {}

    std::cerr << "Value: " << val << "\n";
    exec->terminate();
    driver.join();
    return 0;
}
