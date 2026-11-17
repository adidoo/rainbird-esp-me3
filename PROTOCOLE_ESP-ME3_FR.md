# Protocole série LNK2 ↔ Rain Bird ESP-ME3 (relevé le 01/11/2026)

*English: [PROTOCOLE_ESP-ME3.md](PROTOCOLE_ESP-ME3.md)*

Relevé par écoute passive (UART du Pi du garage) d'un ESP-ME3 (modèle 0x0009, firmware 2.12) et de son
module WiFi LNK2 (firmware 3.97), en corrélant avec des actions faites dans l'appli Rain Bird 2.17.14 et
dans Home Assistant (pyrainbird). Journaux bruts : `~/rainbird/captures/` sur le Pi ; extraits dans `outils/`.

## 1. Couche physique

- UART asynchrone **9600 bauds, 8N1, LSB d'abord, repos à l'état haut**, niveaux 3,3 V côté ESP32 du LNK2.
  (Une fois la session ouverte ; à la mise sous tension la ligne est en Manchester, voir 1.1.)
- LNK2 : émission sur GPIO26 (via LED de l'optocoupleur U3, anode au 3V3, 200 Ω, actif bas),
  réception sur GPIO25 (transistor de U2, collecteur au 3V3, émetteur → GPIO25, repos haut).
- Le LNK2 est le maître : une requête, puis la réponse du programmateur 6 à 40 ms après.

### 1.1 Démarrage : Manchester, puis passage en UART (essayé le 17/11/2026)

- À la mise sous tension, le programmateur ne parle que **Manchester 2400 bit/s** (un bit = deux demi-bits de 208 µs ;
  cadrage de l'ESP-TM2 `F5 F5 1C <long> <données> <somme> 1D`, somme = long + octets de données). L'UART 9600 reste sans réponse.
- Hors session, toute commande est refusée par `00 <op> 01`.
- Poignée de main `06 A` → `86 T(A) B` → `07 T(B)` → `87`, T() = 13 pas d'un registre à décalage 32 bits, bit entrant =
  parité de `x & 0x80718611`, un résultat nul remplacé par `FFFFFFFF` (message n° 18 du fil HA 998452).
- **`0E 02` → `8E 02` dans cette session fait passer la ligne en UART 9600**, jusqu'à la coupure de courant suivante.
  `04 04` et `0D` sont acceptés mais inutiles. Hors session, `0E 02` est refusé et rien ne change.
- Un `06` n'est servi qu'une fois : après un `06` resté sans `07`, les `06` suivants sont ignorés un moment.
- En UART, la même poignée de main rouvre la session.
- Un Manchester émis et lu en logiciel ne doit pas être interrompu (le WiFi fait perdre des fronts) : émission et capture
  interruptions coupées.

## 2. Trame

```
1C <len> 00 <payload…> <crc> 1D
```
- `len` = nombre d'octets du payload ; `00` = octet de rôle inconnu (toujours 00 observé).
- `crc` = CRC-8, polynôme 0x07, valeur initiale 0x00, sur `len 00 payload` (avant échappement).
- Échappement de tout octet 0x1B/0x1C/0x1D entre `1C` et `1D` : `1B 01` = 0x1C, `1B 02` = 0x1B, `1B 03` = 0x1D.

## 3. Commandes classiques (= commandes SIP de pyrainbird)

Requête `<op> [args]` ; réponse `<op|0x80> …`, ou acquittement `01 <op>`, ou refus `00 <op> …`.

| Op | Nom | Requête | Réponse observée |
|---|---|---|---|
| 02 | ModelAndVersion | `02` | `82 00 09 02 0C` (modèle 0x0009, v2.12) |
| 03 | AvailableStations | `03 <page>` | `83 00 FF 1F 00 00` (page 0 : 13 stations) ; page invalide → `83 FF …` |
| 04 | CommandSupport | `04 <op>` | `84 <op> 01` |
| 05 | SerialNumber | `05` | `85 <8 octets>` |
| 0B | ControllerFirmwareVersion | `0B` | `8B 03 0B 00 01 B1 00 19 00` |
| 0D | ? (démarrage) | `0D` | `8D 04 04` |
| 0E | ? (démarrage) | `0E 02` | `8E 02` |
| 10 | CurrentTime | `10` | `90 hh mm ss` |
| 12 | CurrentDate | `12` | `92 jj Mm AA` (jour ; mois = quartet haut ; année sur 12 bits : `01 B7 EA` = 01/11/2026) |
| 30 | WaterBudget | `30 <prog>` | `B0 <prog> 00 <pct>` (= objet 18) |
| 36 | RainDelayGet | `36` | `B6 00 <jours>` (= objet 14) |
| 38 | ManuallyRunProgram | `38 <prog>` (A = 0) | `01 38` |
| 39 | ManuallyRunStation | `39 <zone16> <min>` | `01 39` |
| 3A | TestStations | `3A <min>` | `01 3A` (« exécution rapide » de l'appli) |
| 3B | CurrentQueue | `3B <page 0..3>` | page 0 état, pages 1-3 file (`02 <zone> <s32>` …) |
| 3D | défauts de station ? (page, écho) | `3D <page 0..1>` | `BD <page> 00 00 00 00` (toujours 0 ; masque de stations en défaut ?) |
| 3E | CurrentRainSensorState | `3E` | `BE 00` |
| 3F | CurrentStationsActive | `3F <page>` | `BF <page> <masque 32 bits LE>` (zone 1 = bit 0) |
| 40 | StopIrrigation | `40` | `01 40` |
| 42 | AdvanceStation | `42 00` | `01 42` |
| 48 | CurrentIrrigationState | `48` | `C8 00` (sélecteur OFF) / `C8 01` (AUTO) |
| 4A | ControllerEventTimestamp | `4A 01` | `CA 01 <4 o>` |
| 4B | StackManuallyRunStation | `4B <zone16> <min>` | `01 4B` (« arrosage personnalisé », une par zone) |
| 62 | débit ? | `62` | `E2 FF` (toujours ; pas de sonde de débit : valeur indisponible ?) |
| 63 | lecture sonde de débit | `63` | `E3 <installée 0/1> 00` |
| 64 | écriture sonde de débit | `64 <installée 0/1> 00` | `01 64` |
| 0C | enveloppe du protocole d'objets | voir § 4 | `8C …` |

Cycle d'interrogation du LNK2 (toutes les ~13 s) : `12 10 0B 03 00 30 0..3 3B 00 3B 01 62 63 3F 00 3F 01
3D 0..2 36 3E 4A 01 48 0C(0D)` ; pendant un arrosage aussi `3B 02` `3B 03`.

## 4. Protocole d'objets (enveloppe 0C / 8C)

```
requête  0C 20 00 | 01 00 <dst 4 o> 00 00 <src 4 o> 00 00 05 00 00 00 | len16 LE | contenu
réponse  8C 20 00 | 00 00 <src 4 o> 00 00 <dst 4 o> 00 00 05 00 <FF FF ou xx xx> | len16 LE | contenu
```
- dst `08 05 00 00`, src `0C 00 00 00` : configuration (ops 07/08 lecture, 05/06 écriture).
- dst `01 01 00 00`, src `0C 00 00 00` : variante « module » (ops 03/04 lecture, 01/02 écriture), utilisée par
  l'écran Paramètres de l'appli (sonde de pluie).
- dst `01 01 00 00`, src `0E 00 00 00` : journal d'événements (`83 0B …` → `84 0B …`, 95 octets).

Contenu :
```
lecture   07 0B <n> <descr>×n                      → 08 0B <n> (<descr> <type> <valeurs>)×n
écriture  05 0B <n> (<descr> <type> <valeurs>)×n   → 06 0B 00
descr     objet16 LE, ndim (1 o), puis ndim × (début16 LE, fin16 LE)
type      01 = 8 bits, 02 = 16 bits, 04 = 32 bits ; valeurs petit-boutistes, produit des tailles des dimensions
```

| Objet | Dimensions | Sens |
|---|---|---|
| 0A | zone[0..21] | cycle (min) du « cycle + ressuyage » |
| 0B | zone[0..21] | ressuyage (min) |
| 0D | — | sonde de pluie active (1) / contournée (0) |
| 10 | prog[0..3] × jour[0..6] | jours d'arrosage, dimanche d'abord (mode personnalisé) |
| 11 | prog[0..3] | jours restants avant le prochain arrosage en mode cyclique (vérifié sur PGM C : 2) |
| 12 | prog[0..3] | intervalle du mode cyclique en jours (3 par défaut ; vérifié sur PGM C : 5) |
| 13 | prog[0..3] | mode : 0 personnalisé, 2 impair, 3 pair, 4 cyclique |
| 14 | — | report d'arrosage en jours (= commande 36) |
| 15 | prog × zone[0..21] | durée en secondes (32 bits) |
| 18 | prog[0..3] | ajustement saisonnier en % (16 bits) |
| 1D | prog × [0] × départ[0..5] | heures de départ en minutes depuis minuit, 1440 = aucune |

Le LNK2 relit toute la configuration à son démarrage et après une écriture « module », puis interroge 0D
en boucle (~15-20 s) et le journal toutes les ~2 min.

Aucun indicateur « programme actif » : l'appli n'affiche un programme que s'il a au moins une heure de départ.
Un programme non utilisé garde ses valeurs (C, D : 18 = 100, 13 = 2, 12 = 3, 11 = 0, 1D = 1440, 15 = 0).

## 5. Ce qui ne passe pas par le programmateur

Noms de zones, zone « câblée », type de sol, ajustement saisonnier automatique (calculé ailleurs puis écrit
dans 18), calendrier, notifications, paramètres WiFi : gérés par l'appli / le LNK2 / le cloud.
