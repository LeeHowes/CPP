cmake_minimum_required(VERSION 3.0)
project(futures)

add_executable(simple_coroutine src/SimpleCoroutine.cpp)
target_compile_options(simple_coroutine PUBLIC -stdlib=libc++ -fcoroutines-ts -std=c++17)
