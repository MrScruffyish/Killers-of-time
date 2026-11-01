add_executable(kot_server
    main.cpp
    GameServer.h
    GameServer.cpp
)
target_link_libraries(kot_server PRIVATE kot_net kot_ai kot_core Qt6::Network kot_warnings)
