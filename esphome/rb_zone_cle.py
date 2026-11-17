# Shared settings for the test scripts: API key and board address, read from secrets.yaml (not published).
import re


def _secret(nom):
    for ligne in open("secrets.yaml", encoding="utf-8"):
        m = re.match(r'\s*' + nom + r'\s*:\s*"?([^"\s]+)"?', ligne)
        if m:
            return m.group(1)
    return None


def cle_api():
    cle = _secret("rainbird_api_key")
    if not cle:
        raise SystemExit("clé rainbird_api_key introuvable dans secrets.yaml")
    return cle


# Board address: « rainbird_host » in secrets.yaml, otherwise its mDNS name
HOTE = _secret("rainbird_host") or "rainbird-me3.local"
