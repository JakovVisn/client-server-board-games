cmake_minimum_required(VERSION 3.16)
project(ClientServerBoardGames)

enable_testing()

add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(viruswar/client)
add_subdirectory(viruswar/random-bot)
add_subdirectory(tests)
