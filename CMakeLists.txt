cmake_minimum_required(VERSION 3.16)

project(slayer-editor VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)
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
    src/NewFileDialog.cpp src/NewFileDialog.h
    src/ThemeEditor.cpp src/ThemeEditor.h
    src/DapClient.cpp src/DapClient.h
    src/DebugSession.cpp src/DebugSession.h
    src/DebugPanel.cpp src/DebugPanel.h
    src/HelpDialog.cpp src/HelpDialog.h
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

# Traduções: os .qm vão embutidos no binário, em :/i18n.
qt_add_translations(slayer-editor
    TS_FILES translations/slayer-editor_pt_BR.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -locations none -no-obsolete)

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
    add_executable(tst_help tests/tst_help.cpp src/HelpDialog.cpp src/Icons.cpp resources/resources.qrc)
    target_link_libraries(tst_help PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME help COMMAND tst_help)
    add_executable(tst_highlight tests/tst_highlight.cpp src/SyntaxHighlighter.cpp src/Language.cpp src/Theme.cpp)
    target_link_libraries(tst_highlight PRIVATE Qt6::Gui Qt6::Test)
    add_test(NAME highlight COMMAND tst_highlight)
    add_executable(tst_themes tests/tst_themes.cpp src/Theme.cpp)
    target_link_libraries(tst_themes PRIVATE Qt6::Gui Qt6::Test)
    add_test(NAME themes COMMAND tst_themes)
    add_executable(tst_languages tests/tst_languages.cpp src/Language.cpp src/Settings.cpp src/LspManager.cpp
                   src/LspClient.cpp src/Document.cpp src/Folding.cpp src/SyntaxHighlighter.cpp src/Theme.cpp)
    target_link_libraries(tst_languages PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME languages COMMAND tst_languages)
    add_executable(tst_settings tests/tst_settings.cpp src/SettingsDialog.cpp src/ThemeEditor.cpp src/Settings.cpp
                   src/LspManager.cpp src/LspClient.cpp src/Language.cpp src/Theme.cpp src/Icons.cpp
                   src/SyntaxHighlighter.cpp src/Document.cpp src/Folding.cpp resources/resources.qrc)
    target_link_libraries(tst_settings PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME settings COMMAND tst_settings)
    add_executable(tst_viewspace tests/tst_viewspace.cpp src/ViewSpace.cpp src/CodeEditor.cpp src/Document.cpp
                   src/Folding.cpp src/SyntaxHighlighter.cpp src/Language.cpp src/Theme.cpp src/Icons.cpp
                   resources/resources.qrc)
    target_link_libraries(tst_viewspace PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME viewspace COMMAND tst_viewspace)
    add_executable(tst_debug tests/tst_debug.cpp src/DebugSession.cpp src/DapClient.cpp src/LspClient.cpp
                   src/TerminalDock.cpp src/Theme.cpp src/Icons.cpp src/Settings.cpp src/LspManager.cpp
                   src/Document.cpp src/Folding.cpp src/SyntaxHighlighter.cpp src/Language.cpp
                   resources/resources.qrc)
    target_link_libraries(tst_debug PRIVATE Qt6::Widgets Qt6::Test qtermwidget6)
    add_test(NAME debug COMMAND tst_debug)
endif()
