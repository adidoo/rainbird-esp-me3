# Builds the sections of the Overview « Arrosage » view (view 8) from a dashboard backup.
# Output: JSON of the section list (to be set at /views/8/sections).
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))["data"]["config"]
v = d["views"][8]
[pic] = [c for s in v["sections"] for c in s["cards"] if c.get("type") == "picture-elements"]
drip = [c for s in v["sections"] for c in s["cards"] if "drip_garden" in json.dumps(c)]
P = "jardin_rain_bird_esp_me3_"
for e in pic["elements"]:
    e["target"] = {"entity_id": f"switch.{P}zone_{e['title']}"}
pic["grid_options"] = {"columns": "full", "rows": "auto"}
row = lambda dom, name, label: {"entity": f"{dom}.{P}{name}", "name": label}
tile = lambda dom, name, label, cols, **k: {"type": "tile", "entity": f"{dom}.{P}{name}", "name": label, "grid_options": {"columns": cols}, **k}
press = lambda name, label, icon, cols: tile("button", name, label, cols, icon=icon, hide_state=True, tap_action={
    "action": "perform-action", "perform_action": "button.press", "target": {"entity_id": f"button.{P}{name}"}})
jours = ["Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"]
md = "\n\n".join(f"**{l.upper()}** : {{{{ states('sensor.{P}programme_{l}') }}}}" for l in "abcd")
sections = [
    {"type": "grid", "cards": [
        {"type": "heading", "heading": "Zones", "icon": "mdi:sprinkler-variant", "badges": [
            {"type": "entity", "entity": f"binary_sensor.{P}arrosage_en_cours", "show_state": True},
            {"type": "entity", "entity": f"sensor.{P}zones_actives", "show_state": True}]},
        tile("switch", "commandes_autorisees", "Commandes autorisées", 6, tap_action={"action": "toggle"}),
        tile("switch", "file_d_attente", "File d'attente", 6, tap_action={"action": "toggle"}),
        tile("number", "duree_manuelle", "Durée manuelle", 12, features=[{"type": "numeric-input", "style": "buttons"}], features_position="inline"),
        press("arreter_l_arrosage", "Arrêter", "mdi:water-off", 6),
        press("zone_suivante", "Zone suivante", "mdi:skip-next", 6),
        {"type": "entities", "entities": [row("switch", f"zone_{z}", f"Zone {z}") for z in range(1, 14)]}]},
    {"type": "grid", "column_span": 2, "cards": [
        {"type": "heading", "heading": "Plan du jardin", "icon": "mdi:map-outline"}, pic]},
    {"type": "grid", "cards": [
        {"type": "heading", "heading": "Programmes", "icon": "mdi:calendar-clock"},
        {"type": "markdown", "content": md},
        *[press(f"lancer_le_programme_{l}", f"Lancer {l.upper()}", f"mdi:alpha-{l}-circle-outline", 3) for l in "abcd"],
        press("tester_toutes_les_zones", "Tester toutes les zones", "mdi:test-tube", 6),
        tile("number", "duree_du_test", "Durée du test", 6, features=[{"type": "numeric-input", "style": "buttons"}]),
        {"type": "heading", "heading": "Réglages", "heading_style": "subtitle", "icon": "mdi:cog-outline"},
        {"type": "entities", "entities": [
            row("binary_sensor", "sonde_de_pluie", "Pluie détectée"),
            row("switch", "sonde_de_pluie_active", "Sonde de pluie active"),
            row("number", "report_d_arrosage_reglage", "Report d'arrosage"),
            row("sensor", "etat_d_arrosage", "Sélecteur (1 = AUTO, 0 = OFF)"),
            row("sensor", "heure_du_programmateur", "Heure du programmateur"),
            row("sensor", "date_du_programmateur", "Date du programmateur"),
            row("button", "mettre_a_l_heure", "Mettre à l'heure"),
            {"type": "divider"},
            row("select", "zone_editee", "Cycle + ressuyage : zone"),
            row("number", "cycle", "Cycle"),
            row("number", "ressuyage", "Ressuyage"),
            {"type": "divider"},
            row("binary_sensor", "liaison_programmateur", "Liaison programmateur"),
            row("binary_sensor", "configuration_lue", "Configuration lue"),
            row("sensor", "modele", "Modèle"),
            row("sensor", "signal_wifi", "Signal WiFi"),
            row("button", "relire_la_configuration", "Relire la configuration")]}]},
    {"type": "grid", "cards": [
        {"type": "heading", "heading": "Éditeur de programme", "icon": "mdi:playlist-edit"},
        {"type": "entities", "entities": [
            row("select", "programme_edite", "Programme édité"), row("select", "frequence", "Fréquence"),
            row("number", "ajustement_saisonnier", "Ajustement saisonnier"),
            row("number", "intervalle_cyclique", "Intervalle (cyclique)"),
            row("number", "jours_restants", "Jours restants (cyclique)")]},
        {"type": "heading", "heading": "Jours d'arrosage", "heading_style": "subtitle"},
        {"type": "entities", "entities": [row("switch", j.lower(), j) for j in jours]},
        {"type": "heading", "heading": "Heures de départ (HH:MM, vide = aucun)", "heading_style": "subtitle"},
        {"type": "entities", "entities": [row("text", f"depart_{i}", f"Départ {i}") for i in range(1, 7)]}]},
    {"type": "grid", "cards": [
        {"type": "heading", "heading": "Durées du programme édité", "icon": "mdi:timer-sand"},
        {"type": "entities", "entities": [row("number", f"duree_zone_{z}", f"Zone {z}") for z in range(1, 14)]}]},
    {"type": "grid", "cards": [{"type": "heading", "heading": "Goutte-à-goutte", "icon": "mdi:water-outline"}, *drip]},
]
sys.stdout.reconfigure(encoding="utf-8")
print(json.dumps(sections, ensure_ascii=False, separators=(",", ":")))
