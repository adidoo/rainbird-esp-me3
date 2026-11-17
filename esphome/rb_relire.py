# Reads the controller configuration again and prints the raw replies for objects 18/13/12/11/10 (read only).
import asyncio, re
from aioesphomeapi import APIClient, LogLevel, ButtonInfo
from rb_zone_cle import cle_api, HOTE
async def main():
    c = APIClient(HOTE, 6053, None, noise_psk=cle_api())
    await c.connect(login=True)
    ents, _ = await c.list_entities_services()
    b = next(e for e in ents if isinstance(e, ButtonInfo) and "Relire" in e.name)
    def journal(m):
        t = re.sub(r"\x1b\[[0-9;]*m", "", m.message.decode("utf-8", "replace"))
        if re.search(r"← 8C .*08 0B 0[34] 1[42]|Configuration du|refus|Pas de r", t):
            print(t[:330], flush=True)
    c.subscribe_logs(journal, log_level=LogLevel.LOG_LEVEL_DEBUG)
    await asyncio.sleep(1)
    c.button_command(b.key)
    await asyncio.sleep(8)
    await c.disconnect()
asyncio.run(main())
