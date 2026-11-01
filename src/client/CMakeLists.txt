add_executable(kot_client
    main.cpp
    MainWindow.h
    MainWindow.cpp
)
target_link_libraries(kot_client PRIVATE kot_net kot_core Qt6::Widgets Qt6::Network kot_warnings)
