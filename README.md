# Rain Bird ESP-ME3 without the LNK2

*Français : [README_FR.md](README_FR.md)*

Replacement of the Rain Bird LNK2 WiFi module of an **ESP-ME3** irrigation controller (model `0x0009`, firmware 2.12)
by an **ESP32-C6 running ESPHome**, controlled locally from Home Assistant. No cloud, no Rain Bird app.

Background and discussion: Home Assistant community thread
[Reverse engineering ACCESSORY port – Rain Bird ESP-RZX](https://community.home-assistant.io/t/reverse-engineering-accessory-port-rain-bird-esp-rzx/998452).
For the ESP-TM2, see [sjwright/rainbird-esphome](https://github.com/sjwright/rainbird-esphome).

**Status: personal project, working on my controller.** Reading, running a station and writing program settings have
been tested; other commands are implemented but not all tested yet.

## How the link works (ESP-ME3)

1. After power-up the controller speaks **Manchester 2400 bit/s** (TM2 framing `F5 F5 1C len payload sum 1D`) and
   refuses every command (`00 <op> 01`) until a session is open.
2. Session handshake: `06 A` → `86 T(A) B` → `07 T(B)` → `87`, where T = 13 steps of a 32-bit shift register with mask
   `0x80718611` (published in post #18 of the thread above).
3. **`0E 02` in that Manchester session switches the controller to UART 9600 8N1**, where it stays until the next
   power cut.
4. In UART (frame `1C len 00 payload crc8 1D`), the same handshake again, then normal operation.

The component does all of this by itself, including after a power cut. Details: `PROTOCOLE_ESP-ME3.md`.

## Contents

| Path | Purpose |
|---|---|
| `PROTOCOLE_ESP-ME3.md` | Protocol: UART frame, CRC-8 0x07, SIP commands, `0C`/`8C` object protocol |
| `CABLAGE_PROTOTYPE.md`, `images/wiring_prototype.png` | Interface with two PC817 optocouplers between connector J1 and the ESP32 |
| `esphome/components/rainbird_me3/` | ESPHome component: link (Manchester wake-up, handshake, UART), commands, irrigation queue with remaining time, reading and writing programs |
| `esphome/rainbird-me3.yaml` | Board configuration (needs a `secrets.yaml`, not included) |
| `esphome/gen_entites.py` → `rainbird-me3-entites.yaml` | Generates the entities: zones, buttons, program editor |
| `carte_ha/rainbird-card.js` | Lovelace card `custom:rainbird-card`: status, running zone with remaining time, queue, zones, programs with an editor, optional isometric garden plan |
| `gen_vue_arrosage.py` | "Irrigation" dashboard view built from the entities |
| `outils/` | Line decoders and listening tools (Raspberry Pi), test of the handshake transform |
| `esphome/rb_*.py` | Tests through the ESPHome API from a PC (`rainbird_host` and `rainbird_api_key` in `secrets.yaml`) |

## Lovelace card

Copy `carte_ha/rainbird-card.js` to `/config/www/`, add the resource `/local/rainbird-card.js` (module), then:

```yaml
type: custom:rainbird-card
prefix: jardin_rain_bird_esp_me3_   # entity prefix of your ESPHome device
zones:                              # optional zone names
  1: Front lawn
plan: /local/rainbird-plan.json     # optional garden plan (JSON: parcel, lawn, paths, buildings, trees, nozzles)
```

The garden plan file is specific to each garden and is not included.

## Build

From PowerShell: `esphome compile esphome/rainbird-me3.yaml`, then flash `firmware.factory.bin` at address `0x0`.

## License

MIT, see `LICENSE`.
