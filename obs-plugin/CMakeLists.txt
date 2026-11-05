cmake_minimum_required(VERSION 3.20)
project(cameraphone VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../protocol)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(cameraphone SHARED
    src/plugin-main.cpp
    src/cameraphone-source.cpp
    src/discovery-manager.cpp
    src/stream-client.cpp
    src/media-decoder.cpp
)
set_target_properties(cameraphone PROPERTIES PREFIX "" OUTPUT_NAME "cameraphone")

if(WIN32)
    target_link_libraries(cameraphone PRIVATE ws2_32)
    target_link_options(cameraphone PRIVATE -static)
endif()

enable_testing()
add_executable(test_client_and_discovery
    tests/test_client_and_discovery.cpp
    src/discovery-manager.cpp
    src/stream-client.cpp
)
if(WIN32)
    target_link_libraries(test_client_and_discovery PRIVATE ws2_32)
    target_link_options(test_client_and_discovery PRIVATE -static)
endif()
add_test(NAME test_client_and_discovery COMMAND test_client_and_discovery)

add_executable(test_live_emulator_to_obs
    tests/test_live_emulator_to_obs.cpp
    src/discovery-manager.cpp
    src/stream-client.cpp
    src/media-decoder.cpp
)
if(WIN32)
    target_link_libraries(test_live_emulator_to_obs PRIVATE ws2_32)
    target_link_options(test_live_emulator_to_obs PRIVATE -static)
endif()
