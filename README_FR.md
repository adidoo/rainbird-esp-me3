# Rain Bird ESP-ME3 sans LNK2

*English: [README.md](README.md)*

Remplacement du module WiFi Rain Bird LNK2 d'un programmateur **ESP-ME3** (modèle `0x0009`, firmware 2.12)
par un **ESP32-C6 sous ESPHome**, piloté localement depuis Home Assistant. Ni cloud, ni appli Rain Bird.

Contexte et discussion : fil de la communauté Home Assistant
[Reverse engineering ACCESSORY port – Rain Bird ESP-RZX](https://community.home-assistant.io/t/reverse-engineering-accessory-port-rain-bird-esp-rzx/998452).
Pour l'ESP-TM2, voir [sjwright/rainbird-esphome](https://github.com/sjwright/rainbird-esphome).

**État : projet personnel, en service sur mon programmateur.** Lecture, lancement d'une zone et écriture des réglages
de programme essayés ; les autres commandes sont en place mais pas toutes essayées.

## Fonctionnement de la liaison (ESP-ME3)

1. À la mise sous tension, le programmateur parle **Manchester 2400 bit/s** (cadrage TM2 `F5 F5 1C long données somme 1D`)
   et refuse toute commande (`00 <op> 01`) tant qu'aucune session n'est ouverte.
2. Poignée de main : `06 A` → `86 T(A) B` → `07 T(B)` → `87`, T = 13 pas d'un registre à décalage 32 bits de masque
   `0x80718611` (publié au message n° 18 du fil ci-dessus).
3. **`0E 02` dans cette session Manchester fait passer le programmateur en UART 9600 8N1**, où il reste jusqu'à la
   coupure de courant suivante.
4. En UART (trame `1C long 00 données crc8 1D`), même poignée de main, puis fonctionnement normal.

Le composant fait tout cela seul, y compris après une coupure de courant. Détails : `PROTOCOLE_ESP-ME3_FR.md`.

## Contenu

| Chemin | Rôle |
|---|---|
| `PROTOCOLE_ESP-ME3_FR.md` | Protocole : trame UART, CRC-8 0x07, commandes SIP, protocole d'objets `0C`/`8C` |
| `CABLAGE_PROTOTYPE_FR.md`, `images/wiring_prototype_FR.png` | Interface à deux optocoupleurs PC817 entre le connecteur J1 et l'ESP32 |
| `esphome/components/rainbird_me3/` | Composant ESPHome : liaison (réveil Manchester, poignée de main, UART), commandes, file d'arrosage avec temps restant, lecture et écriture des programmes |
| `esphome/rainbird-me3.yaml` | Configuration de la carte (nécessite un `secrets.yaml` non fourni) |
| `esphome/gen_entites.py` → `rainbird-me3-entites.yaml` | Génère les entités : zones, boutons, éditeur de programme |
| `carte_ha/rainbird-card.js` | Carte Lovelace `custom:rainbird-card` : état, zone en cours et temps restant, file, zones, programmes avec éditeur, plan isométrique du jardin en option |
| `gen_vue_arrosage.py` | Vue « Arrosage » du tableau de bord à partir des entités |
| `outils/` | Décodeurs et outils d'écoute de la ligne (Raspberry Pi), test de la transformée du handshake |
| `esphome/rb_*.py` | Essais par l'API ESPHome depuis un PC (`rainbird_host` et `rainbird_api_key` dans `secrets.yaml`) |

## Carte Lovelace

Copier `carte_ha/rainbird-card.js` dans `/config/www/`, ajouter la ressource `/local/rainbird-card.js` (module), puis :

```yaml
type: custom:rainbird-card
prefix: jardin_rain_bird_esp_me3_   # préfixe des entités de l'appareil ESPHome
zones:                              # noms des zones (facultatif)
  1: Pelouse devant
plan: /local/rainbird-plan.json     # plan du jardin (facultatif ; JSON : parcelle, pelouse, allées, bâtiments, arbres, buses)
```

Le fichier du plan est propre à chaque jardin et n'est pas fourni.

## Compilation

Depuis PowerShell : `esphome compile esphome/rainbird-me3.yaml`, puis flasher `firmware.factory.bin` à l'adresse `0x0`.

## Licence

MIT, voir `LICENSE`.
