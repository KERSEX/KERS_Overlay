"""
Gelernte Streckenkonturen in die Auslieferung uebernehmen:
PY2EXE\\output\\data\\tracks.json  ->  static/tracks.json

WARUM DAS NOETIG IST
--------------------
Die Streckenkontur fuer die Trackmap wird aus einer gefahrenen Runde gelernt
(_learn_outline in main.py). Es gibt keine Fremdquelle dafuer: die Punkte sind die
WELTKOORDINATEN DES SPIELS, und die bekommt man nur, indem jemand die Strecke
faehrt. Der Weg ist also immer: gefahren -> gelernt -> uebernommen.

Zur Laufzeit gibt es zwei Dateien (siehe Kommentar bei TRACKS_FILE in main.py):

  tracks.json         neben der .exe - was der Benutzer selbst gefahren hat
  static/tracks.json  in der EXE eingebacken - was mitgeliefert wird

Dieses Skript schiebt die erste in die zweite. Einmal vor einem Release
aufgerufen, wandert alles, was seitdem gefahren wurde, in die naechste EXE - und
alle anderen muessen diese Strecken nicht mehr selbst anlernen.

⚠ AUFRUFEN, BEVOR nach input\\ gespiegelt wird. Danach ist es zu spaet: der Build
nimmt static/ aus input\\, nicht aus dem Projektordner.

BENUTZUNG
---------
    venv\\Scripts\\python.exe tools\\tracks_uebernehmen.py
    venv\\Scripts\\python.exe tools\\tracks_uebernehmen.py --dry

--dry zeigt nur, was passieren wuerde.

⚠ Vorhandene Eintraege werden UEBERSCHRIEBEN, wenn die gelernte Kontur mehr
Punkte hat (feiner aufgeloest) oder die mitgelieferte Streckenlaenge nicht mehr
passt. Sonst bleibt die mitgelieferte stehen - eine einmal geprueft ausgelieferte
Kontur soll nicht durch eine schlechtere ersetzt werden, nur weil jemand die
Strecke nochmal gefahren ist.
"""
import json
import os
import sys

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUELLE = r"D:\Programme\Code\PY2EXE\output\data\tracks.json"
ZIEL = os.path.join(PROJ, "static", "tracks.json")
FMT = 1

trocken = "--dry" in sys.argv


kuerzen = None       # wird unten aus main.py geholt, siehe aus_main


def lesen(pfad):
    try:
        with open(pfad, encoding="utf-8") as f:
            roh = json.load(f)
    except FileNotFoundError:
        return {}, "gibt es nicht"
    except Exception as e:
        return {}, "unlesbar (%s)" % e
    if not isinstance(roh, dict) or int(roh.get("fmt") or 0) != FMT:
        return {}, "falsches Format"
    raus = {}
    for k, v in roh.items():
        if k == "fmt" or not isinstance(v, dict):
            continue
        try:
            # Genau wie der Server: eine Kontur mit zwei Runden auf die erste
            # kuerzen. Sonst wandert der Murks in die Auslieferung und wird erst
            # beim Einlesen geheilt - bei jedem Start aufs Neue.
            laenge = int(v["len"])
            raus[int(k)] = {"len": laenge,
                            "pts": kuerzen(v["pts"], laenge)}
        except (TypeError, ValueError, KeyError):
            continue
    return raus, "%d Strecken" % len(raus)


def aus_main(name):
    """Eine Funktion aus main.py holen, ohne main.py zu importieren.

    ⚠ Nicht abschreiben: _kontur_auf_eine_runde entscheidet, ob eine Kontur zwei
    Runden enthaelt. Eine zweite Kopie hier wuerde frueher oder spaeter von der
    im Server abweichen, und dann liefert das Werkzeug etwas anderes aus, als der
    Server einliest.
    """
    import ast
    with open(os.path.join(PROJ, "main.py"), encoding="utf-8") as f:
        baum = ast.parse(f.read())
    for k in baum.body:
        if isinstance(k, ast.FunctionDef) and k.name == name:
            umgebung = {}
            exec(compile(ast.Module(body=[k], type_ignores=[]), "<main.py>", "exec"),
                 umgebung)
            return umgebung[name]
    raise SystemExit("%s in main.py nicht gefunden" % name)


def namen():
    """Streckennamen aus main.py holen, ohne main.py zu importieren.

    ⚠ Ein Import wuerde den Flask-Server aufbauen und eine overlay_settings.json
    im Projektordner anlegen - fuer ein Werkzeug, das nur Dateien zusammenlegt,
    ist das zu viel Nebenwirkung."""
    import ast
    with open(os.path.join(PROJ, "main.py"), encoding="utf-8") as f:
        baum = ast.parse(f.read())
    for k in baum.body:
        if isinstance(k, ast.Assign) and getattr(k.targets[0], "id", "") == "TRACK_INFO":
            roh = ast.literal_eval(k.value)
            return {i: v[0] for i, v in roh.items()}
    return {}


kuerzen = aus_main("_kontur_auf_eine_runde")

gelernt, q_info = lesen(QUELLE)
liegt, z_info = lesen(ZIEL)
name = namen()

print("Gelernt : %s  (%s)" % (QUELLE, q_info))
print("Ziel    : %s  (%s)" % (ZIEL, z_info))
print("")

if not gelernt:
    print("Nichts zu uebernehmen.")
    sys.exit(0)

neu, ersetzt, behalten = [], [], []
for tid, e in sorted(gelernt.items()):
    alt = liegt.get(tid)
    bezeichnung = "%-18s (#%d)" % (name.get(tid, "Strecke #%d" % tid), tid)
    if alt is None:
        neu.append(bezeichnung + "  %5d Punkte" % len(e["pts"]))
        liegt[tid] = e
    elif len(e["pts"]) > len(alt["pts"]) or alt["len"] != e["len"]:
        ersetzt.append(bezeichnung + "  %5d -> %5d Punkte" % (len(alt["pts"]), len(e["pts"])))
        liegt[tid] = e
    else:
        behalten.append(bezeichnung + "  %5d Punkte bleiben" % len(alt["pts"]))

for titel, liste in (("NEU", neu), ("ERSETZT", ersetzt), ("UNVERAENDERT", behalten)):
    if liste:
        print("%s:" % titel)
        for z in liste:
            print("   " + z)
        print("")

if not neu and not ersetzt:
    print("Die Auslieferung ist schon auf dem Stand.")
    sys.exit(0)

if trocken:
    print("--dry: nichts geschrieben.")
    sys.exit(0)

raus = {"fmt": FMT}
for tid, e in sorted(liegt.items()):
    raus[str(tid)] = {"len": e["len"],
                      "pts": [[round(float(p[0]), 1), round(float(p[1]), 1),
                               round(float(p[2]), 4), int(p[3])] for p in e["pts"]]}
os.makedirs(os.path.dirname(ZIEL), exist_ok=True)
with open(ZIEL, "w", encoding="utf-8") as f:
    json.dump(raus, f, ensure_ascii=False, separators=(",", ":"))

groesse = os.path.getsize(ZIEL)
print("Geschrieben: %s" % ZIEL)
print("  %d Strecken, %.1f KB" % (len(liegt), groesse / 1024))
print("")
print("⚠ Jetzt erst nach input\\ spiegeln und dann bauen - sonst landet der alte")
print("  Stand in der EXE.")
