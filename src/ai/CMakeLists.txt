# kot_ai: AI players. They use the same command interface as humans (later).
add_library(kot_ai STATIC
    IAiPlayer.h
    PassiveAi.h
    PassiveAi.cpp
)
target_include_directories(kot_ai PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_ai PUBLIC kot_core PRIVATE kot_warnings)
