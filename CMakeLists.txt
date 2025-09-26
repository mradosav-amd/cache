cmake_minimum_required(VERSION 3.10)


project(cache
    VERSION 1.0
    DESCRIPTION "Caching mechanism"
    LANGUAGES CXX
)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cache
   cacheable.hpp
   cache_storage.hpp
   storage_parser.hpp
   main.cpp
)