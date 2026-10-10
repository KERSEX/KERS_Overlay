#include "hud_state.h"

#include "jsonfile.h"

#include <QJsonObject>

namespace kers {

HudState HudState::load(const QString& path)
{
    HudState s;
    bool ok = false;
    const QJsonObject o = json::readObject(path, &ok);
    if (!ok)
        return s;
    auto str = [&](const char* k, QString& v) { if (o.value(QLatin1String(k)).isString()) v = o.value(QLatin1String(k)).toString(); };
    auto num = [&](const char* k, int& v) { if (o.value(QLatin1String(k)).isDouble()) v = o.value(QLatin1String(k)).toInt(); };
    auto flag = [&](const char* k, bool& v) { if (o.value(QLatin1String(k)).isBool()) v = o.value(QLatin1String(k)).toBool(); };
    str("base_url", s.baseUrl);
    num("x", s.x); num("y", s.y); num("w", s.w); num("h", s.h);
    flag("locked", s.locked); flag("visible", s.visible);
    num("opacity", s.opacity); num("hz", s.hz);
    flag("obs_window", s.obsWindow); num("obs_w", s.obsW); num("obs_h", s.obsH);
    flag("obs_findable", s.obsFindable); flag("obs_chroma", s.obsChroma);
    str("obs_chroma_color", s.obsChromaColor);
    num("panel_x", s.panelX); num("panel_y", s.panelY);
    flag("panel_visible", s.panelVisible); flag("panel_on_top", s.panelOnTop);
    flag("update_stumm", s.updateStumm);
    flag("server_autostart", s.serverAutostart);
    str("remote_key", s.remoteKey);
    if (s.baseUrl.endsWith(QLatin1Char('/')))
        s.baseUrl.chop(1);
    return s;
}

bool HudState::save(const QString& path) const
{
    QJsonObject o;
    o["base_url"] = baseUrl;
    o["x"] = x; o["y"] = y; o["w"] = w; o["h"] = h;
    o["locked"] = locked; o["visible"] = visible;
    o["opacity"] = opacity; o["hz"] = hz;
    o["obs_window"] = obsWindow; o["obs_w"] = obsW; o["obs_h"] = obsH;
    o["obs_findable"] = obsFindable; o["obs_chroma"] = obsChroma;
    o["obs_chroma_color"] = obsChromaColor;
    o["panel_x"] = panelX; o["panel_y"] = panelY;
    o["panel_visible"] = panelVisible; o["panel_on_top"] = panelOnTop;
    o["update_stumm"] = updateStumm;
    o["server_autostart"] = serverAutostart;
    if (!remoteKey.isEmpty())
        o["remote_key"] = remoteKey;
    return json::writeObject(path, o);
}

}  // namespace kers
