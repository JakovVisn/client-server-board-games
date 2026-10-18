cmake_minimum_required(VERSION 3.16)
project(Tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

# Game rules (VirusWarCore), time controls, matchmaking and account storage
add_executable(test_server
    test_server.cpp
    ${PROJECT_SOURCE_DIR}/../server/auth_manager.cpp
    ${PROJECT_SOURCE_DIR}/../server/time_control.cpp
    ${PROJECT_SOURCE_DIR}/../server/matchmaking.cpp
)
target_include_directories(test_server PRIVATE ${PROJECT_SOURCE_DIR}/../server)
target_link_libraries(test_server PRIVATE VirusWarCore SharedLog Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME test_server COMMAND test_server)

# Client protocol parsing and game clock
add_executable(test_client
    test_client.cpp
    ${PROJECT_SOURCE_DIR}/../viruswar/client/protocol.cpp
    ${PROJECT_SOURCE_DIR}/../viruswar/client/gameclock.cpp
)
target_include_directories(test_client PRIVATE ${PROJECT_SOURCE_DIR}/../viruswar/client)
target_link_libraries(test_client PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_client COMMAND test_client)
