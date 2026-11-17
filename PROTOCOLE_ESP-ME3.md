# LNK2 ↔ Rain Bird ESP-ME3 serial protocol (recorded on 01/11/2026)

*Français : [PROTOCOLE_ESP-ME3_FR.md](PROTOCOLE_ESP-ME3_FR.md)*

Recorded by passive listening (UARTs of the garage Raspberry Pi) to an ESP-ME3 (model 0x0009, firmware 2.12) and its
LNK2 WiFi module (firmware 3.97), correlated with actions made in the Rain Bird app 2.17.14 and in Home Assistant
(pyrainbird). Raw logs: `~/rainbird/captures/` on the Pi; extracts in `outils/`.

## 1. Physical layer

- Asynchronous UART, **9600 baud, 8N1, LSB first, idle high**, 3.3 V levels on the LNK2's ESP32 side.
  (Once the session is open; at power-up the line is Manchester, see 1.1.)
- LNK2: transmits on GPIO26 (through the LED of optocoupler U3, anode to 3V3, 200 Ω, active low),
  receives on GPIO25 (transistor of U2, collector to 3V3, emitter → GPIO25, idle high).
- The LNK2 is the master: one request, then the controller's reply 6 to 40 ms later.

### 1.1 Start-up: Manchester, then switch to UART (tested 17/11/2026)

- After power-up the controller speaks **Manchester 2400 bit/s only** (one bit = two 208 µs halves; ESP-TM2 framing
  `F5 F5 1C <len> <payload> <sum> 1D`, sum = len + payload bytes). UART 9600 gets no reply.
