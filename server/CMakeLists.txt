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
# Also linked into the viruswar_sim shared module, which needs -fPIC on Linux
set_target_properties(VirusWarCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(VirusWarCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── Qt server executable ─────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_add_executable(GameServer
    main.cpp
    auth_manager.h auth_manager.cpp
    client.h client.cpp
    game_session.h game_session.cpp
    matchmaking.h matchmaking.cpp
    server_constants.h
    server.h server.cpp
    time_control.h time_control.cpp
)
target_link_libraries(GameServer PRIVATE VirusWarCore SharedLog Qt6::Core Qt6::Network)

# ── Optional pybind11 Python extension ──────────────────────────────────────
set(PYBIND11_FINDPYTHON ON)  # use modern FindPython, respects Python_EXECUTABLE
# viruswar_sim is built for the bots' shared venv, with the venv's pybind11
set(VIRUSWAR_VENV_PYTHON "${CMAKE_SOURCE_DIR}/viruswar/.venv/bin/python")
if(EXISTS "${VIRUSWAR_VENV_PYTHON}")
    set(Python_EXECUTABLE "${VIRUSWAR_VENV_PYTHON}" CACHE FILEPATH "Python for viruswar_sim" FORCE)
    execute_process(COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
                    OUTPUT_VARIABLE _pybind11_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(pybind11_DIR "${_pybind11_dir}" CACHE PATH "pybind11 CMake config" FORCE)
endif()
find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(viruswar_sim bindings.cpp)
    target_link_libraries(viruswar_sim PRIVATE VirusWarCore)
    message(STATUS "pybind11 found — building viruswar_sim for ${Python_EXECUTABLE}")
else()
    # Loud: the bots would silently keep using an old viruswar_sim build
    message(WARNING "pybind11 not found — skipping viruswar_sim. "
                    "Run: viruswar/.venv/bin/pip install -r viruswar/requirements.txt")
endif()
