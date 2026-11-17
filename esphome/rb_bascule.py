# Test of the ESP-ME3 Manchester → UART switch (17/11/2026).
#   rb_bascule.py preparer            « Écoute seule » on: the board no longer sends anything by itself
#   rb_bascule.py etapes <step>…      runs the steps in order, stops at the first UART probe that gets a reply
#   rb_bascule.py remettre            « Écoute seule » off: back to normal operation
# Steps: sonde (UART « 06 » + random challenge, checks whether anything comes back), poignee (full Manchester
# handshake), m:<hex> (raw Manchester frame, e.g. m:0E02), coupure / attendre (wait for a power cut / the return).
import asyncio, re, sys, time, pathlib
from aioesphomeapi import APIClient, LogLevel, SwitchInfo
from rb_zone_cle import cle_api, HOTE

journal_fichier = pathlib.Path(__file__).parent / "captures" / time.strftime("bascule_%Y%m%d_%H%M%S.log")


async def main(cmd, etapes):
    c = APIClient(HOTE, 6053, None, noise_psk=cle_api())
    await c.connect(login=True)
    ents, services = await c.list_entities_services()
    ecoute = next(e for e in ents if isinstance(e, SwitchInfo) and "coute" in e.name)
    svc = {s.name: s for s in services}
    lignes, recu = [], {"uart": False}
    journal_fichier.parent.mkdir(exist_ok=True)
    t0 = time.monotonic()

    def note(t):
        ligne = f"{time.strftime('%H:%M:%S')} +{time.monotonic() - t0:6.2f}s {t}"
        print(ligne, flush=True)
        lignes.append(ligne)

    def log(m):
        t = re.sub(r"\x1b\[[0-9;]*m", "", m.message.decode("utf-8", "replace"))
        if "rainbird_me3" not in t:
            return
        if "TEST Manchester ← 00 02" in t:
            recu["m02"] = True
        if "]: ← " in t:
            recu["uart"] = True
        if recu.get("trace") and ("TRACE" in t or "fronts" in t):
            note(t.split("]: ", 1)[-1][:420])
            return
        if any(x in t for x in ("TEST Manchester → 02", "TEST Manchester ← (rien", "TRACE", "← 00 02 01", "fronts")):
            return
        if re.search(r"TEST|← |Octet hors trame|CRC|somme", t):
            note(t.split("]: ", 1)[-1][:300])

    c.subscribe_logs(log, log_level=LogLevel.LOG_LEVEL_DEBUG)
    await asyncio.sleep(0.5)
    if cmd == "preparer":
        c.switch_command(ecoute.key, True)
        note(">>> Écoute seule ALLUMÉE : la carte n'envoie plus rien d'elle-même. Coupe puis remets le programmateur.")
    elif cmd == "remettre":
        c.switch_command(ecoute.key, False)
        note(">>> Écoute seule ÉTEINTE : retour au fonctionnement normal (poignée de main UART, réveil si besoin).")
    else:
        for e in etapes:
            if e == "sonde":
                recu["uart"] = False
                note(">>> sonde UART (06 + défi)")
                await c.execute_service(svc["test_uart"], {"octets": "06"})
                await asyncio.sleep(1.5)
                note(f"    → l'UART {'RÉPOND' if recu['uart'] else 'ne répond pas'}")
                if recu["uart"]:
                    break
            elif e == "coupure":  # wait until the controller goes silent (power cut in progress)
                note(">>> en attente de la coupure du programmateur (10 min au plus)")
                muets = 0
                for _ in range(600):
                    recu["m02"] = False
                    await c.execute_service(svc["test_manchester"], {"octets": "02"})
                    await asyncio.sleep(1.0)
                    muets = 0 if recu["m02"] else muets + 1
                    if muets >= 5:  # 5 s of complete silence: a real power cut, not two lost frames
                        note("    → programmateur éteint")
                        break
                else:
                    note("    → pas de coupure vue : abandon")
                    break
            elif e == "attendre":  # wait for the controller to come back after the cut (it refuses 02 with 00 02 01)
                note(">>> attente du programmateur (Manchester 02 toutes les secondes, 3 min au plus)")
                recu["m02"] = False
                for _ in range(180):
                    await c.execute_service(svc["test_manchester"], {"octets": "02"})
                    await asyncio.sleep(1.0)
                    if recu["m02"]:
                        note("    → le programmateur répond : on enchaîne")
                        break
                else:
                    note("    → pas de réponse en 3 min : abandon")
                    break
            elif e == "poignee":
                note(">>> poignée de main Manchester")
                recu["trace"] = True
                await c.execute_service(svc["test_manchester_poignee"], {})
                await asyncio.sleep(2.5)
                recu["trace"] = False
            elif e.startswith("m:"):
                hexa = " ".join(re.findall("..", e[2:].replace(" ", "")))
                note(f">>> Manchester {hexa}")
                await c.execute_service(svc["test_manchester"], {"octets": hexa})
                await asyncio.sleep(1.2)
    await asyncio.sleep(0.5)
    await c.disconnect()
    with open(journal_fichier, "a", encoding="utf-8") as f:
        f.write("\n".join(lignes) + "\n")
    print(f"journal : {journal_fichier}")


asyncio.run(main(sys.argv[1], sys.argv[2:]))
