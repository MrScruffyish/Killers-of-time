# kot_net: protocol, framing, connections and the lobby client. Shared by client and server.
add_library(kot_net STATIC
    Protocol.h
    Protocol.cpp
    Message.h
    Message.cpp
    MessageFramer.h
    MessageFramer.cpp
    Connection.h
    Connection.cpp
    LobbyTypes.h
    LobbyTypes.cpp
    LobbyClient.h
    LobbyClient.cpp
)
target_include_directories(kot_net PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_net PUBLIC kot_core Qt6::Network PRIVATE kot_warnings)
