# Command line tools for content makers (no GUI).
add_executable(kot_graph kot_graph.cpp)
target_link_libraries(kot_graph PRIVATE kot_core kot_warnings)
