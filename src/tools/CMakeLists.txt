# Command line tools (and a tweaking GUI) for content makers.
add_library(kot_tools_lib STATIC BalanceCalc.h BalanceCalc.cpp)
target_include_directories(kot_tools_lib PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_tools_lib PUBLIC kot_core PRIVATE kot_warnings)

add_executable(kot_graph kot_graph.cpp)
target_link_libraries(kot_graph PRIVATE kot_core kot_warnings)

add_executable(kot_balance kot_balance.cpp)
target_link_libraries(kot_balance PRIVATE kot_tools_lib kot_warnings)

add_executable(kot_balance_gui kot_balance_gui.cpp)
target_link_libraries(kot_balance_gui PRIVATE kot_tools_lib Qt6::Widgets kot_warnings)

add_executable(kot_mapgen kot_mapgen.cpp)
target_link_libraries(kot_mapgen PRIVATE kot_core kot_warnings)

add_executable(kot_aiplay kot_aiplay.cpp)
target_link_libraries(kot_aiplay PRIVATE kot_server_lib kot_warnings)

add_executable(kot_artexport kot_artexport.cpp)
target_link_libraries(kot_artexport PRIVATE kot_artgen kot_warnings)

add_executable(kot_saveview kot_saveview.cpp)
target_link_libraries(kot_saveview PRIVATE kot_core kot_warnings)

add_executable(kot_viewbench kot_viewbench.cpp)
find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
target_link_libraries(kot_viewbench PRIVATE kot_client_lib Qt6::OpenGLWidgets kot_warnings)
