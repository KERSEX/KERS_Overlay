"""
Schriften fuers QML-Overlay bereitstellen: static/fonts/*.woff2 -> static/fonts/ttf/*.ttf

WARUM DAS NOETIG IST
--------------------
Das Web-Overlay laedt Inter und Teko per @font-face als WOFF2. Qt kann WOFF2 nicht:
QFontDatabase.addApplicationFont() (und damit auch der QML-FontLoader) versteht nur
TTF/OTF/TTC. Ohne diesen Schritt faellt das QML-Overlay stillschweigend auf die
Systemschrift zurueck und sieht dann deutlich anders aus als das Web-Overlay.

ZWEITER GRUND: STATISCHE SCHNITTE STATT VARIABLE FONT
-----------------------------------------------------
Inter und Teko sind Variable Fonts (ein Datei, Gewicht stufenlos 100-900). Qt legt eine
Variable Font aber nur mit ihrer STANDARD-Instanz in der Fontdatenbank ab - `font.weight:
Font.Bold` waehlt dann NICHT den fetten Schnitt aus, sondern rendert weiter Regular.
Das Overlay lebt aber von den Gewichten (600/700/800 sind ueberall im CSS).

Deshalb schneidet dieses Skript aus der Variable Font feste Einzelschnitte heraus
(fontTools.varLib.instancer). Jeder Schnitt bekommt ueber `updateFontNames=True` einen
korrekten Namen ("Inter SemiBold") und die passende OS/2-Gewichtsklasse. Qt gruppiert sie
dann von selbst zu EINER Familie "Inter" mit mehreren Gewichten - genau wie im Browser.

AUFRUF
------
    python tools/woff2_to_ttf.py

Braucht fontTools und brotli - beide NUR fuer diesen einmaligen Schritt, nicht zur
Laufzeit des Overlays. Deshalb stehen sie auch nicht in requirements.txt:

    pip install fonttools brotli

Die erzeugten TTF liegen in static/fonts/ttf/ und werden mitversioniert. Erneut laufen
lassen muss man das Skript nur, wenn die WOFF2 ausgetauscht werden.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "static" / "fonts"
OUT_DIR = SRC_DIR / "ttf"

# Welche Gewichte gebraucht werden - abgelesen an den font-weight-Angaben im CSS.
#   Inter: 600 (.col-driver), 700 (Gaps, Zeiten), 800 (.col-drs, .col-gap.leader);
#          400/500 fuer normalen Flaechentext.
#   Teko : 500 (.ticker-item), 700 (Positionen, Titel, Badges); 400 als Grundschnitt.
WEIGHTS = {
    "Inter": [400, 500, 600, 700, 800],
    "Teko":  [400, 500, 700],
}

# Stilname je Gewicht. Steht so in der Schrift und ist das, wonach Qt gruppiert.
SUFFIX = {400: "Regular", 500: "Medium", 600: "SemiBold", 700: "Bold", 800: "ExtraBold"}

# Nur diese vier Stile duerfen laut OpenType im Basis-Familiennamen (nameID 1) stehen.
# Alles andere (Medium, SemiBold, ExtraBold, ...) bekommt eine eigene Legacy-Familie
# "Inter SemiBold" und wird erst ueber die typografischen Namen (16/17) wieder zur
# Familie "Inter" zusammengefasst.
RIBBI = {"Regular", "Bold", "Italic", "Bold Italic"}


def _rename(font, family: str, style: str, weight: int) -> None:
    """Namenstabelle und Gewichtsangaben auf genau einen Schnitt festlegen.

    Der eingebaute Weg (instancer(..., updateFontNames=True)) reicht hier nicht: er
    leitet die Namen aus der STAT-Tabelle ab, und die ist bei Inter/Teko so duenn, dass
    400, 500, 600 und 800 alle als "Inter / Regular" herauskommen. Vier Dateien mit
    identischem Familien- UND Stilnamen sind fuer Qt derselbe Schnitt - drei davon
    fallen unter den Tisch und das halbe Overlay rendert im falschen Gewicht.

    Deshalb hier von Hand, nach der ueblichen Konvention:
        nameID  1/2   Legacy-Familie + RIBBI-Stil  ("Inter SemiBold" / "Regular")
        nameID 16/17  Typografische Familie + Stil ("Inter" / "SemiBold")
    Qt liest 16/17 wenn vorhanden und sieht dadurch EINE Familie "Inter" mit fuenf
    Gewichten. Programme, die nur 1/2 kennen, sehen weiterhin gueltige Namen.
    """
    is_ribbi = style in RIBBI
    legacy_family = family if is_ribbi else f"{family} {style}"
    legacy_style = style if is_ribbi else "Regular"
    full = family if style == "Regular" else f"{family} {style}"
    ps = full.replace(" ", "")

    name = font["name"]
    # Alte Eintraege wegwerfen: die Variable Font bringt Namen fuer alle Instanzen mit,
    # stehengebliebene Reste wuerden sich mit den neuen mischen.
    for nid in (1, 2, 3, 4, 6, 16, 17):
        name.removeNames(nameID=nid)
    for nid, value in ((1, legacy_family), (2, legacy_style), (3, f"{full}; KERS Overlay"),
                       (4, full), (6, ps), (16, family), (17, style)):
        # Beide Plattformen: 3/1/0x409 = Windows/Unicode/en-US, 1/0/0 = Mac/Roman.
        name.setName(value, nid, 3, 1, 0x409)
        name.setName(value, nid, 1, 0, 0)

    font["OS/2"].usWeightClass = weight
    # Fett-Bits konsistent halten - sonst hebt Qt/Windows den Schnitt doppelt hervor
    # (echtes Bold PLUS synthetisches Fett) oder gar nicht.
    bold = weight >= 700
    fs = font["OS/2"].fsSelection & ~((1 << 5) | (1 << 6))   # Bit 5 = BOLD, Bit 6 = REGULAR
    if bold:
        fs |= 1 << 5
    elif style == "Regular":
        fs |= 1 << 6                              # nur der echte Grundschnitt ist "REGULAR"
    font["OS/2"].fsSelection = fs
    mac = font["head"].macStyle & ~1              # Bit 0 = Bold
    font["head"].macStyle = mac | (1 if bold else 0)


def main() -> int:
    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
    except ImportError:
        print("fontTools fehlt. Einmalig installieren:\n    pip install fonttools brotli")
        return 1

    sources = sorted(SRC_DIR.glob("*.woff2"))
    if not sources:
        print(f"Keine .woff2 in {SRC_DIR} gefunden.")
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for src in sources:
        stem = src.stem                      # "Inter" / "Teko"
        font = TTFont(src)                   # brotli entpackt das WOFF2 hier

        if "fvar" not in font:
            # Statische Schrift - dann reicht das reine Umpacken nach TTF.
            out = OUT_DIR / f"{stem}.ttf"
            font.flavor = None
            font.save(out)
            print(f"  {src.name} -> {out.name}  (statisch, 1 Schnitt)")
            continue

        axes = {a.axisTag: (a.minValue, a.maxValue) for a in font["fvar"].axes}
        lo, hi = axes.get("wght", (400, 400))
        for want in WEIGHTS.get(stem, [400]):
            wght = max(lo, min(hi, want))    # ausserhalb der Achse -> auf den Rand klemmen
            style = SUFFIX.get(want, str(want))
            # Achse festfrieren. updateFontNames bleibt bewusst AUS - die Namen setzt
            # _rename() selbst, siehe Begruendung dort.
            inst = instancer.instantiateVariableFont(
                TTFont(src), {"wght": wght}, inplace=False, updateFontNames=False,
            )
            _rename(inst, stem, style, want)
            inst.flavor = None               # TTF statt WOFF2 schreiben
            out = OUT_DIR / f"{stem}-{style}.ttf"
            inst.save(out)
            print(f"  {src.name} @ wght={wght:>3} -> {out.name}"
                  f"  ({inst['name'].getDebugName(16)} / {inst['name'].getDebugName(17)})")

    print(f"\nFertig. Zielordner: {OUT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
