# probe_registers uses POSIX sockets (arpa/inet.h, unistd.h) — Linux/macOS only
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(probe_registers probe_registers.c)
    target_link_libraries(probe_registers PRIVATE windmi_utils)
    target_compile_options(probe_registers PRIVATE -Wno-unused-result)
endif()
