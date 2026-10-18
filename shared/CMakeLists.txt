cmake_minimum_required(VERSION 3.16)
project(SharedLog LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

# One log format for the server, the client and the random bot
add_library(SharedLog STATIC
    logging.h logging.cpp
)
target_include_directories(SharedLog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SharedLog PUBLIC Qt6::Core)
