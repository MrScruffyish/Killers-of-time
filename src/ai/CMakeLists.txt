# kot_ai: AI players. They play through the same commands as human clients.
add_library(kot_ai STATIC
    IAiPlayer.h
    AiFactory.h
    AiFactory.cpp
    AiView.h
    AiView.cpp
    PassiveAi.h
    PassiveAi.cpp
    SimpleAi.h
    SimpleAi.cpp
)
target_include_directories(kot_ai PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_ai PUBLIC kot_core kot_net PRIVATE kot_warnings)
kot_pch(kot_ai)
