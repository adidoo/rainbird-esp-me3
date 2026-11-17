# Drives the rainbird-me3 board through the ESPHome API and prints its logs.
# Steps given as arguments, run in order:
#   ecoute:on | ecoute:off      « Écoute seule » switch
#   brut:<hex>                  envoyer_brut action (read), e.g. brut:06F9AF8EBE
#   attendre:<s>                pause in seconds
# The API key is read from secrets.yaml and never printed.
import asyncio
import re
import sys
import time

from aioesphomeapi import APIClient, LogLevel, SwitchInfo

from rb_zone_cle import HOTE


def cle_api():
    for ligne in open("secrets.yaml", encoding="utf-8"):
        m = re.match(r'\s*rainbird_api_key\s*:\s*"?([^"\s]+)"?', ligne)
        if m:
            return m.group(1)
    raise SystemExit("clé rainbird_api_key introuvable")


async def main(etapes):
    client = APIClient(HOTE, 6053, None, noise_psk=cle_api())
    await client.connect(login=True)
    entites, services = await client.list_entities_services()
    ecoute = next(e for e in entites if isinstance(e, SwitchInfo) and "coute" in e.name)
    brut = next(s for s in services if s.name == "envoyer_brut")
    t0 = time.monotonic()

    def journal(msg):
        texte = re.sub(r"\x1b\[[0-9;]*m", "", msg.message.decode("utf-8", "replace"))
        if "rainbird_me3" in texte:
            print(f"{time.monotonic() - t0:6.1f}s {texte}", flush=True)

    client.subscribe_logs(journal, log_level=LogLevel.LOG_LEVEL_DEBUG)
    for etape in etapes:
        cle, _, val = etape.partition(":")
        print(f"{time.monotonic() - t0:6.1f}s >>> {etape}", flush=True)
        if cle == "ecoute":
            client.switch_command(ecoute.key, val == "on")
        elif cle == "brut":
            await client.execute_service(brut, {"octets": val, "ecriture": False})
        elif cle == "attendre":
            await asyncio.sleep(float(val))
    await asyncio.sleep(1)
    await client.disconnect()


asyncio.run(main(sys.argv[1:]))
