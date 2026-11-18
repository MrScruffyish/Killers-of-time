# kot_artgen: the art drawn in code, used only by kot_artexport (and tst_art). The
# client loads the PNG files it writes and never draws these pictures.
add_library(kot_artgen STATIC
    ArtGen.h
    ArtExport.cpp
    MapArt.cpp
    IconArt.cpp
    PortraitArt.cpp
    SpellArt.cpp
    TownArt.cpp
    UnitArt.cpp
)
target_include_directories(kot_artgen PUBLIC ${KOT_SRC_DIR})
target_link_libraries(kot_artgen PUBLIC kot_core Qt6::Gui PRIVATE kot_warnings)
