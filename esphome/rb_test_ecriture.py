# Reversible write test: seasonal adjustment of program D (unused) 100 -> 105 -> 100.
import asyncio, re, time
from aioesphomeapi import APIClient, LogLevel
from rb_zone_cle import cle_api, HOTE
async def main():
    c = APIClient(HOTE, 6053, None, noise_psk=cle_api())
    await c.connect(login=True)
    ents, _ = await c.list_entities_services()
    k = {e.name: e for e in ents}
    t0 = time.monotonic()
    def journal(m):
        t = re.sub(r"\x1b\[[0-9;]*m", "", m.message.decode("utf-8", "replace"))
        if "rainbird_me3" in t and re.search(r"05 0B|06 0B|refus|accept|Écriture|inattendu|Pas de r| 18 00 01 00 00 03 00 02", t):
            print(f"{time.monotonic()-t0:5.1f}s {t[:210]}", flush=True)
    c.subscribe_logs(journal, log_level=LogLevel.LOG_LEVEL_DEBUG)
    c.switch_command(k["Commandes autorisées"].key, True)
    c.select_command(k["Programme édité"].key, "D")
    await asyncio.sleep(3)
    for v in (105, 100):
        print(f"{time.monotonic()-t0:5.1f}s >>> ajustement saisonnier D = {v}", flush=True)
        c.number_command(k["Ajustement saisonnier"].key, v)
        await asyncio.sleep(8)
    c.select_command(k["Programme édité"].key, "A")
    c.switch_command(k["Commandes autorisées"].key, False)
    await asyncio.sleep(1)
    await c.disconnect()
asyncio.run(main())
