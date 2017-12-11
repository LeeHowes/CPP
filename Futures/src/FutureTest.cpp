#include <iostream>
#include <thread>
#include <experimental/coroutine>

#include "Executor.h"
#include "SimpleAwaitable.h"
#include "MyAsyncLibrary.h"
#include "Future.h"

ZeroOverheadAwaitable adder(int value) {
    where("adder");
    co_return (value + 3);
}

MyLibrary::AsyncAwaitable asyncEntryPoint(int value) {
    where("asyncEntryPoint");
    auto v = co_await(adder(value));
    auto v2 = co_await(adder(value+5));
    co_return v + v2;
}

int main() {
    // Temporarily create this globally
    where("main()");
    MyLibrary::init();
   
    {
        where("Before make with value");
        auto f = make_future(3);
        where("Before get");
        std::cout << "Future: " << f.get() << "\n";
    }
    
    {
        where("Before make awaitable");
        auto f = make_awaitable_future<int>(adder(3));
        where("Before awaitable get");
        std::cout << "ZeroOverheadAwaitableFuture: " << f.get() << "\n";
    }

    {
        where("Before make async");
        auto f = make_awaitable_future<int>(asyncEntryPoint(3));
        where("Before async get");
        std::cout << "AsyncAwaitableFuture: " << f.get() << "\n";
    }

    {
        where("Before make promise");
        Promise<int> p;
        auto f = p.get_future();
        p.set_value(5);
        std::cout << "Promise future: " << f.get() << "\n";
    }

    {
        where("Via and then");
        Promise<int> p;
        std::cerr << "Promise\n";
        auto f = p.get_future();
        std::cerr << "Future\n";
        auto exec = std::make_shared<DrivenExecutor>(); 
        std::atomic<int> val = 0;
        std::cerr << "Atomic\n";
        auto cf = f.via(exec).then([&](int oldVal){val = oldVal; std::cout << "Running\n"; return 2;});
        std::cerr << "via and then\n";
        auto t = std::thread([&](){
                exec->run();
            });
        std::cout << "Val before\n";
        p.set_value(7);
        while(val == 0) {}
        std::cout << "Val: " << val << " and returned " << cf.get() << "\n";
    }

    MyLibrary::shutdown();
    std::cout << "END\n";


    return 0;
}
