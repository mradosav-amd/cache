cmake_minimum_required(VERSION 3.10)

project(caching-mechanism
    VERSION 1.0
    DESCRIPTION "Caching mechanism"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)

add_executable(cache-example
    main.cpp
)

target_link_libraries(cache-example INTERFACE cache-lib)