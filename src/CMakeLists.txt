
add_library(caching-lib INTERFACE)
add_library(CachingLib::caching-lib ALIAS caching-lib)

set(CACHING_LIBRARY_HEADER_FILES
    cache_type_traits.hpp
    cacheable.hpp
    cache_storage.hpp
    storage_parser.hpp
    type_registry.hpp
)

target_sources(caching-lib INTERFACE
    $<BUILD_INTERFACE:${CACHING_LIBRARY_HEADERS}>
)

target_include_directories(caching-lib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/caching-lib>
)

target_compile_features(caching-lib INTERFACE cxx_std_17)

target_compile_options(caching-lib INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
)
