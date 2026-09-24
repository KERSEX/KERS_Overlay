// Der Live-Payload (/api/live, /api/stream) und die Nebendaten fuer die Routen.
// Portierung von build_live_payload, calculate_championship, api_track, api_tracks,
// api_lap_positions aus main.py.
#pragma once

#include "race_state.h"

#include <QJsonObject>

namespace kers::server {

/// Kompletter Live-Zustand fuers Overlay.
QJsonObject buildLivePayload(const RaceState& st, const QJsonObject& settings, const QJsonObject& regie);

/// WM-Stand aus championship.json plus Live-Hochrechnung (nur Top 10 bekommen Punkte).
QJsonObject calculateChampionship(const RaceState& st, const QString& champPath);

/// /api/track - die gelernte Kontur samt Lernfortschritt.
QJsonObject trackJson(RaceState& st);

/// /api/tracks - alle bekannten Strecken.
QJsonObject tracksJson(const RaceState& st);

/// /api/lap_positions
QJsonObject lapPositionsJson(const RaceState& st);

}  // namespace kers::server
