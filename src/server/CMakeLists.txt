# kot_server_lib: everything except main(), so tests can use it.
add_library(kot_server_lib STATIC
    Lobby.h
    Lobby.cpp
    GameServer.h
    GameServer.cpp
    GameSession.h
    GameSession.cpp
)
target_include_directories(kot_server_lib PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_server_lib PUBLIC kot_net kot_ai kot_core Qt6::Network PRIVATE kot_warnings)

add_executable(kot_server main.cpp)
target_link_libraries(kot_server PRIVATE kot_server_lib kot_warnings)
