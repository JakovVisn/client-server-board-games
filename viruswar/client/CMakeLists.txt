cmake_minimum_required(VERSION 3.16)
project(VirusWarClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_add_executable(VirusWarClient
    main.cpp
    boardwidget.h boardwidget.cpp
    connectdialog.h connectdialog.cpp
    gamestate.h gamestate.cpp
    mainwindow.h mainwindow.cpp
    networkclient.h networkclient.cpp
    resources.qrc
)

target_link_libraries(VirusWarClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)
