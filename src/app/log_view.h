// Log-Fenster - Ersatz fuer das Konsolenfenster des Python-Servers.
//
// Zeigt die letzten Zeilen aus kers::log (Server, HUD, QML-Warnungen) und laeuft
// live mit. Dieselben Zeilen stehen in data\kers.log.
#pragma once

#include <QPlainTextEdit>
#include <QWidget>

namespace kers::app {

class LogView : public QWidget {
    Q_OBJECT
public:
    explicit LogView(QWidget* parent = nullptr);

private:
    void anhaengen(const QString& zeile);
    QPlainTextEdit* m_text;
};

}  // namespace kers::app
