cmake_minimum_required(VERSION 3.16)

project(slayer-editor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
find_package(qtermwidget6 REQUIRED)

add_executable(slayer-editor
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/CodeEditor.cpp src/CodeEditor.h
    src/SyntaxHighlighter.cpp src/SyntaxHighlighter.h
    src/Language.cpp src/Language.h
    src/Theme.cpp src/Theme.h
    src/TerminalDock.cpp src/TerminalDock.h
    src/FindBar.cpp src/FindBar.h
    src/Icons.cpp src/Icons.h
    resources/resources.qrc
)

target_compile_definitions(slayer-editor PRIVATE
    SLAYER_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(slayer-editor PRIVATE Qt6::Widgets qtermwidget6)

install(TARGETS slayer-editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/slayer-editor.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/io.github.wyllianbs.SlayerEditor.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
foreach(size 16 22 24 32 48 64 128 256 512)
    install(FILES resources/icons/hicolor/${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME slayer-editor.png)
endforeach()
