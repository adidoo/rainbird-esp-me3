# Draws the wiring diagram of the ESP32-C6 ↔ ESP-ME3 prototype (two PC817C) and the PC817C pinout,
# in English and in French: images/wiring_prototype.png, images/wiring_prototype_FR.png,
# images/pc817_pinout.png, images/pc817_pinout_FR.png.
import pathlib

from PIL import Image, ImageDraw, ImageFont

W, H = 1900, 1250
F = "C:/Windows/Fonts/segoeui.ttf"
FB = "C:/Windows/Fonts/segoeuib.ttf"
f_titre = ImageFont.truetype(FB, 40)
f_bloc = ImageFont.truetype(FB, 30)
f = ImageFont.truetype(F, 26)
f_pt = ImageFont.truetype(F, 22)
f_pin = ImageFont.truetype(FB, 24)

ROUGE, BLEU, VERT, ORANGE, VIOLET, GRIS, NOIR = "#d62728", "#1f77b4", "#2ca02c", "#ff7f0e", "#9467bd", "#888888", "#222222"
FIL = 6

TXT = {
    "": dict(
        titre="Prototype ESP32-C6 ↔ Rain Bird ESP-ME3 controller (two PC817C)", usb="(PC USB only)",
        c3v3="left column, 2nd", c18="right column, 12th", c19="right column, 11th", cgnd="right column, 14th",
        o1="Opto 1: TRANSMIT", o2="Opto 2: RECEIVE (turned 180°)", iso="isolation",
        prog="Controller", pads="J1 pads (LNK2 removed)",
        desc={5: "10 V AC: do not connect", 4: "J_RX  3.2 V", 3: "COMMON", 2: "J_TX  1.1 V", 1: "34.5 V AC: do not connect"},
        v15="(28 V AC between 1 and 5)",
        notes=["• Idle: GPIO18 high (LED 1 off, pad 4 stays at 3.2 V); LED 2 lit by the controller → GPIO19 ≈ 3.2 V.",
               "• No wire between the ESP32 ground and the controller: only the optocouplers link the two sides.",
               "• If GPIO19 stays below 2.5 V at idle, replace 1 kΩ with 2.2 kΩ. ESPHome: UART 9600 8N1, no inversion.",
               "• PC817C top view: dot = pin 1 (A), 2 = K next to it; opposite: 4 = C (facing 1), 3 = E. Opto 2 is turned 180° (pin 1 bottom right)."]),
    "_FR": dict(
        titre="Prototype ESP32-C6 ↔ programmateur Rain Bird ESP-ME3 (deux PC817C)", usb="(USB du PC seulement)",
        c3v3="colonne gauche, 2e", c18="colonne droite, 12e", c19="colonne droite, 11e", cgnd="colonne droite, 14e",
        o1="Opto 1 : ÉMISSION", o2="Opto 2 : RÉCEPTION (tourné de 180°)", iso="isolation",
        prog="Programmateur", pads="pastilles de J1 (LNK2 retiré)",
        desc={5: "10 V AC : ne pas brancher", 4: "J_RX  3,2 V", 3: "COMMUN", 2: "J_TX  1,1 V", 1: "34,5 V AC : ne pas brancher"},
        v15="(28 V AC entre 1 et 5)",
        notes=["• Repos : GPIO18 haut (LED 1 éteinte, pastille 4 reste à 3,2 V) ; LED 2 allumée par le programmateur → GPIO19 ≈ 3,2 V.",
               "• Aucun fil entre la masse de l'ESP32 et le programmateur : seuls les optocoupleurs relient les deux côtés.",
               "• Si GPIO19 reste sous 2,5 V au repos, remplacer 1 kΩ par 2,2 kΩ. ESPHome : UART 9600 8N1, sans inversion.",
               "• PC817C vu de dessus : point = broche 1 (A), 2 = K à côté ; en face : 4 = C (face au 1), 3 = E. Opto 2 posé tourné de 180° (broche 1 en bas à droite)."]),
}
PIN = {
    "": dict(t="PC817C pinout (top view)", a="anode (LED)", k="cathode (LED)", c="collector", e="emitter",
             bas="dot = pin 1  ·  LED side: 1-2  ·  transistor side: 4-3"),
    "_FR": dict(t="Brochage du PC817C (vu de dessus)", a="anode (LED)", k="cathode (LED)", c="collecteur", e="émetteur",
                bas="point = broche 1  ·  côté LED : 1-2  ·  côté transistor : 4-3"),
}
sortie = pathlib.Path(__file__).resolve().parent.parent / "images"
sortie.mkdir(exist_ok=True)
d = None


def fil(points, c):
    d.line(points, fill=c, width=FIL, joint="curve")


def point(x, y, c):
    d.ellipse((x - 10, y - 10, x + 10, y + 10), fill=c)


def texte(x, y, t, police=f, c=NOIR, ancre="la"):
    d.text((x, y), t, font=police, fill=c, anchor=ancre)


