#include <iostream>
#include <thread>
#include <experimental/coroutine>
#include "AsyncAwait.h"
#include "SimpleAwaitable.h"
#include "MyAsyncLibrary.h"

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

MyLibrary::AsyncAwaitable asyncAdder(int value) {
    where("asyncAdder");
    co_return (value + 4);
}

MyLibrary::AsyncAwaitable asyncEntryPoint(int value) {
    where("asyncEntryPoint");
    auto v = co_await(asyncAdder(value));
    co_return (value + 6);
}

int main() {
    MyLibrary::init();

    auto exec = std::make_shared<DrivenExecutor>();
    std::thread driver([&](){exec->run();});
    std::atomic<int> val{0};

    async_await(exec, entryPoint(1), [&](int value) {
            val = value;
        });
    while(val == 0) {}
    std::cerr << "Value with simple awaitable: " << val << "\n";
    
    val = 0;
    async_await(exec, asyncEntryPoint(2), [&](int value) { val = value; });
    while(val == 0) {}
    std::cerr << "Value with async awaitable: " << val << "\n";
    

    exec->terminate();
    driver.join();
    MyLibrary::shutdown();
 
    return 0;
}
