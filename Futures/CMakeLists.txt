cmake_minimum_required(VERSION 3.0)
project(futures)

add_library(asynclib src/MyAsyncLibrary.cpp src/Executor.h src/MyAsyncLibrary.h)
target_compile_options(asynclib PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17 -g)

add_executable(simple_test src/SimpleTest.cpp)
target_link_libraries(simple_test asynclib)
target_compile_options(simple_test PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17 -g)

add_executable(async_await_test src/AsyncAwaitTest.cpp src/Executor.h src/AsyncAwait.h)
target_link_libraries(async_await_test asynclib)
target_compile_options(async_await_test PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17 -g)


add_executable(executor_test src/ExecutorTest.cpp src/Executor.h)
target_link_libraries(executor_test asynclib)
target_compile_options(executor_test PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17 -g)

add_executable(future_test src/FutureTest.cpp src/Future.cpp src/Executor.h src/Future.h)
target_link_libraries(future_test asynclib)
target_compile_options(future_test PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17 -g)

