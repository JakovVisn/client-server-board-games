cmake_minimum_required(VERSION 3.16)
project(VirusWarRandomBot VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(VirusWarRandomBot
    main.cpp
    botclient.cpp
    botlogic.cpp
)

target_link_libraries(VirusWarRandomBot PRIVATE
    Qt6::Core
    Qt6::Network
)
