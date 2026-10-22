# KERS Subsystems - F1 26 Telemetrie-Overlay, C++/Qt 6 (statisch, eine EXE).
#
# Bauen (siehe packaging\env.bat fuer die portable Toolchain unter D:\Tools):
#
#   call packaging\env.bat
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.11.2-static
#   cmake --build build
#
# Ergebnis: build\KERS_Subsystems.exe - ohne DLLs lauffaehig.
#
# Aufbau:
#   src/common   Pfade (data\ neben der EXE), JSON-Dateien, Log
#   src/hud      Theme, Modelle, Bausteine, Fenster  (Lib kers_hud)
#   src/server   UDP, Zustand, HTTP/SSE               (Lib kers_server, ab M2)
#   src/app      main.cpp, Schaltbrett, Tray, Updater
#   hud/qml      die QML-Szene - UNVERAENDERT gegenueber der Python-Fassung
#   static/, templates/   Assets und Web-Seiten, per qrc eingebettet

cmake_minimum_required(VERSION 3.28)

include(cmake/version.cmake)          # KERS_VERSION aus static/version.txt
project(KERS_Subsystems VERSION ${KERS_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Network Qml Quick QuickPrivate ZlibPrivate WebSockets)
qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/hud)

# âš  qt_add_executable, nicht add_executable: nur dann laeuft der Qt-Finalizer,
# der die statischen QML-Plugins (QtQuick, QtQuick.Effects, QtQuick.Shapes) per
# qmlimportscanner findet und dazulinkt. Mit add_executable zeigt MultiEffect
# nichts und QML meldet "module QtQuick.Effects is not installed".
qt_add_executable(KERS_Subsystems WIN32
    src/app/main.cpp
    src/app/icon.cpp
    src/app/server_control.cpp
    src/app/local_link.cpp
    src/app/log_view.cpp
    src/app/updater.cpp
    src/app/subsystems_panel.cpp
    src/app/access_dialog.cpp
    "${CMAKE_BINARY_DIR}/app.rc"
)
target_include_directories(KERS_Subsystems PRIVATE src/app)
target_link_libraries(KERS_Subsystems PRIVATE
    kers_hud
    kers_server
    kers_common
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickPrivate       # Bild-Cache beim Beenden leeren (main.cpp, BildCacheLeeren)
    Qt6::Network
)
# Keine libc++/libunwind/winpthread-DLLs neben der EXE - alles hinein.
target_link_options(KERS_Subsystems PRIVATE -static)

include(cmake/qml.cmake)              # die Szene aus hud/qml als QML-Modul "KersHud"

# Assets: static/ (Bilder, TTF-Schriften, tracks.json, version.txt) und die
# Web-Seiten aus templates/ landen als :/static/... und :/templates/... in der EXE.
# Die woff2-Schriften bleiben drin: QFontDatabase nimmt sie nicht (das HUD laedt
# die TTF), aber die Web-Seiten holen sie ueber /static/fonts/*.woff2.
# championship.json ist die eingebettete WM-Vorlage (Rueckfall, wenn in data\ keine liegt).
# designs/ (ab 0.3.1): die eingebauten Designs samt Schriften und QML -> :/designs/...
file(GLOB_RECURSE KERS_ASSETS RELATIVE ${CMAKE_SOURCE_DIR} CONFIGURE_DEPENDS
    static/* templates/* designs/*)
list(APPEND KERS_ASSETS championship.json)
qt_add_resources(KERS_Subsystems "assets"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}
    FILES ${KERS_ASSETS}
)

# Icon und Versionsinfo der EXE
configure_file(packaging/app.rc.in app.rc @ONLY)

# Tests:  ctest --test-dir build --output-on-failure
enable_testing()
add_subdirectory(tests)
