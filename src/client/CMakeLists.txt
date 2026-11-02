# kot_client_lib: everything except main(), so tests can drive the windows.
add_library(kot_client_lib STATIC
    ViewModel.h
    ViewModel.cpp
    MapView.h
    MapView.cpp
    ConnectPage.h
    ConnectPage.cpp
    LobbyPage.h
    LobbyPage.cpp
    TownDialog.h
    TownDialog.cpp
    GamePage.h
    GamePage.cpp
    MainWindow.h
    MainWindow.cpp
)
target_include_directories(kot_client_lib PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_client_lib PUBLIC kot_net kot_core Qt6::Widgets Qt6::Network PRIVATE kot_warnings)

add_executable(kot_client main.cpp)
target_link_libraries(kot_client PRIVATE kot_client_lib kot_warnings)
