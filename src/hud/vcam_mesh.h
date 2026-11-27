// VirtualCam: die Netze, prozedural gebaut (keine Modelldateien, keine Lizenzen).
//
// Koordinaten wie im Spiel: rechtshaendig, y oben, Meter. Das Auto schaut nach
// +z - mit heading = atan2(vorwaerts.x, vorwaerts.z) um y gedreht zeigt es also
// in Fahrtrichtung.
#pragma once

#include "vcam_item.h"

#include <QVector3D>

#include <memory>

namespace kers::hud {

/// Quader in ein Netz haengen. teamfarbe = true: Instanzfarbe statt `farbe`.
void vcAddBox(VcMesh& m, QVector3D mitte, QVector3D groesse, QColor farbe, bool teamfarbe);

/// Ein F1-Auto aus Quadern (~5,6 m lang, 2 m breit).
std::shared_ptr<const VcMesh> vcCarMesh();

/// Strecke aus einer geschlossenen Kontur (Mittellinie mit Hoehe): Asphaltband,
/// helle Randlinien, Start/Ziel-Strich bei `startIdx` und eine Bodenflaeche darunter.
std::shared_ptr<const VcMesh> vcStreckenNetz(const QList<QVector3D>& kontur, int startIdx, float breite = 12.f);

}  // namespace kers::hud
