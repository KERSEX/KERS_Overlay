// Datenzufuhr fuers Overlay - die gemeinsame Schnittstelle der drei Quellen.
//
//   SseFeed     haengt am SSE-Strom eines Servers (/api/stream) - Remote-Betrieb
//   LocalFeed   bekommt die Payloads direkt vom In-Process-Server (ab M3)
//   DemoFeed    ein erfundenes Rennen, fuers Bauen ohne Spiel und fuer die
//               Layout-Vorschau
//
// Signale:
//   payload(QJsonObject)   neuer Live-Zustand
//   linkChanged(bool)      Kontakt zur QUELLE da/weg (nicht zu verwechseln mit
//                          payload["connected"] - das meint die UDP-Daten vom SPIEL)
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace kers::hud {

class Feed : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setBaseUrl(const QString&) {}
    virtual void setHz(int) {}
    /// Liefert die Quelle selbst Streckenkontur und WM-Stand (Demo)? Sonst
    /// holt die Bruecke beides vom Server.
    virtual bool hasOwnExtras() const { return false; }
    virtual QJsonArray trackPoints() const { return {}; }
    virtual QJsonObject championship() const { return {}; }

signals:
    void payload(const QJsonObject& data);
    void linkChanged(bool linked);
};

}  // namespace kers::hud
