
cmake_minimum_required(VERSION 3.25)
project(automw C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Werror)

add_subdirectory(core)
add_subdirectory(someip)
add_subdirectory(demo)


