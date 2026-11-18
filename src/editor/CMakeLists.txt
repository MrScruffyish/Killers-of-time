# kot_editor: the scenario / map editor.
add_executable(kot_editor main.cpp)
target_link_libraries(kot_editor PRIVATE kot_core Qt6::Widgets kot_warnings)
