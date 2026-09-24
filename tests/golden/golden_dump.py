"""golden_dump.py - eine .f1rec-Aufnahme durch die handle_*-Funktionen von main.py
schicken und den Zustand in festen Abstaenden als JSON-Zeilen ausgeben.

Gegenstueck zu kers_replay (C++). vergleich.py legt beide Ausgaben nebeneinander.

    python golden_dump.py <main.py> <aufnahme.f1rec> <ausgabe.jsonl> [--every 5] [--builtin tracks.json]

⚠ main.py wird in einen TEMPORAEREN Ordner kopiert und von dort importiert. Sonst
waere BASE_DIR der echte Programmordner, und eine waehrend des Replays gelernte
Strecke landete in dessen tracks.json. Gebraucht wird Flask (Test\\venv hat es);
installiert wird nichts.

Der Ablauf pro Paket ist der aus udp_listener() in main.py - dort steckt er in der
Endlosschleife und laesst sich nicht direkt aufrufen, deshalb steht er hier noch
einmal. Die Uhr (time.time) laeuft mit der Aufnahme.
"""

import gzip, importlib, json, os, shutil, struct, sys, tempfile, time as _time, types


def arg(name, default=None):
    a = sys.argv[1:]
    if name in a and a.index(name) + 1 < len(a):
        return a[a.index(name) + 1]
    return default


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    main_py, rec, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    every = float(arg("--every", 5))
    builtin = arg("--builtin", os.path.join(os.path.dirname(os.path.abspath(main_py)), "static", "tracks.json"))

    tmp = tempfile.mkdtemp(prefix="kers_golden_")
    shutil.copy(main_py, os.path.join(tmp, "main.py"))
    os.makedirs(os.path.join(tmp, "static"))
    shutil.copy(builtin, os.path.join(tmp, "static", "tracks.json"))
    sys.path.insert(0, tmp)

    clock = [1.8e9]                      # feste Startzeit, wie in kers_replay
    fake = types.SimpleNamespace(time=lambda: clock[0], strftime=_time.strftime,
                                 localtime=_time.localtime, sleep=lambda s: None)
    m = importlib.import_module("main")
    m.time = fake
    regie = {"chart": "none", "strat": False, "champ": False, "battles": True, "hotlap": True}
    m.regie_state.clear()
    m.regie_state.update(regie)

    def feed(data):
        h = m.parse_header(data)
        if not h:
            return
        pid = h["packet_id"]
        m._rx_format = h["format"]
        if h["session_uid"] != m._last_session_uid:
            if m.session_info.get("is_quali"):
                m.last_quali.clear()
                for d in m.drivers.values():
                    if d["best_lap"] > 0 and d["name"]:
                        m.last_quali[d["name"]] = d["best_lap"]
            m._last_session_uid = h["session_uid"]
            m.drivers.clear()
            m.lap_track.clear()
            m.valid_participants.clear()
            m.session_info["current_lap"] = 0
            m.session_info["fastest_lap_time"] = 0.0
            m.session_info["fastest_lap_driver"] = ""
            m.race_control.clear()
            m.car_pos.clear()
            m.final_classification.clear()
            m.lap_positions.clear()
            m.start_lights.update({"num": 0, "out": False, "t": 0.0})
            m.track_outline["pts"] = []
            m.track_outline["done"] = False
            m.track_outline["ver"] += 1
            m._outline_ref.update({"idx": None, "start_pt": None, "last": None, "dist": 0.0, "start_prog": None})
        m.last_packet_time = m.time.time()
        m._player_idx = h.get("player_index", m._player_idx)
        handler = {1: m.handle_session, 4: m.handle_participants, 2: m.handle_lap_data,
                   7: m.handle_car_status, 6: m.handle_car_telemetry, 16: m.handle_car_telemetry2,
                   3: m.handle_event, 0: m.handle_motion, 8: m.handle_final_classification,
                   10: m.handle_car_damage, 11: m.handle_session_history,
                   15: m.handle_lap_positions}.get(pid)
        if handler:
            handler(data)

    n = 0
    nxt = every
    with gzip.open(rec, "rb") as fh, open(out_path, "w", encoding="utf-8") as out:
        meta = json.loads(fh.readline().decode("utf-8"))
        while True:
            hd = fh.read(12)
            if len(hd) < 12:
                break
            ts, ln = struct.unpack("<dI", hd)
            data = fh.read(ln)
            if len(data) < ln:
                break
            clock[0] = 1.8e9 + ts
            feed(data)
            n += 1
            if ts >= nxt:
                with m.app.test_request_context():
                    track = m.api_track().get_json()
                    lap_pos = m.api_lap_positions().get_json()
                zeile = {"t": round(ts, 3), "live": m.build_live_payload(), "track": track,
                         "lap_positions": lap_pos}
                out.write(json.dumps(zeile, ensure_ascii=False) + "\n")
                while nxt <= ts:
                    nxt += every
    print(f"{n} Pakete aus {rec} ({meta.get('created')})")
    shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
