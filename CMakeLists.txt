cmake_minimum_required(VERSION 3.16)

project(MemoryPoolProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 没指定构建类型时按 Release 编译：性能对比才有意义
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

enable_testing()

# 公共库：内存池实现
add_library(memorypool STATIC src/MemoryPool.cpp)
target_include_directories(memorypool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(memorypool PRIVATE -Wall -Wextra)

# 性能对比：new/delete vs 内存池
add_executable(MemoryPoolProject test/tests.cpp)
target_link_libraries(MemoryPoolProject PRIVATE memorypool Threads::Threads)
target_compile_options(MemoryPoolProject PRIVATE -Wall -Wextra)

# 正确性测试：可写、不重叠、可复用、并发不重复分配
add_executable(correctness_test test/correctness.cpp)
target_link_libraries(correctness_test PRIVATE memorypool Threads::Threads)
target_compile_options(correctness_test PRIVATE -Wall -Wextra)

add_test(NAME correctness_test COMMAND correctness_test)
set_tests_properties(correctness_test PROPERTIES TIMEOUT 120)
