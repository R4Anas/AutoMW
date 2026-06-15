foreach(target publisher subscriber server client)
    add_executable(${target} ${target}.c)
    target_link_libraries(${target} PRIVATE automw_core)
endforeach()