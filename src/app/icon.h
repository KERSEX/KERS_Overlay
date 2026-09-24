// Das Blitz-Logo als Tray-, Fenster- und Taskleisten-Icon - Portierung von
// make_icon() in hud/common.py.
//
// Quelle ist static/brand/Icon.png (500x500, der Blitz darin nur 305x473).
// ⚠ Der transparente Rand wird abgeschnitten, sonst rechnet Windows das GANZE
// Quadrat auf die 16 px des Tray-Symbols herunter und vom Blitz bleibt ein
// Fussel. Danach wird quadratisch aufgefuellt (sonst gestaucht) und jede
// Groesse einzeln ins QIcon gelegt (sonst franst Windows selbst herunter).
#pragma once

#include <QIcon>

namespace kers::app {

QIcon makeIcon();

}  // namespace kers::app
