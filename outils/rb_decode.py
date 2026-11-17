#!/usr/bin/env python3
"""Decoder for LNK2 ↔ Rain Bird controller exchanges from sigrok captures (.sr).

Physical layer (SPEC.md of sjwright/rainbird-esphome): Manchester 2400 bit/s, 208 µs half-bit,
high→low = 1, MSB first, idle low. Replies arrive inverted after the optocoupler and
start with one extra half-bit: both polarities and both phases are therefore tried.
Frames: request  F5 F5    1C <len> <payload> <chk> 1D
        reply    F5 F5 F5 1C <len> <payload> <chk> 1D,   chk = (len + sum(payload)) & 0xFF

Usage :
  rb_decode.py capture.sr [other.sr ...]      decodes, one line per frame
  rb_decode.py --test                          self-test on synthetic signals
The file name cap_YYYYMMDD_HHMMSS_mmm.sr (rb_capture.sh) gives the capture start time.
"""

import re
import sys
import zipfile
from datetime import datetime, timedelta

DEMI_BIT_US = 1e6 / 2400 / 2  # 208,3 µs
TOLERANCE = 0.35  # ±35 % on durations
SILENCE_US = 2000  # beyond that, end of burst (a frame has no level longer than 416 µs)

# Analyser channels: D0 = LNK2 requests, D1 = controller replies
VOIES = {0: "LNK2→prog", 1: "prog→LNK2"}

COMMANDES = {
    0x02: "ModelAndVersion", 0x03: "AvailableStations", 0x04: "CommandSupport", 0x05: "SerialNumber",
    0x0B: "ControllerFirmwareVersion", 0x10: "CurrentTime", 0x11: "SetCurrentTime", 0x12: "CurrentDate",
    0x13: "SetCurrentDate", 0x30: "WaterBudget", 0x31: "WaterBudgetSet", 0x36: "RainDelayGet",
    0x37: "RainDelaySet", 0x38: "ManuallyRunProgram", 0x39: "ManuallyRunStation", 0x3A: "TestStations",
    0x3B: "CurrentQueue", 0x3D: "CurrentStationError?", 0x3E: "CurrentRainSensorState",
    0x3F: "CurrentStationsActive", 0x40: "StopIrrigation", 0x42: "AdvanceStation",
    0x48: "CurrentIrrigationState", 0x49: "CurrentControllerStateSet", 0x4A: "ControllerEventTimestamp",
    0x4B: "StackManuallyRunStation", 0x4C: "CombinedControllerState",
}


def nom_payload(payload):
    """Readable name: request, reply (opcode + 0x80) or acknowledgement 01 <opcode>."""
    if not payload:
        return "?"
    op = payload[0]
    if op == 0x01 and len(payload) >= 2:
        return f"ACK {COMMANDES.get(payload[1], f'{payload[1]:02X}')}"
    if op == 0x00 and len(payload) >= 2:
        return f"NAK {COMMANDES.get(payload[1], f'{payload[1]:02X}')}"
    if op in COMMANDES:
        return COMMANDES[op]
    if op & 0x80 and (op & 0x7F) in COMMANDES:
        return f"réponse {COMMANDES[op & 0x7F]}"
    return f"{op:02X}?"


# --- Reading sigrok files -------------------------------------------------------------------------

def lire_sr(chemin):
    """Returns (sample rate in Hz, sample bytes, channel → bit mapping)."""
    with zipfile.ZipFile(chemin) as z:
        meta = z.read("metadata").decode()
        freq = None
        bits = {}
        for ligne in meta.splitlines():
            if ligne.startswith("samplerate="):
                v = ligne.split("=", 1)[1].strip().lower().replace(" ", "")
                mult = {"khz": 1e3, "mhz": 1e6, "hz": 1}
                for suffixe, m in mult.items():
                    if v.endswith(suffixe):
                        freq = float(v[: -len(suffixe)]) * m
                        break
            m = re.match(r"probe(\d+)=D(\d+)", ligne)
            if m:
                bits[int(m.group(2))] = int(m.group(1)) - 1
        morceaux = sorted(
            (n for n in z.namelist() if n.startswith("logic-1")),
            key=lambda n: int(n.rsplit("-", 1)[1]),
        )
        donnees = b"".join(z.read(n) for n in morceaux)
    return freq, donnees, bits


def paliers(donnees, bit, freq):
    """List of (level, duration µs, start µs) for one bit of the sample."""
    masque = 1 << bit
    us = 1e6 / freq
    out = []
    if not donnees:
        return out
    niveau = 1 if donnees[0] & masque else 0
    debut = 0
    for i in range(1, len(donnees)):
        n = 1 if donnees[i] & masque else 0
        if n != niveau:
            out.append((niveau, (i - debut) * us, debut * us))
            niveau, debut = n, i
    out.append((niveau, (len(donnees) - debut) * us, debut * us))
    return out


def rafales(liste):
    """Splits the levels into bursts separated by silence; returns (start µs, levels, idle level)."""
    courante, debut = [], None
    for niveau, duree, t in liste:
        if duree > SILENCE_US:
            if courante:
                yield debut, courante, niveau
            courante, debut = [], None
            continue
        if debut is None:
            debut = t
        courante.append((niveau, duree))
    if courante:
        yield debut, courante, None


# --- Manchester ------------------------------------------------------------------------------------

def demi_bits(paliers_rafale):
    """Converts the levels into a sequence of half-bit levels; None if a duration is out of tolerance."""
    out = []
    for niveau, duree in paliers_rafale:
        n = round(duree / DEMI_BIT_US)
        if n not in (1, 2) or abs(duree - n * DEMI_BIT_US) > TOLERANCE * n * DEMI_BIT_US:
            return None
        out.extend([niveau] * n)
    return out


