# Die QML-Szene als Modul "KersHud" in die EXE einbetten.
#
# Die Dateien bleiben in hud/qml/ liegen (unveraendert gegenueber der
# Python-Fassung). Damit sie in der Ressource NICHT unter hud/qml/... landen,
# sondern direkt unter qrc:/KersHud/, bekommt jede Datei einen Alias ohne den
# Ordner-Praefix. Dann funktionieren die Verzeichnis-Imports im QML weiter:
#     import "parts"   -> qrc:/KersHud/parts/
#     import ".."      -> qrc:/KersHud/
#     import "Fmt.js"  -> qrc:/KersHud/Fmt.js
#
# Die Szene laedt main.cpp ueber qrc:/KersHud/Overlay.qml. Zum Bauen an der Szene
# gibt es --qml-dir <Ordner>: dann kommt sie aus Dateien und "Neu laden" greift.

set(KERS_QML_DIR "${CMAKE_SOURCE_DIR}/hud/qml")

set(KERS_QML_FILES
    Overlay.qml
    EditFrame.qml
    AccentTop.qml
    MaskedIcon.qml
    MedalText.qml
    RunningBorder.qml
    Fmt.js
    parts/BattleRow.qml
    parts/Battles.qml
    parts/Championship.qml
    parts/Charts.qml
    parts/DangerZone.qml
    parts/FastestLapBanner.qml
    parts/Hotlaps.qml
    parts/LowerThird.qml
    parts/Onboard.qml
    parts/PitCards.qml
    parts/PitProjection.qml
    parts/RaceMessage.qml
    parts/StartLights.qml
    parts/Tower.qml
    parts/TowerRow.qml
    parts/Trackmap.qml
)

set(_kers_qml_abs)
foreach(_f IN LISTS KERS_QML_FILES)
    set(_p "${KERS_QML_DIR}/${_f}")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "QML-Datei fehlt: ${_p}")
    endif()
    set_source_files_properties("${_p}" PROPERTIES QT_RESOURCE_ALIAS "${_f}")
    list(APPEND _kers_qml_abs "${_p}")
endforeach()
# Fmt.js traegt `.pragma library` erst nach dem Kopfkommentar - der QML-Engine
# reicht das, Qts CMake-Pruefung sieht es nicht und warnt. Die Datei wird ohnehin
# ueber ihren Pfad importiert (import "Fmt.js"), nicht ueber die qmldir.
set_source_files_properties("${KERS_QML_DIR}/Fmt.js" PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)

# Die Probe-Szene fuer Meilenstein 0 liegt in tests/, wird aber neben die
# Hilfskomponenten (MedalText, RunningBorder, MaskedIcon) ins Modul gelegt, damit
# sie dieselben Typen sieht wie Overlay.qml. Start mit --probe.
set_source_files_properties("${CMAKE_SOURCE_DIR}/tests/M0Probe.qml"
    PROPERTIES QT_RESOURCE_ALIAS "M0Probe.qml")
list(APPEND _kers_qml_abs "${CMAKE_SOURCE_DIR}/tests/M0Probe.qml")

qt_add_qml_module(KERS_Subsystems
    URI KersHud
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES ${_kers_qml_abs}
)
