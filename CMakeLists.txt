
cmake_minimum_required(VERSION 3.28)
project(automw C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings — good habit from day one
add_compile_options(-Wall -Wextra -Werror)

add_subdirectory(core)
add_subdirectory(someip)
add_subdirectory(demo)


