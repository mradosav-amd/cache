include(CTest)
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    DOWNLOAD_EXTRACT_TIMESTAMP 1
)
FetchContent_MakeAvailable(googletest)

# Coverage
option(ENABLE_COVERAGE "Enable coverage reporting" ON)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(COVERAGE_FLAGS "--coverage")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COVERAGE_FLAGS}")
    endif()
endif()

set(UNIT_TEST_SOURCES
    test_type_registry.cpp
    test_cacheable.cpp
    test_cache_storage.cpp
    test_storage_parser.cpp
    test_flush_worker.cpp
    test_cache_integration.cpp
)

add_executable(caching-lib-tests ${UNIT_TEST_SOURCES})

target_link_libraries(caching-lib-tests
    CachingLib::caching-lib
    GTest::gtest_main
    GTest::gmock
)

# Coverage
if(ENABLE_COVERAGE AND(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    target_compile_options(caching-lib-tests PRIVATE ${COVERAGE_FLAGS})
    target_link_options(caching-lib-tests PRIVATE ${COVERAGE_FLAGS})
endif()

include(GoogleTest)
gtest_discover_tests(caching-lib-tests)

# Coverage
if(ENABLE_COVERAGE)
    find_program(GCOV_PATH gcov)
    find_program(GCOVR_PATH gcovr)

    if(GCOV_PATH AND GCOVR_PATH)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${GCOVR_PATH}
            --root ${CMAKE_SOURCE_DIR}
            --exclude '.*tests.*'
            --exclude '/usr/.*'
            --html --html-details
            --output coverage.html
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report with gcovr"
        )
    endif()
endif()




