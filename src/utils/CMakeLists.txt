add_library(windmi_utils STATIC
    Config.cpp
    JsonHelpers.cpp
    SpscQueue.cpp
    Logger.cpp
    LoggerC.cpp
    Platform.cpp
)
target_include_directories(windmi_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
