// Python-Umwandlungen fuer JSON-Werte aus Requests.
//
// main.py prueft Eingaben mit int(v), float(v), bool(v) und str(v). Deren Regeln
// sind nicht die von QJsonValue: int("1.5") scheitert, int(True) ist 1, bool("false")
// ist True, str(None) ist "None". Damit die Settings-Pruefung genau dasselbe annimmt
// und ablehnt wie 0.2.6, stehen die Regeln hier einmal nachgebaut.
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cmath>
#include <optional>

namespace kers::server::py {

/// bool(v) - Wahrheitswert nach Python.
inline bool truthy(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool();
    case QJsonValue::Double: return v.toDouble() != 0.0;
    case QJsonValue::String: return !v.toString().isEmpty();
    case QJsonValue::Array: return !v.toArray().isEmpty();
    case QJsonValue::Object: return !v.toObject().isEmpty();
    default: return false;
    }
}

/// int(v) - nullopt, wo Python TypeError/ValueError wirft.
inline std::optional<long long> toInt(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool() ? 1 : 0;
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (!std::isfinite(d))
            return std::nullopt;
        return static_cast<long long>(std::trunc(d));
    }
    case QJsonValue::String: {
        QString s = v.toString().trimmed();
        s.remove(QLatin1Char('_'));
        bool ok = false;
        const long long n = s.toLongLong(&ok, 10);      // "1.5" -> Fehler wie in Python
        return ok ? std::optional<long long>(n) : std::nullopt;
    }
    default: return std::nullopt;
    }
}

/// float(v)
inline std::optional<double> toFloat(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Bool: return v.toBool() ? 1.0 : 0.0;
    case QJsonValue::Double: return v.toDouble();
    case QJsonValue::String: {
        bool ok = false;
        const double d = v.toString().trimmed().toDouble(&ok);
        return ok ? std::optional<double>(d) : std::nullopt;
    }
    default: return std::nullopt;
    }
}

/// str(v)
inline QString toStr(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::String: return v.toString();
    case QJsonValue::Bool: return v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    case QJsonValue::Double: {
        const double d = v.toDouble();
        if (std::isfinite(d) && std::floor(d) == d && std::abs(d) < 1e15)
            return QString::number(static_cast<long long>(d));
        return QString::number(d, 'g', 17);
    }
    case QJsonValue::Array: return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object: return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    default: return QStringLiteral("None");
    }
}

/// os.path.basename(str(v or "")) mit Windows-Regeln (ntpath): / und \ trennen,
/// ein Laufwerk "C:" davor faellt weg.
inline QString basename(const QJsonValue& v)
{
    if (!truthy(v))
        return QString();
    QString s = toStr(v);
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (s.size() >= 2 && s.at(1) == QLatin1Char(':') && s.at(0).isLetter())
        s = s.mid(2);
    const int slash = s.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? s.mid(slash + 1) : s;
}

/// round(x, n) wie Python: gerundet wird der EXAKTE Binaerwert, nicht x * 10^n.
/// 0.69275 ist als double 0.692749999... - Python macht daraus 0.6927, das naive
/// std::round(x * 10000) / 10000 aber 0.6928 (die Multiplikation rundet auf).
/// Die Dezimalumwandlung von Qt rundet korrekt, deshalb der Weg ueber den Text.
inline double round(double x, int n)
{
    if (!std::isfinite(x))
        return x;
    return QString::number(x, 'f', n).toDouble();
}

}  // namespace kers::server::py
