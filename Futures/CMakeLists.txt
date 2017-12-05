cmake_minimum_required(VERSION 3.0)
project(futures)

add_executable(simple_coroutine src/SimpleCoroutine.cpp)
target_compile_options(simple_coroutine PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17)

add_executable(executor_coroutine src/ExecutorCoroutine.cpp src/MyAsyncLibrary.cpp src/Executor.h src/MyAsyncLibrary.h)
target_compile_options(executor_coroutine PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17)

add_executable(future_test src/FutureTest.cpp src/MyAsyncLibrary.cpp src/Future.cpp src/Executor.h src/MyAsyncLibrary.h src/Future.h)
target_compile_options(future_test PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17)

