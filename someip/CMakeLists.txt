add_library(automw_someip STATIC
    src/someip.c
    src/someip_sd.c
)

target_include_directories(automw_someip PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(automw_someip PUBLIC automw_core)
