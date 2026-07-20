cmake_minimum_required(VERSION 3.16)

# 项目名称；使用 C++ 语言
project(MemoryPoolProject LANGUAGES CXX)

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找线程库，因为 tests.cpp 使用了 std::thread
find_package(Threads REQUIRED)

# 生成可执行程序
# 注意：这里列出所有需要参与编译的 cpp 文件
add_executable(MemoryPoolProject
    src/MemoryPool.cpp
    test/tests.cpp
)

# 告诉编译器去 include 目录中查找头文件
target_include_directories(MemoryPoolProject PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接线程库
target_link_libraries(MemoryPoolProject PRIVATE
    Threads::Threads
)

# 开启 GCC / MinGW 常用警告
target_compile_options(MemoryPoolProject PRIVATE
    -Wall
    -Wextra
)