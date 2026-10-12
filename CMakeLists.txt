cmake_minimum_required(VERSION 3.21)
project(Scrabble VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets)

# The word list is compiled into a minimized graph at build time and embedded in the binary.
add_executable(mkdict tools/mkdict.cpp src/core/dawg_builder.cpp)
target_include_directories(mkdict PRIVATE src)
set(DAWG_FILE ${CMAKE_CURRENT_BINARY_DIR}/enable.dawg)
add_custom_command(
    OUTPUT ${DAWG_FILE}
    COMMAND mkdict ${CMAKE_CURRENT_SOURCE_DIR}/data/enable1.txt ${DAWG_FILE}
    DEPENDS mkdict ${CMAKE_CURRENT_SOURCE_DIR}/data/enable1.txt
    COMMENT "Building the word graph")

add_library(scrabble_core
    src/core/rules.cpp
    src/core/dawg.cpp
    src/core/dawg_builder.cpp
    src/core/game.cpp
    src/core/ai.cpp
    src/core/hints.cpp
    src/core/storage.cpp
    src/core/statistics.cpp
    src/core/theme.cpp
    src/core/theme_source.cpp
)
target_include_directories(scrabble_core PUBLIC src)
target_link_libraries(scrabble_core PUBLIC Qt6::Core Qt6::Gui)
qt_add_resources(scrabble_core dictionary
    PREFIX /dict
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${DAWG_FILE}
    OPTIONS -no-compress)

add_library(scrabble_ui
    src/ui/tile_painter.cpp
    src/ui/icons.cpp
    src/ui/board_view.cpp
    src/ui/rack_view.cpp
    src/ui/modal_backdrop.cpp
    src/ui/modals.cpp
    src/ui/theme_watcher.cpp
    src/ui/window.cpp
)
target_link_libraries(scrabble_ui PUBLIC scrabble_core Qt6::Widgets)

add_executable(scrabble src/main.cpp src/cli.cpp)
target_link_libraries(scrabble PRIVATE scrabble_ui)

foreach(target mkdict scrabble_core scrabble_ui scrabble)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    foreach(name dawg game ai hints theme widgets window)
        add_executable(${name}_tests tests/${name}_tests.cpp)
        target_link_libraries(${name}_tests PRIVATE scrabble_ui Qt6::Test)
        add_test(NAME ${name} COMMAND ${name}_tests)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    add_test(NAME cli COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.sh $<TARGET_FILE:scrabble>)
    add_test(NAME install COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_test.sh
        $<TARGET_FILE_DIR:scrabble> ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-desktop.sh)
endif()

include(GNUInstallDirs)
install(TARGETS scrabble RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/io.github.scrabble.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/io.github.scrabble.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
