// KERS_PROP - Gegenstueck zum _prop-Helfer in hud/models.py.
//
// Eine Property mit Getter, Setter und Signal `<name>Changed`, das NUR bei
// echter Aenderung feuert. Von Hand waeren das pro Feld sieben Zeilen Rumpf, und
// SessionState hat gut zwanzig davon. Wichtig ist der Gleichheitstest: ohne ihn
// feuert jedes Feld bei jedem Payload und QML rechnet Bindings neu, die sich gar
// nicht geaendert haben.
//
//     class SessionState : public QObject {
//         Q_OBJECT
//         KERS_PROP(QString, title, QString())
//         KERS_PROP(bool, connected, false)
//     };
//
// moc sieht die Q_PROPERTY-Zeile, weil es Makros aus eingebundenen Headern
// expandiert. Nach dem Makro steht die Klasse im Abschnitt `private:`.
#pragma once

#include <QObject>

#define KERS_PROP(Type, name, Default)                                          \
public:                                                                         \
    Q_PROPERTY(Type name READ name WRITE set_##name NOTIFY name##Changed)      \
    Type name() const { return m_##name; }                                      \
    void set_##name(const Type& v)                                              \
    {                                                                           \
        if (m_##name == v)                                                      \
            return;                                                             \
        m_##name = v;                                                           \
        Q_EMIT name##Changed();                                                 \
    }                                                                           \
Q_SIGNALS:                                                                      \
    void name##Changed();                                                       \
private:                                                                        \
    Type m_##name = Default;
