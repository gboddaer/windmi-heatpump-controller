add_library(windmi_web STATIC
    WebServer.cpp
)
target_include_directories(windmi_web PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(windmi_web PUBLIC mongoose windmi_utils)
