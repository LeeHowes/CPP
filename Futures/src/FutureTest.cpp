#include <iostream>
#include <thread>
#include <experimental/coroutine>

#include "Executor.h"
#include "SimpleAwaitable.h"
#include "MyAsyncLibrary.h"
#include "Future.h"

#if 0
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
#endif

int main() {
    // Temporarily create this globally
    where("main()");
    MyLibrary::init();
   
    {
        auto f = make_future(3);
        std::cout << "Future: " << f.get() << "\n";
    }
    


    MyLibrary::shutdown();
    std::cout << "END\n";


    return 0;
}