def resistance_h(x1, x2, y, c, etiquette):
    fil([(x1, y), (x1 + 30, y)], c)
    fil([(x2 - 30, y), (x2, y)], c)
    d.rectangle((x1 + 30, y - 20, x2 - 30, y + 20), outline=NOIR, width=4, fill="#fff7d6")
    texte((x1 + x2) // 2, y - 30, etiquette, f_pin, NOIR, "mb")


def resistance_v(x, y1, y2, c, etiquette):
    fil([(x, y1), (x, y1 + 30)], c)
    fil([(x, y2 - 30), (x, y2)], c)
    d.rectangle((x - 20, y1 + 30, x + 20, y2 - 30), outline=NOIR, width=4, fill="#fff7d6")
    texte(x + 30, (y1 + y2) // 2, etiquette, f_pin, NOIR, "lm")


def opto(x0, y0, titre, led_a_gauche, pins, retourne=False):
    """PC817C package: pins = {'hg','bg','hd','bd'} → label shown; LED on the given side."""
    x1, y1 = x0 + 300, y0 + 260
    d.rounded_rectangle((x0, y0, x1, y1), radius=18, outline=NOIR, width=5, fill="#f4f4f4")
    texte((x0 + x1) // 2, y0 - 12, titre, f_bloc, NOIR, "mb")
    texte((x0 + x1) // 2, y1 + 8, "PC817C", f_pt, GRIS, "ma")
    ya, yb = y0 + 50, y0 + 210
    # pins
    for (cle, x, y) in (("hg", x0, ya), ("bg", x0, yb), ("hd", x1, ya), ("bd", x1, yb)):
        num, nom = pins[cle]
        if x == x0:
            texte(x + 14, y, f"{num} {nom}", f_pin, NOIR, "lm")
        else:
            texte(x - 14, y, f"{nom} {num}", f_pin, NOIR, "rm")
    # LED
    xl = x0 + 95 if led_a_gauche else x1 - 95
    xt = x1 - 95 if led_a_gauche else x0 + 95
    d.line((xl, ya, xl, yb), fill=NOIR, width=3)
    if retourne:  # package turned 180°: anode at the bottom, LED pointing up
        d.polygon([(xl - 26, 150 + y0), (xl + 26, 150 + y0), (xl, 105 + y0)], outline=NOIR, fill="#ffd34d")
        d.line((xl - 26, 103 + y0, xl + 26, 103 + y0), fill=NOIR, width=4)
    else:
        d.polygon([(xl - 26, 105 + y0), (xl + 26, 105 + y0), (xl, 150 + y0)], outline=NOIR, fill="#ffd34d")
        d.line((xl - 26, 152 + y0, xl + 26, 152 + y0), fill=NOIR, width=4)
    sens = 1 if led_a_gauche else -1
    for dy in (110, 140):
        xa, xb = xl + sens * 34, xl + sens * 70
        d.line((xa, y0 + dy, xb, y0 + dy), fill=ORANGE, width=3)
        d.polygon([(xb, y0 + dy), (xb - sens * 12, y0 + dy - 7), (xb - sens * 12, y0 + dy + 7)], fill=ORANGE)
    # transistor
    d.line((xt, ya, xt, ya + 70), fill=NOIR, width=3)
    d.line((xt, yb - 70, xt, yb), fill=NOIR, width=3)
    d.line((xt - sens * 30, ya + 60, xt - sens * 30, yb - 60), fill=NOIR, width=5)
    d.line((xt - sens * 30, ya + 85, xt, ya + 70), fill=NOIR, width=3)
    d.line((xt - sens * 30, yb - 85, xt, yb - 70), fill=NOIR, width=3)
    if retourne:  # emitter at the top
        d.polygon([(xt, ya + 70), (xt - sens * 14, ya + 70), (xt - sens * 6, ya + 82)], fill=NOIR)
    else:
        d.polygon([(xt, yb - 70), (xt - sens * 14, yb - 70), (xt - sens * 6, yb - 82)], fill=NOIR)
    # internal links to the pins
    for (x, y) in ((x0, ya), (x0, yb), (x1, ya), (x1, yb)):
        cible = xl if (x == x0) == led_a_gauche else xt
        d.line((x, y, cible, y), fill=NOIR, width=3)
    return x0, x1, ya, yb


def cablage(L):
    global d
    im = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(im)
    texte(W // 2, 20, L["titre"], f_titre, NOIR, "ma")
    # ESP32-C6 board
    ex0, ex1 = 60, 380
    d.rounded_rectangle((ex0, 150, ex1, 1040), radius=22, outline=NOIR, width=5, fill="#e8f0fb")
    texte((ex0 + ex1) // 2, 170, "nanoESP32-C6", f_bloc, NOIR, "ma")
    texte((ex0 + ex1) // 2, 210, L["usb"], f_pt, GRIS, "ma")
    Y3V3, Y18, Y19, YGND = 300, 460, 760, 960
    for (y, nom, cote) in ((Y3V3, "3V3", L["c3v3"]), (Y18, "GPIO18 (TX)", L["c18"]),
                           (Y19, "GPIO19 (RX)", L["c19"]), (YGND, "GND", L["cgnd"])):
        texte(ex1 - 16, y - 4, nom, f_pin, NOIR, "rb")
        texte(ex1 - 16, y + 4, cote, f_pt, GRIS, "ra")
        d.rectangle((ex1 - 8, y - 8, ex1 + 8, y + 8), fill=NOIR)
    # Optocouplers
    o1 = opto(760, 250, L["o1"], True, {"hg": ("1", "A"), "bg": ("2", "K"), "hd": ("4", "C"), "bd": ("3", "E")})
    o2 = opto(760, 660, L["o2"], False, {"hg": ("3", "E"), "bg": ("4", "C"), "hd": ("2", "K"), "bd": ("1", "A")}, retourne=True)
    # Isolation barrier
    for y in range(240, 1040, 30):
        if 185 <= y <= 245 or 515 <= y <= 655 or 925 <= y <= 975:
            continue
        d.line((910, y, 910, y + 16), fill=GRIS, width=3)
    texte(910, 1065, L["iso"], f_pt, GRIS, "ma")
    # Controller connector
    cx0, cx1 = 1440, 1860
    d.rounded_rectangle((cx0, 150, cx1, 1040), radius=22, outline=NOIR, width=5, fill="#fdf0e6")
    texte((cx0 + cx1) // 2, 170, L["prog"], f_bloc, NOIR, "ma")
    texte((cx0 + cx1) // 2, 210, L["pads"], f_pt, GRIS, "ma")
    P = {5: 262, 4: 300, 3: 600, 2: 870, 1: 975}
    for n, y in P.items():
        c = ROUGE if n in (1, 5) else NOIR
        d.rectangle((cx0 - 10, y - 10, cx0 + 10, y + 10), fill=c)
        texte(cx0 + 22, y, f"{n}  {L['desc'][n]}", f_pin, c, "lm")
    texte(cx0 + 22, P[1] + 34, L["v15"], f_pt, ROUGE, "lm")
    # --- Wires on the ESP32 side ---
    ox0, ox1, a1, b1 = o1
    _, _, a2, b2 = o2
    fil([(ex1, Y3V3), (545, Y3V3), (545, a1), (ox0, a1)], ROUGE)  # 3V3 → opto1 A and opto2 C
    fil([(545, a1), (545, b2), (ox0, b2)], ROUGE)
    point(545, a1, ROUGE)
    texte(557, (a1 + a2) // 2, "3V3", f_pin, ROUGE, "lm")
    fil([(ex1, Y18), (600, Y18)], BLEU)  # GPIO18 → 220 Ω → opto1 K
    resistance_h(600, ox0, b1, BLEU, "220 Ω")
    fil([(ex1, Y19), (700, Y19), (700, a2), (ox0, a2)], VERT)  # GPIO19 ← opto2 E, 1 kΩ to GND
    point(430, Y19, VERT)
    resistance_v(430, Y19, YGND, VERT, "1 kΩ")
    fil([(ex1, YGND), (430, YGND)], NOIR)
    # --- Wires on the controller side ---
    fil([(ox1, a1), (1300, a1), (1300, P[4]), (cx0, P[4])], ORANGE)  # opto1 C → pad 4
    fil([(ox1, b1), (1360, b1), (1360, P[3]), (cx0, P[3])], VIOLET)  # opto1 E → pad 3
    fil([(ox1, a2), (1360, a2), (1360, P[3])], VIOLET)  # opto2 K → pad 3
    point(1360, P[3], VIOLET)
    fil([(ox1, b2), (cx0, P[2])], ORANGE)  # opto2 A → pad 2
    for i, t in enumerate(L["notes"]):
        texte(60, 1100 + i * 36, t, f_pt, NOIR)
    return im


def brochage(L):
    global d
    im = Image.new("RGB", (1000, 430), "white")
    d = ImageDraw.Draw(im)
    texte(500, 20, L["t"], f_titre, NOIR, "ma")
    x0, y0, x1, y1 = 380, 110, 620, 340
    d.rounded_rectangle((x0, y0, x1, y1), radius=16, outline=NOIR, width=5, fill="#f4f4f4")
    d.ellipse((x0 + 22, y0 + 22, x0 + 50, y0 + 50), fill=NOIR)
    for (n, y, cote, nom) in ((1, y0 + 60, "g", L["a"]), (2, y1 - 60, "g", L["k"]),
                              (4, y0 + 60, "d", L["c"]), (3, y1 - 60, "d", L["e"])):
        if cote == "g":
            d.rectangle((x0 - 60, y - 12, x0, y + 12), fill="#b0b0b0", outline=NOIR, width=3)
            texte(x0 - 75, y, f"{n}  {nom}", f_bloc, NOIR, "rm")
        else:
            d.rectangle((x1, y - 12, x1 + 60, y + 12), fill="#b0b0b0", outline=NOIR, width=3)
            texte(x1 + 75, y, f"{nom}  {n}", f_bloc, NOIR, "lm")
    texte(500, y1 + 22, L["bas"], f_pt, GRIS, "ma")
    return im


for suffixe in ("", "_FR"):
    cablage(TXT[suffixe]).save(sortie / f"wiring_prototype{suffixe}.png")
    brochage(PIN[suffixe]).save(sortie / f"pc817_pinout{suffixe}.png")
print("ok")
