cmake_minimum_required(VERSION 3.20)
project(cameraphone-obs VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../protocol)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(cameraphone-obs SHARED
    src/plugin-main.cpp
    src/cameraphone-source.cpp
    src/discovery-manager.cpp
    src/stream-client.cpp
    src/media-decoder.cpp
)

if(WIN32)
    target_link_libraries(cameraphone-obs PRIVATE ws2_32)
endif()

enable_testing()
add_executable(test_client_and_discovery
    tests/test_client_and_discovery.cpp
    src/discovery-manager.cpp
    src/stream-client.cpp
)
if(WIN32)
    target_link_libraries(test_client_and_discovery PRIVATE ws2_32)
endif()
add_test(NAME test_client_and_discovery COMMAND test_client_and_discovery)
