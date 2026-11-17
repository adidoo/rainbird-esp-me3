#!/usr/bin/env python3
"""Decodes the 0C/8C wrapped exchanges (ESP-ME3 object protocol) from an rb_ecoute.py log.

Enveloppe (01/11/2026) :
  request  0C 20 00 | 01 00 <dst 4 B> 00 00 <src 4 B> 00 00 05 00 00 00 | len16 LE | content
  reply    8C 20 00 | addresses swapped …                 05 00 FF FF | len16 LE | content
  dst 08 05 00 00 / src 0C 00 00 00 = configuration ; dst 01 01 00 00 / src 0E 00 00 00 = journal.
Contenu :
  07 0B <n> <descriptor>×n                          read     → 08 0B <n> (<descriptor> <type> <values>)×n
  05 0B <n> (<descriptor> <type> <values>)×n       write    → 06 0B 00 (ACK)
  83 0B …                                            journal   → 84 0B …
  descriptor = 16-bit LE object, ndim (1 B), then ndim × (start16, end16) LE
  type = 01 (8 bits), 02 (16 bits), 04 (32 bits), little-endian values, product of the dimension sizes.

Usage : rb_0c.py journal.log [heure_debut [heure_fin]] [--brut]
"""
import sys

OBJETS = {
    0x0A: "cycle (min) par zone ?", 0x0B: "ressuyage (min) par zone ?", 0x0D: "indicateur (interrogé en boucle)",
    0x10: "jours d'arrosage [prog][dim..sam]", 0x11: "? [prog]", 0x12: "? [prog]", 0x13: "? [prog]",
    0x14: "? (scalaire)", 0x15: "durée zone en s [prog][zone]", 0x18: "ajustement saisonnier % [prog]",
    0x1D: "heures de départ en min [prog][?][6] (1440 = aucune)",
}


def octets(ligne):
    reste = ligne.split("LNK2→prog", 1)[-1] if "LNK2→prog" in ligne else ligne.split("prog→LNK2", 1)[-1]
    out = []
    for c in reste.split():
        if len(c) == 2 and all(x in "0123456789ABCDEF" for x in c):
            out.append(int(c, 16))
        elif out:
            break
    return out


def decouper(b):
    for i in range(3, min(len(b) - 1, 40)):
        n = b[i] | (b[i + 1] << 8)
        if n == len(b) - i - 2 and n > 0:
            return b[:i], b[i + 2:]
    return b, []


def u16(b, i):
    return b[i] | (b[i + 1] << 8)


def descripteur(c, i):
    obj, nd = u16(c, i), c[i + 2]
    i += 3
    dims = []
    for _ in range(nd):
        dims.append((u16(c, i), u16(c, i + 2)))
        i += 4
    return obj, dims, i


def texte_desc(obj, dims):
    d = "".join(f"[{a}..{b}]" if a != b else f"[{a}]" for a, b in dims)
    return f"obj {obj:02X}{d} ({OBJETS.get(obj, '?')})"


def valeurs(c, i, dims):
    t = c[i]
    i += 1
    n = 1
    for a, b in dims:
        n *= b - a + 1
    taille = {1: 1, 2: 2, 4: 4}.get(t)
    if taille is None:
        return f"type {t:02X} inconnu : {' '.join(f'{x:02X}' for x in c[i:])}", len(c)
    vals = [int.from_bytes(bytes(c[i + k * taille:i + (k + 1) * taille]), "little") for k in range(n)]
    return f"type {t} = {vals}", i + n * taille


def analyser(c):
    """Returns a list of readable lines for a 0C content."""
    if len(c) < 3 or c[1] != 0x0B:
        return [" ".join(f"{x:02X}" for x in c)]
    op, n = c[0], c[2]
    out = []
    try:
        if op == 0x07:
            i = 3
            for _ in range(n):
                obj, dims, i = descripteur(c, i)
                out.append(f"LIRE   {texte_desc(obj, dims)}")
        elif op in (0x08, 0x05):
            i = 3
            for _ in range(n):
                obj, dims, i = descripteur(c, i)
                v, i = valeurs(c, i, dims)
                out.append(f"{'DONNÉ' if op == 8 else 'ÉCRIRE'} {texte_desc(obj, dims)} : {v}")
        elif op == 0x06:
            out.append(f"ACK écriture ({' '.join(f'{x:02X}' for x in c[2:])})")
        else:
            out.append(f"op {op:02X} : {' '.join(f'{x:02X}' for x in c)}")
    except IndexError:
        out.append("(tronqué) " + " ".join(f"{x:02X}" for x in c))
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    brut = "--brut" in sys.argv
    f = args[0]
    t0 = args[1] if len(args) > 1 else ""
    t1 = args[2] if len(args) > 2 else "99"
    for l in open(f, encoding="utf-8"):
        if "  0C 20" not in l and "  8C 20" not in l:
            continue
        h = l[:12]
        if not (t0 <= h <= t1):
            continue
        sens = "→" if "LNK2→prog" in l else "←"
        tete, corps = decouper(octets(l))
        if brut:
            print(f"{h} {sens} [{' '.join(f'{x:02X}' for x in tete[3:])}] {' '.join(f'{x:02X}' for x in corps)}")
        for ligne in analyser(corps):
            print(f"{h} {sens} {ligne}")


if __name__ == "__main__":
    main()
