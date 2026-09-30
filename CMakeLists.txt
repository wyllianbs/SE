cmake_minimum_required(VERSION 3.16)

project(slayer-editor VERSION 0.2.0 LANGUAGES CXX)

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
    src/LspClient.cpp src/LspClient.h
    src/LspManager.cpp src/LspManager.h
    src/Folding.cpp src/Folding.h
    src/Document.cpp src/Document.h
    src/ViewSpace.cpp src/ViewSpace.h
    src/Settings.cpp src/Settings.h
    src/SettingsDialog.cpp src/SettingsDialog.h
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

# Testes (ctest). Desligue com -DBUILD_TESTING=OFF.
option(BUILD_TESTING "Build the unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test Gui)
    add_executable(tst_folding tests/tst_folding.cpp src/Folding.cpp)
    target_link_libraries(tst_folding PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME folding COMMAND tst_folding)
    add_executable(tst_document tests/tst_document.cpp src/Document.cpp src/Folding.cpp
                   src/SyntaxHighlighter.cpp src/Language.cpp src/Theme.cpp)
    target_link_libraries(tst_document PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME document COMMAND tst_document)
    add_executable(tst_lsp tests/tst_lsp.cpp src/Document.cpp src/Folding.cpp src/SyntaxHighlighter.cpp
                   src/Language.cpp src/Theme.cpp src/Settings.cpp src/LspManager.cpp src/LspClient.cpp)
    target_link_libraries(tst_lsp PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME lsp COMMAND tst_lsp)
endif()
