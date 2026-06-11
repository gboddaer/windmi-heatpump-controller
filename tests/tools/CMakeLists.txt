add_executable(probe_registers probe_registers.c)
target_link_libraries(probe_registers PRIVATE windmi_utils)
target_compile_options(probe_registers PRIVATE -Wno-unused-result)
