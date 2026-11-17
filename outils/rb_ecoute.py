#!/usr/bin/env python3
"""Continuous passive listening to LNK2 ↔ ESP-ME3 exchanges through two UARTs of the Pi (9600 8N1, receive only).

Wiring (01/11/2026): white wire D0 (LNK2 GPIO26, requests) → pin 29 = GPIO5 = uart3 RX,
purple wire D1 (LNK2 GPIO25, replies) → pin 21 = GPIO9 = uart4 RX, black wire GND → pin 25.

Frame: 1C <len> 00 <payload> <crc> 1D; escaping 1B 01 = 1C, 1B 02 = 1B, 1B 03 = 1D;
crc = CRC-8 polynomial 0x07, init 0, over len 00 payload (before escaping).

Usage: rb_ecoute.py [log file]   (Ctrl+C to stop; default ~/rainbird/captures/ecoute_<date>.log)
Options: --brut  also prints the bytes outside frames.
"""

import os
import select
import sys
import termios
import time
from datetime import datetime

VOIES = {"/dev/ttyAMA3": "LNK2→prog", "/dev/ttyAMA4": "prog→LNK2"}
ECHAPPEMENT = {0x01: 0x1C, 0x02: 0x1B, 0x03: 0x1D}

COMMANDES = {
    0x02: "ModelAndVersion", 0x03: "AvailableStations", 0x04: "CommandSupport", 0x05: "SerialNumber",
    0x0B: "ControllerFirmwareVersion", 0x0C: "0C?", 0x10: "CurrentTime", 0x11: "SetCurrentTime",
    0x12: "CurrentDate", 0x13: "SetCurrentDate", 0x30: "WaterBudget", 0x31: "WaterBudgetSet",
    0x36: "RainDelayGet", 0x37: "RainDelaySet", 0x38: "ManuallyRunProgram", 0x39: "ManuallyRunStation",
    0x3A: "TestStations", 0x3B: "CurrentQueue", 0x3D: "3D?", 0x3E: "CurrentRainSensorState",
    0x3F: "CurrentStationsActive", 0x40: "StopIrrigation", 0x42: "AdvanceStation",
    0x48: "CurrentIrrigationState", 0x49: "CurrentControllerStateSet", 0x4A: "ControllerEventTimestamp",
    0x4B: "StackManuallyRunStation", 0x4C: "CombinedControllerState", 0x62: "62?", 0x63: "63?",
}


def crc8(octets):
    c = 0
    for b in octets:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def nom(payload):
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


def ouvrir(chemin):
    fd = os.open(chemin, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0                                   # iflag: nothing (no conversion, no XON/XOFF)
    a[1] = 0                                   # oflag
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0                                   # lflag: raw mode
    a[4] = a[5] = termios.B9600
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIFLUSH)
    return fd


class Decodeur:
    def __init__(self, sens, sortie, brut):
        self.sens, self.sortie, self.brut = sens, sortie, brut
        self.tampon, self.debut, self.dernier = [], None, 0.0

    def ecrire(self, texte):
        print(texte, flush=True)
        self.sortie.write(texte + "\n")
        self.sortie.flush()

    def octets(self, donnees, t):
        if self.tampon and t - self.dernier > 0.05:   # silence > 50 ms: frame dropped
            self.emettre(incomplete=True)
        for b in donnees:
            if b == 0x1C and not self.tampon:
                self.debut = t
            if b == 0x1C or self.tampon:
                self.tampon.append(b)
                if b == 0x1D:
                    self.emettre()
            elif self.brut:
                self.ecrire(f"{horodatage(t)} {self.sens}  hors trame : {b:02X}")
        self.dernier = t

    def emettre(self, incomplete=False):
        brut = self.tampon
        self.tampon = []
        hexa = " ".join(f"{b:02X}" for b in brut)
        if incomplete or len(brut) < 5:
            self.ecrire(f"{horodatage(self.debut)} {self.sens}  INCOMPLÈTE : {hexa}")
            return
        c, i, corps = brut[1:-1], 0, []
        while i < len(c):
            if c[i] == 0x1B and i + 1 < len(c):
                corps.append(ECHAPPEMENT.get(c[i + 1], -1))
                i += 2
            else:
                corps.append(c[i])
                i += 1
        ok = len(corps) >= 3 and crc8(corps[:-1]) == corps[-1] and corps[0] == len(corps) - 3
        payload = corps[2:-1]
        etat = "" if ok else "  ** CRC/LONGUEUR FAUX **"
        p = " ".join(f"{b:02X}" for b in payload)
        self.ecrire(f"{horodatage(self.debut)} {self.sens}  {p:<40} {nom(payload)}{etat}")


def horodatage(t):
    return datetime.fromtimestamp(t).strftime("%H:%M:%S.%f")[:-3]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    brut = "--brut" in sys.argv
    journal = args[0] if args else os.path.expanduser(
        f"~/rainbird/captures/ecoute_{datetime.now():%Y%m%d_%H%M%S}.log")
    os.makedirs(os.path.dirname(journal), exist_ok=True)
    with open(journal, "a", encoding="utf-8") as sortie:
        fds = {ouvrir(ch): Decodeur(sens, sortie, brut) for ch, sens in VOIES.items()}
        print(f"Écoute de {', '.join(VOIES)} → {journal} (Ctrl+C pour arrêter)", flush=True)
        try:
            while True:
                prets, _, _ = select.select(list(fds), [], [], 0.05)
                t = time.time()
                for fd in prets:
                    donnees = os.read(fd, 4096)
                    if donnees:
                        fds[fd].octets(donnees, t)
                for d in fds.values():
                    if d.tampon and t - d.dernier > 0.05:
                        d.emettre(incomplete=True)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
