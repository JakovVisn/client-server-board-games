cmake_minimum_required(VERSION 3.16)
project(GameServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── Qt-free game logic library ──────────────────────────────────────────────
add_library(VirusWarCore STATIC
    abstract_game.h
    viruswar_field.h viruswar_field.cpp
    virus_war_game.h virus_war_game.cpp
)
target_compile_features(VirusWarCore PUBLIC cxx_std_17)
target_include_directories(VirusWarCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── Qt server executable ─────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_add_executable(GameServer
    main.cpp
    auth_manager.h auth_manager.cpp
    client.h client.cpp
    game_session.h game_session.cpp
    server_constants.h
    server.h server.cpp
)
target_link_libraries(GameServer PRIVATE VirusWarCore Qt6::Core Qt6::Network)

# ── Optional pybind11 Python extension ──────────────────────────────────────
set(PYBIND11_FINDPYTHON ON)  # use modern FindPython, respects Python3_EXECUTABLE
find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(viruswar_sim bindings.cpp)
    target_link_libraries(viruswar_sim PRIVATE VirusWarCore)
    message(STATUS "pybind11 found — building viruswar_sim")
else()
    message(STATUS "pybind11 not found — skipping viruswar_sim")
endif()
