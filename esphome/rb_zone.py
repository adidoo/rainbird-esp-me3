# Starts a zone through the ESPHome API of the rainbird-me3 board, follows the logs, then closes the safeguard.
# usage: rb_zone.py <zone> <minutes> [follow time in s]
import asyncio, re, sys, time
from aioesphomeapi import APIClient, LogLevel, SwitchInfo

from rb_zone_cle import HOTE

def cle_api():
    for ligne in open("secrets.yaml", encoding="utf-8"):
        m = re.match(r'\s*rainbird_api_key:\s*"?([^"\s]+)"?', ligne)
        if m:
            return m.group(1)
    raise SystemExit("clé rainbird_api_key introuvable")

async def main(zone, minutes, suivi):
    client = APIClient(HOTE, 6053, None, noise_psk=cle_api())
    await client.connect(login=True)
    entites, services = await client.list_entities_services()
    garde = next(e for e in entites if isinstance(e, SwitchInfo) and "Commandes" in e.name)
    lancer = next(s for s in services if s.name == "lancer_zone")
    t0 = time.monotonic()
    vus = {}
    def journal(msg):
        texte = re.sub(r"\x1b\[[0-9;]*m", "", msg.message.decode("utf-8", "replace"))
        if "rainbird_me3" not in texte:
            return
        # only print commands, errors and state changes
        m = re.search(r"← ((BF|C8|BB) .*)", texte)
        if m:
            if vus.get(m.group(2)) == m.group(1):
                return
            vus[m.group(2)] = m.group(1)
        elif not re.search(r"39 |accept|refus|Pas de r|Session|Poign|CRC|→ 40|01 40", texte):
            return
        print(f"{time.monotonic() - t0:6.1f}s {time.strftime('%H:%M:%S')} {texte}", flush=True)
    client.subscribe_logs(journal, log_level=LogLevel.LOG_LEVEL_DEBUG)
    await asyncio.sleep(3)
    client.switch_command(garde.key, True)
    await asyncio.sleep(1)
    print(f"{time.monotonic() - t0:6.1f}s >>> lancer_zone {zone} / {minutes} min", flush=True)
    await client.execute_service(lancer, {"zone": zone, "minutes": minutes})
    await asyncio.sleep(suivi)
    client.switch_command(garde.key, False)
    print(f"{time.monotonic() - t0:6.1f}s >>> garde-fou refermé", flush=True)
    await asyncio.sleep(1)
    await client.disconnect()

asyncio.run(main(int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]) if len(sys.argv) > 3 else 60))
