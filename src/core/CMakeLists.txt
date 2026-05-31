add_library(windmi_core STATIC
    ControlLoop.cpp
    StatusMonitor.cpp
)
target_include_directories(windmi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(windmi_core PUBLIC mongoose)
