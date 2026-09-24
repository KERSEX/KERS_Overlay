"""vergleich.py - Ausgaben von golden_dump.py (Python) und kers_replay (C++) vergleichen.

    python vergleich.py <python.jsonl> <cpp.jsonl> [--tol 1e-6]

Zahlen werden als Zahlen verglichen (1.0 == 1), Gleitkomma mit Toleranz. Jede
Abweichung wird mit ihrem Pfad ausgegeben, gleiche Pfade zusammengefasst.
Rueckgabe 0 = keine Abweichung.
"""

import json, sys
from collections import Counter

TOL = 1e-6


def num(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def diff(a, b, pfad, out):
    if num(a) and num(b):
        if abs(a - b) > TOL * max(1.0, abs(a), abs(b)):
            out.append((pfad, a, b))
        return
    if type(a) is not type(b):
        out.append((pfad, a, b))
        return
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                out.append((f"{pfad}.{k}", "<fehlt>", b[k]))
            elif k not in b:
                out.append((f"{pfad}.{k}", a[k], "<fehlt>"))
            else:
                diff(a[k], b[k], f"{pfad}.{k}", out)
        return
    if isinstance(a, list):
        if len(a) != len(b):
            out.append((f"{pfad}[len]", len(a), len(b)))
        for i, (x, y) in enumerate(zip(a, b)):
            diff(x, y, f"{pfad}[{i}]", out)
        return
    if a != b:
        out.append((pfad, a, b))


def main():
    global TOL
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    if "--tol" in sys.argv:
        TOL = float(sys.argv[sys.argv.index("--tol") + 1])
    py = [json.loads(l) for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
    cpp = [json.loads(l) for l in open(sys.argv[2], encoding="utf-8") if l.strip()]
    print(f"Python: {len(py)} Zeilen, C++: {len(cpp)} Zeilen")
    alle = []
    for zp, zc in zip(py, cpp):
        d = []
        diff(zp, zc, f"t={zp.get('t')}", d)
        alle.extend(d)
    if len(py) != len(cpp):
        alle.append(("<zeilen>", len(py), len(cpp)))
    if not alle:
        print("KEINE Abweichung")
        return 0
    # Pfade ohne Zeit und Listenindex zusammenfassen
    import re
    muster = Counter(re.sub(r"\[\d+\]", "[]", p.split(".", 1)[-1]) for p, _, _ in alle)
    print(f"{len(alle)} Abweichungen, {len(muster)} Arten:")
    for m, n in muster.most_common(40):
        bsp = next((a, b) for p, a, b in alle if re.sub(r"\[\d+\]", "[]", p.split(".", 1)[-1]) == m)
        print(f"  {n:6d}x  {m}   z.B. py={str(bsp[0])[:60]!s}  cpp={str(bsp[1])[:60]!s}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
