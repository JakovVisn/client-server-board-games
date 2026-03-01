cmake_minimum_required(VERSION 3.16)
project(GameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_add_executable(GameServer
    main.cpp
    client.h client.cpp
    server_constants.h
    server.h server.cpp
)

target_link_libraries(GameServer PRIVATE Qt6::Core Qt6::Network)
