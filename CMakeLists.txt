cmake_minimum_required(VERSION 3.10)


project(cache
    VERSION 1.0
    DESCRIPTION "Caching mechanism"
    LANGUAGES CXX
)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cache
    benchmark.hpp
   cache_storage.hpp
   main.cpp
)