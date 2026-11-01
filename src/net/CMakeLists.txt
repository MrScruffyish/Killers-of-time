# kot_net: protocol definitions shared by client and server.
add_library(kot_net STATIC
    Protocol.h
    Protocol.cpp
)
target_include_directories(kot_net PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_net PUBLIC kot_core Qt6::Network PRIVATE kot_warnings)
