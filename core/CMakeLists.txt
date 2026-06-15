add_library(automw_core STATIC
    automw_shm.c
    automw_mq.c
)

target_include_directories(automw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# rt = POSIX real-time extensions (shm_open, mq_open)
# pthread = needed by some POSIX semaphore implementations
target_link_libraries(automw_core PUBLIC rt pthread)