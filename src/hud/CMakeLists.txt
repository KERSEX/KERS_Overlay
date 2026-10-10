qt_add_library(kers_hud STATIC
    # Header ohne gleichnamige .cpp muessen hier stehen, sonst laeuft AUTOMOC
    # nicht ueber ihre Q_OBJECT-Klassen (Feed, SlotModel, die Bausteine).
    feed.h
    parts.h
    theme.cpp
    derive.cpp
    models.cpp
    parts_core.cpp
    parts_battles.cpp
    parts_hotlaps.cpp
    parts_onboard.cpp
    parts_pit.cpp
    parts_misc.cpp
    data_source.cpp
    extras.cpp
    sse_feed.cpp
    demo_feed.cpp
    bridge.cpp
    hud_window.cpp
    obs_window.cpp
    design_scene.cpp
)
target_include_directories(kers_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kers_hud PUBLIC
    kers_common
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
)
if(WIN32)
    target_link_libraries(kers_hud PUBLIC dwmapi user32)
endif()