- Without a session every command is refused with `00 <op> 01`.
- Session handshake `06 A` → `86 T(A) B` → `07 T(B)` → `87`, T() = 13 steps of a 32-bit shift register, feedback bit =
  parity of `x & 0x80718611`, an all-zero result replaced by `FFFFFFFF` (post #18 of the HA community thread 998452).
- **`0E 02` → `8E 02` inside that session switches the line to UART 9600**, until the next power cut. `04 04` and `0D`
  are accepted but not needed. Without a session, `0E 02` is refused and nothing changes.
- A `06` is only served once: after a `06` that never got its `07`, further `06` frames are ignored for a while.
- In UART, the same handshake opens the session again.
- Bit-banged Manchester must not be interrupted (WiFi interrupts lose edges): transmit and capture with interrupts off.

## 2. Frame

```
1C <len> 00 <payload…> <crc> 1D
```
- `len` = number of payload bytes; `00` = unknown role byte (always 00 so far).
- `crc` = CRC-8, polynomial 0x07, initial value 0x00, over `len 00 payload` (before escaping).
- Escaping of any 0x1B/0x1C/0x1D byte between `1C` and `1D`: `1B 01` = 0x1C, `1B 02` = 0x1B, `1B 03` = 0x1D.

## 3. Classic commands (= pyrainbird SIP commands)

Request `<op> [args]`; reply `<op|0x80> …`, or acknowledgement `01 <op>`, or refusal `00 <op> …`.

| Op | Name | Request | Observed reply |
|---|---|---|---|
| 02 | ModelAndVersion | `02` | `82 00 09 02 0C` (model 0x0009, v2.12) |
| 03 | AvailableStations | `03 <page>` | `83 00 FF 1F 00 00` (page 0: 13 stations); invalid page → `83 FF …` |
| 04 | CommandSupport | `04 <op>` | `84 <op> 01` |
| 05 | SerialNumber | `05` | `85 <8 bytes>` |
| 0B | ControllerFirmwareVersion | `0B` | `8B 03 0B 00 01 B1 00 19 00` |
| 0D | ? (start-up) | `0D` | `8D 04 04` |
| 0E | ? (start-up) | `0E 02` | `8E 02` |
| 10 | CurrentTime | `10` | `90 hh mm ss` |
| 12 | CurrentDate | `12` | `92 dd Mm YY` (day; month = high nibble; year on 12 bits: `01 B7 EA` = 01/11/2026) |
| 30 | WaterBudget | `30 <prog>` | `B0 <prog> 00 <pct>` (= object 18) |
| 36 | RainDelayGet | `36` | `B6 00 <days>` (= object 14) |
| 38 | ManuallyRunProgram | `38 <prog>` (A = 0) | `01 38` |
| 39 | ManuallyRunStation | `39 <zone16> <min>` | `01 39` |
| 3A | TestStations | `3A <min>` | `01 3A` ("quick run" in the app) |
| 3B | CurrentQueue | `3B <page 0..3>` | page 0 status, pages 1-3 queue (`02 <zone> <s32>` …) |
| 3D | station faults? (page, echo) | `3D <page 0..1>` | `BD <page> 00 00 00 00` (always 0; mask of faulty stations?) |
| 3E | CurrentRainSensorState | `3E` | `BE 00` |
| 3F | CurrentStationsActive | `3F <page>` | `BF <page> <32-bit LE mask>` (zone 1 = bit 0) |
| 40 | StopIrrigation | `40` | `01 40` |
| 42 | AdvanceStation | `42 00` | `01 42` |
| 48 | CurrentIrrigationState | `48` | `C8 00` (dial on OFF) / `C8 01` (AUTO) |
| 4A | ControllerEventTimestamp | `4A 01` | `CA 01 <4 bytes>` |
| 4B | StackManuallyRunStation | `4B <zone16> <min>` | `01 4B` ("custom watering", one per zone) |
| 62 | flow? | `62` | `E2 FF` (always; no flow sensor: value unavailable?) |
| 63 | flow sensor read | `63` | `E3 <installed 0/1> 00` |
| 64 | flow sensor write | `64 <installed 0/1> 00` | `01 64` |
| 0C | object protocol envelope | see § 4 | `8C …` |

LNK2 polling cycle (every ~13 s): `12 10 0B 03 00 30 0..3 3B 00 3B 01 62 63 3F 00 3F 01
3D 0..2 36 3E 4A 01 48 0C(0D)`; during irrigation also `3B 02` `3B 03`.

## 4. Object protocol (0C / 8C envelope)

```
request  0C 20 00 | 01 00 <dst 4 B> 00 00 <src 4 B> 00 00 05 00 00 00 | len16 LE | content
reply    8C 20 00 | 00 00 <src 4 B> 00 00 <dst 4 B> 00 00 05 00 <FF FF or xx xx> | len16 LE | content
```
- dst `08 05 00 00`, src `0C 00 00 00`: configuration (ops 07/08 read, 05/06 write).
- dst `01 01 00 00`, src `0C 00 00 00`: "module" variant (ops 03/04 read, 01/02 write), used by the app's
  Settings screen (rain sensor).
- dst `01 01 00 00`, src `0E 00 00 00`: event log (`83 0B …` → `84 0B …`, 95 bytes).

Content:
```
read      07 0B <n> <descr>×n                      → 08 0B <n> (<descr> <type> <values>)×n
write     05 0B <n> (<descr> <type> <values>)×n    → 06 0B 00
descr     16-bit LE object, ndim (1 B), then ndim × (start16 LE, end16 LE)
type      01 = 8 bits, 02 = 16 bits, 04 = 32 bits; little-endian values, product of the dimension sizes
```

| Object | Dimensions | Meaning |
|---|---|---|
| 0A | zone[0..21] | cycle (min) of "cycle + soak" |
| 0B | zone[0..21] | soak (min) |
| 0D | — | rain sensor active (1) / bypassed (0) |
| 10 | prog[0..3] × day[0..6] | watering days, Sunday first (custom mode) |
| 11 | prog[0..3] | days left before the next watering in cyclic mode (checked on PGM C: 2) |
| 12 | prog[0..3] | cyclic interval in days (3 by default; checked on PGM C: 5) |
| 13 | prog[0..3] | mode: 0 custom, 2 odd, 3 even, 4 cyclic |
| 14 | — | rain delay in days (= command 36) |
| 15 | prog × zone[0..21] | duration in seconds (32 bits) |
| 18 | prog[0..3] | seasonal adjustment in % (16 bits) |
| 1D | prog × [0] × start[0..5] | start times in minutes after midnight, 1440 = none |

The LNK2 reads the whole configuration back at start-up and after a "module" write, then polls 0D in a loop
(~15-20 s) and the log every ~2 min.

There is no "program active" flag: the app only shows a program if it has at least one start time.
An unused program keeps its values (C, D: 18 = 100, 13 = 2, 12 = 3, 11 = 0, 1D = 1440, 15 = 0).

## 5. What does not go through the controller

Zone names, "wired" zone, soil type, automatic seasonal adjustment (computed elsewhere then written to 18), calendar,
notifications, WiFi settings: handled by the app / the LNK2 / the cloud.
