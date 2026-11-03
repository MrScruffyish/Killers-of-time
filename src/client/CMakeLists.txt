# kot_client_lib: everything except main(), so tests can drive the windows.
add_library(kot_client_lib STATIC
    ViewModel.h
    ViewModel.cpp
    MapView.h
    MapView.cpp
    Art.h
    Art.cpp
    Sound.h
    Sound.cpp
    MovePlanner.h
    MovePlanner.cpp
    MiniMap.h
    MiniMap.cpp
    BattleView.h
    BattleView.cpp
    ConnectPage.h
    ConnectPage.cpp
    LobbyPage.h
    LobbyPage.cpp
    LoadDialog.h
    LoadDialog.cpp
    Panels.h
    Panels.cpp
    UnitArt.cpp
    TownArt.cpp
    PortraitArt.cpp
    SpellBook.h
    SpellBook.cpp
    MapMakerDialog.h
    MapMakerDialog.cpp
    Settings.h
    Settings.cpp
    CrashLog.h
    CrashLog.cpp
    MarketDialog.h
    MarketDialog.cpp
    BattleResultDialog.h
    BattleResultDialog.cpp
    TownDialog.h
    TownDialog.cpp
    HeroDialog.h
    HeroDialog.cpp
    GamePage.h
    GamePage.cpp
    MainWindow.h
    MainWindow.cpp
)
target_include_directories(kot_client_lib PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_client_lib PUBLIC kot_net kot_core Qt6::Widgets Qt6::Network PRIVATE kot_warnings)
# Sound effects when Qt Multimedia is installed (sudo apt install qt6-multimedia-dev).
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(Qt6Multimedia_FOUND)
    target_link_libraries(kot_client_lib PRIVATE Qt6::Multimedia)
    target_compile_definitions(kot_client_lib PRIVATE KOT_HAVE_SOUND)
endif()

add_executable(kot_client main.cpp)
# Function names in crash backtraces (client/CrashLog.h).
set_target_properties(kot_client PROPERTIES ENABLE_EXPORTS ON)
target_link_libraries(kot_client PRIVATE kot_client_lib kot_warnings)
