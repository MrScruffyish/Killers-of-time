cmake_minimum_required(VERSION 3.21)
project(KillersOfTime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Widgets Test)

option(KOT_BUILD_TESTS "Build unit tests" ON)

# Shared warning flags, linked by every target.
add_library(kot_warnings INTERFACE)
target_compile_options(kot_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)

# All code includes headers relative to src/, e.g. #include "core/util/Rng.h"
set(KOT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(src)

if(KOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Install and packages (cmake --install build --prefix /usr/local; cpack in build/)
include(GNUInstallDirs)
install(TARGETS kot_client kot_server kot_editor kot_graph kot_balance kot_balance_gui kot_mapgen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/killers-of-time/data)
install(FILES packaging/killers-of-time.desktop packaging/killers-of-time-editor.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/killers-of-time.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES README.md PROJECT.md docs/protocol.md docs/data-format.md docs/save-format.md docs/tools.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

set(CPACK_PACKAGE_NAME "killers-of-time")
set(CPACK_PACKAGE_VENDOR "Gunnar Lindholm")
set(CPACK_PACKAGE_CONTACT "Gunnar Lindholm")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Turn-based fantasy strategy game (heroes, towns, magic, hex battles)")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "DEB;TGZ")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6core6 (>= 6.4), libqt6gui6 (>= 6.4), libqt6widgets6 (>= 6.4), libqt6network6 (>= 6.4)")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
include(CPack)