def bits_depuis_demi(demis, inverse):
    bits = []
    for i in range(0, len(demis) - 1, 2):
        a, b = demis[i], demis[i + 1]
        if inverse:
            a, b = 1 - a, 1 - b
        if a == b:
            return None
        bits.append(1 if (a, b) == (1, 0) else 0)
    return bits


def octets(bits):
    return bytes(
        int("".join(map(str, bits[i : i + 8])), 2) for i in range(0, len(bits) - len(bits) % 8, 8)
    )


def trames(flux):
    """Looks for F5 F5 (F5) 1C len payload chk 1D frames; returns (payload, ok, preamble)."""
    out = []
    i = 0
    while i < len(flux) - 4:
        if flux[i] == 0x1C and i >= 2 and flux[i - 1] == 0xF5 and flux[i - 2] == 0xF5:
            n = flux[i + 1]
            fin = i + 2 + n
            if fin + 1 < len(flux):
                payload = flux[i + 2 : fin]
                ok = flux[fin] == (n + sum(payload)) & 0xFF and flux[fin + 1] == 0x1D
                pre = 3 if i >= 3 and flux[i - 3] == 0xF5 else 2
                out.append((payload, ok, pre))
                i = fin + 2
                continue
        i += 1
    return out


def decoder_rafale(paliers_rafale, niveau_repos):
    """Tries polarity, phase and a last half-bit merged with idle; keeps the best reading."""
    meilleur = (0, [], None)
    for supplement in (0, 1):
        pal = list(paliers_rafale)
        if supplement and niveau_repos is not None:
            pal.append((niveau_repos, DEMI_BIT_US))  # last half-bit merged with idle
        demis = demi_bits(pal)
        if demis is None:
            continue
        for phase in (0, 1):
            for inverse in (False, True):
                bits = bits_depuis_demi(demis[phase:], inverse)
                if bits is None:
                    continue
                trouvees = trames(octets(bits))
                score = sum(2 if ok else 1 for _, ok, _ in trouvees)
                if score > meilleur[0]:
                    meilleur = (score, trouvees, ("inversé" if inverse else "normal", phase))
    return meilleur[1], meilleur[2]


# --- Main ----------------------------------------------------------------------------------------

def heure_debut(chemin):
    m = re.search(r"(\d{8}_\d{6})_(\d{3})", chemin)
    if not m:
        return None
    return datetime.strptime(m.group(1), "%Y%m%d_%H%M%S") + timedelta(milliseconds=int(m.group(2)))


def decoder_fichier(chemin):
    freq, donnees, bits = lire_sr(chemin)
    t0 = heure_debut(chemin)
    lignes = []
    for voie, sens in VOIES.items():
        if voie not in bits:
            continue
        for debut, pal, repos in rafales(paliers(donnees, bits[voie], freq)):
            if len(pal) < 16:
                continue  # glitch
            liste, lecture = decoder_rafale(pal, repos)
            quand = (t0 + timedelta(microseconds=debut)).strftime("%H:%M:%S.%f")[:-3] if t0 else f"{debut/1e6:9.3f}s"
            if not liste:
                lignes.append((debut, f"{quand}  {sens}  ?? rafale non décodée ({len(pal)} paliers)"))
            for payload, ok, pre in liste:
                etat = "" if ok else "  ⚠ somme de contrôle fausse"
                lignes.append((debut, f"{quand}  {sens}  {payload.hex(' ').upper():<40} {nom_payload(payload)}{etat}"))
    for _, l in sorted(lignes):
        print(l)


def synthese(payload, reponse=False, inverse=False, demi_en_plus=False, freq=250_000):
    """Builds the samples of a frame, for the self-test."""
    trame = bytes([0xF5] * (3 if reponse else 2) + [0x1C, len(payload)]) + bytes(payload)
    trame += bytes([(len(payload) + sum(payload)) & 0xFF, 0x1D])
    demis = [0, 0] * 20  # idle
    if demi_en_plus:
        demis.append(1)
    for o in trame:
        for k in range(7, -1, -1):
            demis.extend([1, 0] if (o >> k) & 1 else [0, 1])
    demis += [0] * 20
    if inverse:
        demis = [1 - d for d in demis]
    par_demi = DEMI_BIT_US * freq / 1e6
    ech = []
    for i, d in enumerate(demis):
        ech.extend([d] * (round((i + 1) * par_demi) - round(i * par_demi)))
    return bytes(ech)


def auto_test():
    cas = [
        ([0x02], False, False, False),
        ([0x39, 0x00, 0x01, 0x06], False, False, False),
        ([0x3F, 0x00], False, True, False),
        ([0x01, 0x40], True, True, True),
        ([0x92, 0x13, 0xA7, 0xEA], True, True, True),
        ([0xFF] * 36, True, False, True),
    ]
    reussis = 0
    for payload, rep, inv, demi in cas:
        ech = synthese(payload, rep, inv, demi)
        pal = paliers(ech, 0, 250_000)
        trouve = []
        for _, p, repos in rafales(pal):
            liste, _ = decoder_rafale(p, repos)
            trouve += [bytes(x[0]) for x in liste if x[1]]
        ok = trouve == [bytes(payload)]
        reussis += ok
        print(f"{'OK ' if ok else 'ÉCHEC'}  {bytes(payload).hex(' ').upper():<30} réponse={rep} inversé={inv} demi-bit={demi} → {[t.hex(' ') for t in trouve]}")
    print(f"{reussis}/{len(cas)} réussis")
    return reussis == len(cas)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    if sys.argv[1] == "--test":
        sys.exit(0 if auto_test() else 1)
    for f in sys.argv[1:]:
        print(f"# {f}")
        decoder_fichier(f)
