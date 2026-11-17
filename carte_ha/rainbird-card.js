/*
 * rainbird-card — Lovelace card for the Rain Bird ESP-ME3 controller driven by the ESPHome prototype
 * (rainbird_me3 component). Layout inspired by the Zroshua card (RobinCK/zroshua, MIT license);
 * code written for this project. Reads the <domain>.<prefix>… entities and only calls standard actions
 * (switch, button, number).
 *
 *   type: custom:rainbird-card
 *   prefix: jardin_rain_bird_esp_me3_     # facultatif
 *   title: Arrosage                       # facultatif
 *   zones:                                # optional: zone names
 *     1: Pelouse nord
 *   plan: /local/rainbird-plan.json       # optional: isometric garden plan (JSON)
 */
const DUREES = [2, 5, 10, 15, 30];
const JOURS = ["D", "L", "M", "M", "J", "V", "S"]; // 0 = Sunday, as on the controller
const ORDRE_JOURS = [1, 2, 3, 4, 5, 6, 0];

const fmtDuree = (s) => {
  s = Math.max(0, Math.round(s));
  if (s >= 3600) return `${Math.floor(s / 3600)} h ${String(Math.floor((s % 3600) / 60)).padStart(2, "0")}`;
  if (s >= 60) return `${Math.floor(s / 60)} min${s % 60 && s < 600 ? ` ${String(s % 60).padStart(2, "0")} s` : ""}`;
  return `${s} s`;
};
const fmtHeure = (m) => `${String(Math.floor(m / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`;
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

function lireProgramme(txt) {
  // « s=30,735;f=0;d=1010101;i=3;r=1;p=115;z=3:1560,4:1560 »
  if (!txt || txt === "?" || txt === "unknown" || txt === "unavailable") return null;
  const v = Object.fromEntries(txt.split(";").map((x) => [x.split("=")[0], x.slice(x.indexOf("=") + 1)]));
  return {
    departs: v.s ? v.s.split(",").filter(Boolean).map(Number) : [],
    freq: Number(v.f || 0),
    jours: (v.d || "0000000").split("").map((c) => c === "1"),
    intervalle: Number(v.i || 0),
    restants: Number(v.r || 0),
    pct: Number(v.p || 100),
    zones: v.z ? v.z.split(",").filter(Boolean).map((e) => e.split(":").map(Number)) : [],
  };
}

function arroseLe(p, date, aujourdHui) {
  switch (p.freq) {
    case 0: return p.jours[date.getDay()];
    case 2: return date.getDate() % 2 === 1;
    case 3: return date.getDate() % 2 === 0;
    case 4: {
      const ecart = Math.round((date - aujourdHui) / 86400000);
      const iv = Math.max(1, p.intervalle);
      return ecart >= p.restants && (ecart - p.restants) % iv === 0;
    }
    default: return false;
  }
}

class RainbirdCard extends HTMLElement {
  setConfig(config) {
    this._cfg = { prefix: "jardin_rain_bird_esp_me3_", title: "Arrosage", zones: {}, ...config };
    if (typeof this._cfg.plan === "string" && this._cfg.plan !== this._planUrl) {
      this._planUrl = this._cfg.plan;
      this._plan = null;
      fetch(this._cfg.plan).then((r) => r.json()).then((j) => { this._plan = j; this._rendre(true); })
        .catch((e) => console.warn("rainbird-card : plan illisible", e));
    } else if (typeof this._cfg.plan === "object" && this._cfg.plan) {
      this._plan = this._cfg.plan;
    }
    this._sel = null;
    this._msg = "";
    if (!this.shadowRoot) {
      this.attachShadow({ mode: "open" });
      this.shadowRoot.addEventListener("click", (e) => this._clic(e));
    }
  }
  static getStubConfig() { return {}; }
  getCardSize() { return 12; }
  getGridOptions() { return { columns: 12, rows: "auto" }; }

  connectedCallback() {
    this._timer = setInterval(() => this._hass && this._enCours() && this._rendre(), 1000);
  }
  disconnectedCallback() { clearInterval(this._timer); }

  set hass(hass) { this._hass = hass; this._rendre(); }

  _e(dom, nom) { return this._hass.states[`${dom}.${this._cfg.prefix}${nom}`]; }
  _v(dom, nom) { const s = this._e(dom, nom); return s ? s.state : undefined; }
  _nomZone(z) { return this._cfg.zones[z] || `Zone ${z}`; }
  _zones() {
    const out = [];
    for (let z = 1; z <= 22; z++) if (this._e("switch", `zone_${z}`)) out.push(z);
    return out;
  }
  _enCours() { return this._v("binary_sensor", "arrosage_en_cours") === "on"; }

  _file() {
    const t = this._v("sensor", "file_d_arrosage");
    if (!t || t === "-" || t === "unknown" || t === "unavailable") return [];
    return t.split(";").map((e) => e.split(":").map(Number)).filter((e) => e[0] > 0);
  }

  _prochain(progs) {
    if (this._v("sensor", "etat_d_arrosage") === "0") return { texte: "Sélecteur sur OFF" };
    const report = Number(this._v("number", "report_d_arrosage_reglage") || this._v("sensor", "report_d_arrosage") || 0);
    const maintenant = new Date();
    const aujourdHui = new Date(maintenant.getFullYear(), maintenant.getMonth(), maintenant.getDate());
    let meilleur = null;
    progs.forEach((p, i) => {
      if (!p || !p.departs.length) return;
      for (let j = 0; j < 31 && !meilleur?.trouveAvant?.(j); j++) {
        const jour = new Date(aujourdHui.getTime() + j * 86400000);
        if (j < report || !arroseLe(p, jour, aujourdHui)) continue;
        for (const m of p.departs) {
          const quand = new Date(jour.getTime() + m * 60000);
          if (quand > maintenant && (!meilleur || quand < meilleur.quand)) meilleur = { quand, prog: "ABCD"[i] };
        }
      }
    });
    if (!meilleur) return { texte: "Aucun départ programmé" };
    const min = Math.round((meilleur.quand - maintenant) / 60000);
    const dans = min < 60 ? `${min} min` : min < 1440 ? `${Math.floor(min / 60)} h ${String(min % 60).padStart(2, "0")}` : `${Math.round(min / 1440)} j`;
    return { texte: `dans ${dans}`, detail: `${meilleur.prog} · ${meilleur.quand.toLocaleDateString("fr-FR", { weekday: "short" })} ${fmtHeure(meilleur.quand.getHours() * 60 + meilleur.quand.getMinutes())}` };
  }

  _appel(dom, svc, nom, data = {}) {
    return this._hass.callService(dom, svc, { entity_id: `${dom === "button" ? "button" : dom}.${this._cfg.prefix}${nom}`, ...data });
  }

  _clic(e) {
    const el = e.target.closest("[data-a]");
    if (!el || !this._hass) return;
    const a = el.dataset.a, arg = el.dataset.arg;
    const autorise = this._v("switch", "commandes_autorisees") === "on";
    const besoin = ["lancer", "prog", "suivante", "test"];
    if (besoin.includes(a) && !autorise) {
      this._msg = "Commandes verrouillées : appuie d'abord sur « Autoriser ».";
      this._rendre(true);
      return;
    }
    this._msg = "";
    switch (a) {
      case "autoriser": this._appel("switch", "turn_on", "commandes_autorisees"); break;
      case "verrouiller": this._appel("switch", "turn_off", "commandes_autorisees"); break;
      case "file": this._appel("switch", "toggle", "file_d_attente"); break;
      case "duree": this._hass.callService("number", "set_value", { entity_id: `number.${this._cfg.prefix}duree_manuelle`, value: Number(arg) }); break;
      case "zone": this._sel = this._sel === Number(arg) ? null : Number(arg); break;
      case "lancer": this._appel("switch", "turn_on", `zone_${arg}`); this._sel = null; break;
      case "arreter_zone": this._appel("switch", "turn_off", `zone_${arg}`); this._sel = null; break;
      case "stop": this._appel("button", "press", "arreter_l_arrosage"); break;
      case "suivante": this._appel("button", "press", "zone_suivante"); break;
      case "test": this._appel("button", "press", "tester_toutes_les_zones"); break;
      case "prog": this._appel("button", "press", `lancer_le_programme_${arg}`); break;
      case "editer": this._ouvrirEditeur(Number(arg)); break;
      case "ed_fermer": this._edit = null; break;
      case "ed_freq": this._edit.d.freq = Number(arg); break;
      case "ed_jour": this._edit.d.jours[Number(arg)] = !this._edit.d.jours[Number(arg)]; break;
      case "ed_iv": this._edit.d.intervalle = Math.min(30, Math.max(1, this._edit.d.intervalle + Number(arg))); break;
      case "ed_rest": this._edit.d.restants = Math.min(this._edit.d.intervalle, Math.max(0, this._edit.d.restants + Number(arg))); break;
      case "ed_pct": this._edit.d.pct = Math.min(200, Math.max(0, this._edit.d.pct + Number(arg))); break;
      case "ed_dur": {
        const [z, pas] = arg.split(":").map(Number);
        const v = (this._edit.d.durees[z] || 0) + pas;
        // a 1-minute step first rounds a duration in seconds (e.g. 250 s) to the nearest minute
        this._edit.d.durees[z] = Math.min(6 * 3600, Math.max(0, pas % 60 === 0 && v % 60 ? Math.round(v / 60) * 60 : v));
        break;
      }
      case "ed_dep_suppr": this._edit.d.departs.splice(Number(arg), 1); break;
      case "ed_dep_ajout": {
        const champ = this.shadowRoot.getElementById("ed-heure");
        const m = champ && /^(\d{1,2}):(\d{2})/.exec(champ.value || "");
        if (!m) { this._edit.err = "Choisis une heure dans le champ avant « Ajouter »."; break; }
        const min = Number(m[1]) * 60 + Number(m[2]);
        if (this._edit.d.departs.includes(min)) { this._edit.err = "Ce départ existe déjà."; break; }
        if (this._edit.d.departs.length >= 6) { this._edit.err = "6 départs au plus."; break; }
        this._edit.d.departs.push(min);
        this._edit.d.departs.sort((a, b) => a - b);
        this._edit.err = "";
        break;
      }
      case "ed_save": this._enregistrer(); break;
      case "infos": {
        const ev = new Event("hass-more-info", { bubbles: true, composed: true });
        ev.detail = { entityId: arg };
        this.dispatchEvent(ev);
        break;
      }
    }
    this._rendre(true);
  }

  _ouvrirEditeur(i) {
    if (this._edit && this._edit.p === i) { this._edit = null; return; }
    const p = lireProgramme(this._v("sensor", `programme_${"abcd"[i]}_donnees`));
    if (!p) { this._msg = "Configuration du programmateur pas encore lue."; return; }
    const durees = {};
    p.zones.forEach(([z, s]) => (durees[z] = s));
    const d = { freq: p.freq, intervalle: Math.max(1, p.intervalle), restants: p.restants, jours: [...p.jours],
      departs: [...p.departs].sort((a, b) => a - b), durees, pct: p.pct };
    this._edit = { p: i, d, origine: JSON.stringify(d), err: "" };
  }

  _enregistrer() {
    const E = this._edit;
    if (!E) return;
    if (this._v("switch", "commandes_autorisees") !== "on") {
      E.err = "Commandes verrouillées : appuie d'abord sur « Autoriser » en haut de la carte.";
      return;
    }
    const d = E.d;
    const durees = Object.entries(d.durees).filter(([, s]) => s > 0).map(([z, s]) => `${z}:${s}`).join(",");
    this._hass.callService("esphome", this._cfg.service_programme || "rainbird_me3_programme_ecrire", {
      programme: E.p, frequence: d.freq, intervalle: d.intervalle, restants: d.restants,
      jours: d.jours.map((j) => (j ? "1" : "0")).join(""), departs: d.departs.map(fmtHeure).join(","),
      durees, ajustement: d.pct,
    }).then(() => {
      this._msg = `Programme ${"ABCD"[E.p]} envoyé ; la carte affichera les valeurs relues dans quelques secondes.`;
      this._rendre(true);
    }).catch((err) => {
      this._msg = `Échec de l'envoi : ${err.message || err}`;
      this._rendre(true);
    });
    this._edit = null;
  }

  _htmlEditeur(zones) {
    const E = this._edit, d = E.d, l = "ABCD"[E.p];
    const pas = (action, arg, valeur) =>
      `<span class="pas"><button data-a="${action}" data-arg="-${arg}">−</button><b>${valeur}</b><button data-a="${action}" data-arg="${arg}">+</button></span>`;
    const modes = [[0, "Jours choisis"], [2, "Jours impairs"], [3, "Jours pairs"], [4, "Cyclique"]];
    let quand = "";
    if (d.freq === 0)
      quand = `<div class="ed-l"><span class="ed-lbl"></span><div class="jours">${ORDRE_JOURS.map((j) =>
        `<button class="jb ${d.jours[j] ? "oui" : ""}" data-a="ed_jour" data-arg="${j}">${JOURS[j]}</button>`).join("")}</div></div>`;
    if (d.freq === 4)
      quand = `<div class="ed-l"><span class="ed-lbl">Tous les</span>${pas("ed_iv", 1, `${d.intervalle} j`)}</div>
        <div class="ed-l"><span class="ed-lbl">Prochain dans</span>${pas("ed_rest", 1, `${d.restants} j`)}</div>`;
    const total = zones.reduce((s, z) => s + (d.durees[z] || 0), 0);
    const modifie = JSON.stringify(d) !== E.origine;
    return `<div class="editeur">
      <div class="ed-tete"><b>Modifier le programme ${l}</b><button class="x" data-a="ed_fermer" title="Fermer">✕</button></div>
      <div class="ed-l"><span class="ed-lbl">Fréquence</span><div class="seg">${modes.map(([m, t]) =>
        `<button class="chip ${d.freq === m ? "actif" : ""}" data-a="ed_freq" data-arg="${m}">${t}</button>`).join("")}</div></div>
      ${quand}
      <div class="ed-l"><span class="ed-lbl">Départs</span><div class="deps">
        ${d.departs.length ? d.departs.map((m, k) => `<span class="dep">${fmtHeure(m)}<button data-a="ed_dep_suppr" data-arg="${k}" title="Supprimer">✕</button></span>`).join("") : `<span class="gris">aucun : le programme ne partira pas seul</span>`}
        ${d.departs.length < 6 ? `<span class="ajout"><input id="ed-heure" type="time" step="60"><button class="chip" data-a="ed_dep_ajout">Ajouter</button></span>` : ""}
      </div></div>
      <div class="ed-l"><span class="ed-lbl">Ajustement</span>${pas("ed_pct", 5, `${d.pct} %`)}</div>
      <div class="ed-lbl">Durées par zone</div>
      <div class="ed-zones">${zones.map((z) => `<div class="ed-z ${d.durees[z] ? "" : "zero"}"><span>${esc(this._nomZone(z))}</span>
        ${pas("ed_dur", `${z}:60`, d.durees[z] ? fmtDuree(d.durees[z]) : "—").replace(/data-arg="-(\d+):60"/, 'data-arg="$1:-60"')}</div>`).join("")}</div>
      <div class="ed-total">Total ${fmtDuree(total)}${d.pct !== 100 ? ` · avec l'ajustement ${fmtDuree(total * d.pct / 100)}` : ""}</div>
      ${E.err ? `<div class="msg">${esc(E.err)}</div>` : ""}
      <div class="ed-pied"><button class="btn" data-a="ed_fermer">Annuler</button>
        <button class="btn vert" data-a="ed_save" ${modifie ? "" : "disabled"}>Enregistrer</button></div>
    </div>`;
  }

  _rendre(force) {
    if (!this._hass || !this.shadowRoot) return;
    // Do not rebuild the card while the user is typing (start time)
    if (!force && this.shadowRoot.activeElement && this.shadowRoot.activeElement.tagName === "INPUT") return;
    const h = this._html();
    if (typeof h === "string") {
      this._parts = null;
      this.shadowRoot.innerHTML = `<style>${STYLE}</style>${h}`;
      return;
    }
    // Three blocks updated separately: the plan is only redrawn when it changes (continuous animations)
    if (!this._parts) {
      this.shadowRoot.innerHTML = `<style>${STYLE}</style><ha-card><div class="carte"><div class="bloc" id="haut"></div><div id="plan"></div><div class="bloc" id="bas"></div></div></ha-card>`;
      this._parts = {};
    }
    for (const k of ["haut", "plan", "bas"]) {
      if (this._parts[k] === h[k]) continue;
      this._parts[k] = h[k];
      this.shadowRoot.getElementById(k).innerHTML = h[k];
    }
  }

  _htmlPlan(zones, actives, attente, sel) {
    const PLAN = this._plan;
    if (!PLAN) return "";
    const K = 0.58;
    const P = (x, y, h = 0) => [x, y * K - h];
    const S = (pts, h = 0) => pts.map(([x, y]) => P(x, y, h).map((v) => v.toFixed(1)).join(",")).join(" ");
    const COUL = ["#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4", "#00a6a6", "#f032e6", "#9a6324",
      "#2f4f8f", "#e0a800", "#800000", "#008080", "#5b5bd6", "#c2185b", "#558b2f"];
    const coul = (z) => COUL[(z - 1) % COUL.length];

    // Extruded volumes: visible walls (normal facing the viewer, to the south), then the top
    const volume = (pts, h, dessus, mur, h0 = 0) => {
      let aire = 0;
      pts.forEach(([x1, y1], i) => { const [x2, y2] = pts[(i + 1) % pts.length]; aire += x1 * y2 - x2 * y1; });
      const murs = [];
      pts.forEach(([x1, y1], i) => {
        const [x2, y2] = pts[(i + 1) % pts.length];
        let nx = y2 - y1, ny = -(x2 - x1);
        if (aire < 0) { nx = -nx; ny = -ny; }
        if (ny <= 0) return;
        const teinte = nx / Math.hypot(nx, ny) < 0 ? 0.68 : 0.86;
        murs.push([Math.max(y1, y2), `<polygon points="${S([[x1, y1], [x2, y2]], h0)} ${S([[x2, y2], [x1, y1]], h)}" fill="${mur}" style="filter:brightness(${teinte})"/>`]);
      });
      murs.sort((a, b) => a[0] - b[0]);
      return murs.map((m) => m[1]).join("") + `<polygon points="${S(pts, h)}" fill="${dessus}" stroke="rgba(0,0,0,.2)" stroke-width=".8"/>`;
    };
    const objets = []; // [depth, svg]

    // Hedges: four strips between the parcel outline and the lawn, gap for the entrance on the south-west side
    const O = PLAN.parcelle, I = PLAN.pelouse;
    const lerp = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t];
    const bandes = [[0, 1], [1, 2], [2, 3], [3, 0]].flatMap(([a, b]) => {
      if (a === 3) { // west → north side; the entrance is on the south-west side (2 → 3)
        return [[O[3], O[0], I[0], I[3]]];
      }
      if (a === 2) {
        const [t0, t1] = PLAN.entree.map((t) => 1 - t).sort((a, b) => a - b); // side walked from south to west
        return [[O[2], lerp(O[2], O[3], t0), lerp(I[2], I[3], t0), I[2]], [lerp(O[2], O[3], t1), O[3], I[3], lerp(I[2], I[3], t1)]];
      }
      return [[O[a], O[b], I[b], I[a]]];
    });
    bandes.forEach((b) => objets.push([Math.max(...b.map((p) => p[1])), volume(b, 13, "#3f7d3f", "#2c5a2c")]));
    PLAN.batiments.forEach((b, i) => {
      const poteaux = b.poteaux ? b.pts.map(([x, y]) => { const [px, py] = P(x, y); return `<rect x="${(px - 1.5).toFixed(1)}" y="${(py - b.h0).toFixed(1)}" width="3" height="${b.h0}" fill="#9aa3b2"/>`; }).join("") : "";
      objets.push([Math.max(...b.pts.map((p) => p[1])) + 1 + i * 0.01, poteaux + volume(b.pts, b.h, b.toit, b.mur || "#e8d2a6", b.h0 || 0)]);
    });
    // Trees: trunk only (the foliage is a shadow on the ground, see below)
    PLAN.arbres.forEach((a) => {
      const [x, y] = P(a.x, a.y), hh = a.type === "cypres" ? 14 : 18, w = a.type === "cypres" ? 3 : 4.5;
      objets.push([a.y, `<rect x="${(x - w / 2).toFixed(1)}" y="${(y - hh).toFixed(1)}" width="${w}" height="${hh}" rx="1.5" fill="#6b4a2f"/>
        <ellipse cx="${x.toFixed(1)}" cy="${(y - hh).toFixed(1)}" rx="${w / 2 + 2}" ry="${(w / 2 + 2) * K}" fill="#4d7a3a"/>`]);
    });
    objets.sort((a, b) => a[0] - b[0]);

    // Zones: colour patches around the nozzles, clipped to the lawn
    const parZone = {};
    PLAN.buses.forEach(([x, y, z]) => (parZone[z] = parZone[z] || []).push([x, y]));
    const nappes = Object.entries(parZone).map(([z, pts]) => {
      z = Number(z);
      const on = actives.includes(z), file = attente.some((e) => e[0] === z);
      const op = on ? 0.5 : sel === z ? 0.42 : file ? 0.3 : 0.07;
      return `<g class="nappe" data-a="zone" data-arg="${z}" opacity="${op}">${pts.map(([x, y]) =>
        `<circle cx="${x}" cy="${y}" r="${PLAN.r_nappe || 50}" fill="${coul(z)}"/>`).join("")}</g>`;
    }).join("");
    const jets = PLAN.buses.filter(([, , z]) => actives.includes(z)).map(([x, y], k) =>
      `<circle cx="${x}" cy="${y}" r="4" fill="none" stroke="#7fd4ff" stroke-width="3">
        <animate attributeName="r" values="4;${Math.round((PLAN.r_nappe || 50) * 0.85)}" dur="1.8s" begin="${(k * 0.37) % 1.8}s" repeatCount="indefinite"/>
        <animate attributeName="opacity" values=".9;0" dur="1.8s" begin="${(k * 0.37) % 1.8}s" repeatCount="indefinite"/></circle>`).join("");
    const buses = PLAN.buses.map(([x, y, z]) =>
      `<circle cx="${x}" cy="${y}" r="5" fill="#fff" stroke="${coul(z)}" stroke-width="2.5" data-a="zone" data-arg="${z}"/>`).join("");
    const ombres = PLAN.arbres.map((a) =>
      `<circle cx="${a.x}" cy="${a.y}" r="${a.r}" fill="rgba(20,40,10,.10)" stroke="rgba(20,40,10,.28)" stroke-width="1.5" stroke-dasharray="4 4"/>`).join("");

    // Numbered badges at the centre of each zone
    const pastilles = Object.entries(parZone).map(([z, pts]) => {
      z = Number(z);
      const cx = pts.reduce((s, p) => s + p[0], 0) / pts.length, cy = pts.reduce((s, p) => s + p[1], 0) / pts.length;
      const [x, y] = P(cx, cy, 10);
      const on = actives.includes(z), file = attente.some((e) => e[0] === z);
      return `<g class="pastille" data-a="zone" data-arg="${z}" transform="translate(${x.toFixed(1)},${y.toFixed(1)})">
        ${on ? `<circle r="15" fill="none" stroke="${coul(z)}" stroke-width="3"><animate attributeName="r" values="15;24" dur="1.4s" repeatCount="indefinite"/><animate attributeName="opacity" values="1;0" dur="1.4s" repeatCount="indefinite"/></circle>` : ""}
        <circle r="14" fill="${coul(z)}" stroke="${sel === z ? "#fff" : file ? "#e0a030" : "rgba(0,0,0,.25)"}" stroke-width="${sel === z || file ? 3 : 1}" ${file ? 'stroke-dasharray="4 3"' : ""}/>
        <text text-anchor="middle" dy="5" font-size="14" font-weight="700" fill="#fff">${z}</text></g>`;
    }).join("");

    const muret = `<polyline points="${S(PLAN.muret)}" fill="none" stroke="#9c927c" stroke-width="3" stroke-linecap="round"/>
      <polyline points="${S(PLAN.muret, 4)}" fill="none" stroke="#ddd3bd" stroke-width="2.5" stroke-linecap="round"/>`;
    const sol = `<g transform="scale(1,${K})">
        <polygon points="${O.map((p) => p.join(",")).join(" ")}" fill="#2c5a2c"/>
        <polygon points="${I.map((p) => p.join(",")).join(" ")}" fill="url(#gazon)"/>
        ${PLAN.sols.map((so) => `<polygon points="${so.pts.map((p) => p.join(",")).join(" ")}" fill="${so.c}" stroke="rgba(120,100,60,.35)" stroke-width="2"/>`).join("")}
        <g mask="url(#m-pelouse)">${ombres}${nappes}</g>
        <polygon points="${PLAN.piscine.map((p) => p.join(",")).join(" ")}" fill="#3fb1df" stroke="#efe8d6" stroke-width="7" stroke-linejoin="round"/>
        ${jets}${buses}
      </g>`;
    const nb = Object.keys(parZone).length, sans = zones.filter((z) => !parZone[z]);
    const xs = O.map((p) => p[0]), ys = O.map((p) => p[1] * K);
    const vb = [Math.min(...xs) - 10, Math.min(...ys) - 70, Math.max(...xs) - Math.min(...xs) + 20, Math.max(...ys) - Math.min(...ys) + 90].map((v) => v.toFixed(0)).join(" ");
    return `<div class="section">Plan du jardin <span class="gris">${PLAN.buses.length} buses · ${nb} zones${sans.length ? ` · sans buse sur le plan : ${sans.join(", ")}` : ""}</span></div>
      <div class="plan"><svg viewBox="${vb}" xmlns="http://www.w3.org/2000/svg">
        <defs>
          <linearGradient id="gazon" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#8cc46a"/><stop offset="1" stop-color="#6aa84f"/></linearGradient>
          <mask id="m-pelouse"><polygon points="${I.map((p) => p.join(",")).join(" ")}" fill="#fff"/>${[...PLAN.sols.map((so) => so.pts), PLAN.piscine, ...PLAN.batiments.map((b) => b.pts)].map((q) => `<polygon points="${q.map((p) => p.join(",")).join(" ")}" fill="#000"/>`).join("")}</mask>
        </defs>
                ${sol}${muret}${objets.map((o) => o[1]).join("")}${pastilles}
      </svg></div>`;
  }

  _html() {
    const C = this._cfg;
    if (!this._e("binary_sensor", "liaison_programmateur"))
      return `<ha-card><div class="vide">Entités <code>${esc(C.prefix)}…</code> introuvables.</div></ha-card>`;
    const zones = this._zones();
    const liaison = this._v("binary_sensor", "liaison_programmateur") === "on";
    const autorise = this._v("switch", "commandes_autorisees") === "on";
    const fileMode = this._v("switch", "file_d_attente") === "on";
    const auto = this._v("sensor", "etat_d_arrosage");
    const duree = Number(this._v("number", "duree_manuelle") || 10);
    const actives = zones.filter((z) => this._v("switch", `zone_${z}`) === "on");
    const file = this._file();
    const progs = [0, 1, 2, 3].map((i) => lireProgramme(this._v("sensor", `programme_${"abcd"[i]}_donnees`)));
    const prochain = this._prochain(progs);
    const pluie = this._v("binary_sensor", "sonde_de_pluie") === "on";
    const sondeActive = this._v("switch", "sonde_de_pluie_active") !== "off";
    const report = Number(this._v("sensor", "report_d_arrosage") || 0);

    // Running zone: remaining time interpolated since the sensor's last update
    const tr = this._e("sensor", "temps_restant");
    let enCours = null;
    if (actives.length) {
      const z = actives[0];
      const sw = this._e("switch", `zone_${z}`);
      let reste = tr ? Number(tr.state) - (Date.now() - new Date(tr.last_updated)) / 1000 : NaN;
      const ecoule = (Date.now() - new Date(sw.last_changed)) / 1000;
      if (!(reste >= 0)) reste = NaN;
      const total = isNaN(reste) ? NaN : ecoule + reste;
      enCours = { z, reste, ecoule, pct: isNaN(total) || total <= 0 ? 0 : Math.min(100, (100 * ecoule) / total) };
    }
    const attente = file.filter((e) => !enCours || e[0] !== enCours.z || file.indexOf(e) > 0);

    const tuile = (cls, icone, label, valeur, detail = "") => `
      <div class="stat ${cls}"><div class="ico"><ha-icon icon="${icone}"></ha-icon></div>
        <div><div class="lbl">${label}</div><div class="val">${valeur}</div>${detail ? `<div class="det">${detail}</div>` : ""}</div></div>`;

    let pluieTxt = "Sonde OK", pluieCls = "bleu";
    if (report > 0) { pluieTxt = `Report ${report} j`; pluieCls = "ambre"; }
    else if (pluie && sondeActive) { pluieTxt = "Pluie détectée"; pluieCls = "ambre"; }
    else if (!sondeActive) { pluieTxt = "Sonde contournée"; pluieCls = "gris"; }

    const entete = `
      <div class="entete">
        <div class="titre">${esc(C.title)}</div>
        <div class="puces">
          <span class="puce ${liaison ? "ok" : "ko"}" data-a="infos" data-arg="binary_sensor.${C.prefix}liaison_programmateur">${liaison ? "Connecté" : "Hors ligne"}</span>
          <span class="puce ${auto === "0" ? "ko" : "ok"}">${auto === "0" ? "OFF" : "AUTO"}</span>
        </div>
      </div>
      <div class="stats">
        ${tuile("vert", "mdi:water", "En cours", enCours ? esc(this._nomZone(enCours.z)) : "—", enCours && !isNaN(enCours.reste) ? `${fmtDuree(enCours.reste)} restantes` : "")}
        ${tuile("ambre", "mdi:format-list-bulleted", "En attente", attente.length || "0", attente.length ? fmtDuree(attente.reduce((s, e) => s + e[1], 0)) : "")}
        ${tuile("violet", "mdi:clock-outline", "Prochain", prochain.texte, prochain.detail || "")}
        ${tuile(pluieCls, "mdi:weather-pouring", "Pluie", pluieTxt)}
      </div>
      <div class="verrou ${autorise ? "ouvert" : ""}">
        <ha-icon icon="${autorise ? "mdi:shield-lock-open-outline" : "mdi:shield-lock-outline"}"></ha-icon>
        <span>${autorise ? "Commandes autorisées" : "Commandes verrouillées (lecture seule)"}</span>
        <button class="btn ${autorise ? "discret" : "vert"}" data-a="${autorise ? "verrouiller" : "autoriser"}">${autorise ? "Verrouiller" : "Autoriser"}</button>
      </div>
      ${this._msg ? `<div class="msg">${esc(this._msg)}</div>` : ""}`;

    let maintenant = `<div class="section">Maintenant</div>`;
    if (enCours) {
      maintenant += `
        <div class="ligne">
          <div class="ligne-txt"><div class="nom">${esc(this._nomZone(enCours.z))}</div>
            <div class="barre"><div style="width:${enCours.pct.toFixed(1)}%"></div></div></div>
          <div class="reste">${isNaN(enCours.reste) ? "…" : fmtDuree(enCours.reste)}</div>
          <button class="rond rouge" title="Arrêter" data-a="stop"><ha-icon icon="mdi:stop"></ha-icon></button>
        </div>`;
      if (attente.length)
        maintenant += `<div class="sous">En attente</div>` + attente.map((e) => `
          <div class="attente"><span>${esc(this._nomZone(e[0]))}</span><span class="gris">${fmtDuree(e[1])}</span></div>`).join("");
      maintenant += `<div class="actions"><button class="btn" data-a="suivante"><ha-icon icon="mdi:skip-next"></ha-icon>Zone suivante</button></div>`;
    } else {
      maintenant += `<div class="calme"><ha-icon icon="mdi:sprinkler-variant"></ha-icon>Aucun arrosage en cours</div>`;
    }

    const sel = this._sel;
    const grille = zones.map((z) => {
      const on = actives.includes(z);
      const enFile = attente.find((e) => e[0] === z);
      const etat = on ? (enCours && enCours.z === z && !isNaN(enCours.reste) ? fmtDuree(enCours.reste) : "en cours") : enFile ? "en attente" : "";
      return `<div class="zone ${on ? "on" : ""} ${enFile ? "file" : ""} ${sel === z ? "sel" : ""}" data-a="zone" data-arg="${z}">
        <div class="zico"><ha-icon icon="${on ? "mdi:sprinkler-variant" : "mdi:sprinkler"}"></ha-icon></div>
        <div class="znom">${esc(this._nomZone(z))}</div>${etat ? `<div class="zetat">● ${etat}</div>` : ""}</div>`;
    }).join("");
    let barreSel = "";
    if (sel) {
      const on = actives.includes(sel);
      const empile = fileMode && actives.length && !on;
      barreSel = `<div class="selbar"><span><b>${esc(this._nomZone(sel))}</b>${on ? " — en cours" : ` — ${duree} min`}</span>
        ${on ? `<button class="btn rouge" data-a="arreter_zone" data-arg="${sel}"><ha-icon icon="mdi:stop"></ha-icon>${fileMode ? "Passer" : "Arrêter"}</button>`
             : `<button class="btn vert" data-a="lancer" data-arg="${sel}"><ha-icon icon="mdi:play"></ha-icon>${empile ? "Ajouter à la file" : "Lancer"}</button>`}</div>`;
    }
    const zonesHtml = `
      <div class="section">Zones <span class="gris">${zones.length} · ${actives.length} en cours</span></div>
      <div class="durees"><span class="gris">Durée</span>${DUREES.map((d) => `<button class="chip ${d === duree ? "actif" : ""}" data-a="duree" data-arg="${d}">${d} min</button>`).join("")}
        <button class="chip ${fileMode ? "actif" : ""}" data-a="file" title="Pendant un arrosage, une zone lancée s'ajoute à la suite">File</button></div>
      <div class="grille">${grille}</div>${this._plan ? "" : barreSel}`;

    const progsHtml = `<div class="section">Programmes</div>` + progs.map((p, i) => {
      const l = "ABCD"[i];
      if (!p) return `<div class="prog inactif"><div class="lettre">${l}</div><div class="ptxt gris">…</div></div>`;
      const totalS = p.zones.reduce((s, e) => s + e[1], 0) * p.pct / 100;
      let quand;
      if (p.freq === 0) quand = ORDRE_JOURS.map((d) => `<span class="j ${p.jours[d] ? "oui" : ""}">${JOURS[d]}</span>`).join("");
      else if (p.freq === 2) quand = `<span class="gris">jours impairs</span>`;
      else if (p.freq === 3) quand = `<span class="gris">jours pairs</span>`;
      else if (p.freq === 4) quand = `<span class="gris">tous les ${p.intervalle} j (dans ${p.restants} j)</span>`;
      else quand = `<span class="gris">mode ${p.freq}</span>`;
      const actif = p.departs.length > 0;
      const ouvert = this._edit && this._edit.p === i;
      return `<div class="prog ${actif || ouvert ? "" : "inactif"} ${ouvert ? "ouvert" : ""}">
        <div class="lettre" data-a="editer" data-arg="${i}" title="Modifier">${l}</div>
        <div class="ptxt" data-a="editer" data-arg="${i}" title="Modifier">
          <div class="pl1">${actif ? p.departs.map(fmtHeure).join(" · ") : "Aucun départ"}<span class="gris"> · ${p.zones.length} zone${p.zones.length > 1 ? "s" : ""} · ${fmtDuree(totalS)}${p.pct !== 100 ? ` (${p.pct} %)` : ""}</span></div>
          <div class="pl2">${quand}</div>
        </div>
        <ha-icon class="crayon" icon="mdi:pencil-outline" data-a="editer" data-arg="${i}"></ha-icon>
        <button class="rond vert" title="Lancer le programme ${l}" data-a="prog" data-arg="${l.toLowerCase()}" ${p.zones.length ? "" : "disabled"}><ha-icon icon="mdi:play"></ha-icon></button>
      </div>${ouvert ? this._htmlEditeur(zones) : ""}`;
    }).join("");

    const pied = `<div class="pied">
        <button class="btn rouge plein" data-a="stop"><ha-icon icon="mdi:stop"></ha-icon>Tout arrêter</button>
        <button class="btn" data-a="test"><ha-icon icon="mdi:test-tube"></ha-icon>Tester toutes les zones</button></div>`;

    return { haut: entete + maintenant, plan: this._htmlPlan(zones, actives, attente, sel) + (this._plan ? barreSel : ""), bas: zonesHtml + progsHtml + pied };
  }
}

const STYLE = `
:host { --vert:#1db584; --rouge:#e5534b; --ambre:#e0a030; --violet:#8b7cf6; --bleu:#3fa7e0;
  --fond2: color-mix(in srgb, var(--primary-text-color) 5%, transparent);
  --bord: color-mix(in srgb, var(--primary-text-color) 10%, transparent); }
.carte { padding: 16px; display: flex; flex-direction: column; gap: 10px; }
.vide { padding: 16px; }
.entete { display: flex; align-items: center; justify-content: space-between; }
.titre { font-size: 1.5em; font-weight: 700; }
.puces { display: flex; gap: 6px; }
.puce { font-size: .75em; font-weight: 600; padding: 3px 10px; border-radius: 99px; cursor: pointer; }
.puce.ok { background: color-mix(in srgb, var(--vert) 18%, transparent); color: var(--vert); }
.puce.ko { background: color-mix(in srgb, var(--rouge) 18%, transparent); color: var(--rouge); }
.stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 8px; }
.stat { display: flex; gap: 10px; align-items: center; padding: 12px; border-radius: 14px; background: var(--fond2); }
.stat .ico { width: 38px; height: 38px; border-radius: 10px; display: grid; place-items: center; flex: none; }
.stat.vert .ico { background: color-mix(in srgb, var(--vert) 20%, transparent); color: var(--vert); }
.stat.ambre .ico { background: color-mix(in srgb, var(--ambre) 20%, transparent); color: var(--ambre); }
.stat.violet .ico { background: color-mix(in srgb, var(--violet) 20%, transparent); color: var(--violet); }
.stat.bleu .ico { background: color-mix(in srgb, var(--bleu) 20%, transparent); color: var(--bleu); }
.stat.gris .ico { background: var(--bord); color: var(--secondary-text-color); }
.lbl, .gris, .det { color: var(--secondary-text-color); }
.lbl { font-size: .8em; } .det { font-size: .75em; }
.val { font-size: 1.15em; font-weight: 700; line-height: 1.2; }
.verrou { display: flex; align-items: center; gap: 8px; padding: 8px 10px; border-radius: 12px;
  background: color-mix(in srgb, var(--ambre) 12%, transparent); color: var(--ambre); font-size: .9em; }
.verrou.ouvert { background: color-mix(in srgb, var(--vert) 10%, transparent); color: var(--vert); }
.verrou span { flex: 1; }
.msg { color: var(--rouge); font-size: .85em; }
.section { font-weight: 700; font-size: 1.05em; margin-top: 6px; display: flex; justify-content: space-between; }
.section .gris { font-weight: 500; font-size: .85em; }
.sous { font-size: .8em; color: var(--secondary-text-color); margin-top: 2px; }
.ligne { display: flex; align-items: center; gap: 12px; padding: 4px 0; }
.ligne-txt { flex: 1; min-width: 0; }
.nom { font-weight: 700; margin-bottom: 6px; }
.barre { height: 6px; border-radius: 3px; background: var(--bord); overflow: hidden; }
.barre div { height: 100%; background: var(--vert); border-radius: 3px; transition: width 1s linear; }
.reste { color: var(--secondary-text-color); white-space: nowrap; font-variant-numeric: tabular-nums; }
.attente { display: flex; justify-content: space-between; padding: 6px 10px; border-radius: 10px; background: var(--fond2); }
.calme { display: flex; align-items: center; gap: 8px; color: var(--secondary-text-color); padding: 4px 0; }
.actions, .pied { display: flex; gap: 8px; flex-wrap: wrap; }
.btn { display: inline-flex; align-items: center; gap: 6px; border: 1px solid var(--bord); background: var(--fond2);
  color: var(--primary-text-color); border-radius: 12px; padding: 8px 14px; font: inherit; font-weight: 600; cursor: pointer; }
.btn ha-icon { --mdc-icon-size: 18px; }
.btn.vert { background: var(--vert); border-color: var(--vert); color: #fff; }
.btn.rouge { color: var(--rouge); border-color: color-mix(in srgb, var(--rouge) 40%, transparent); }
.btn.rouge.plein { background: var(--rouge); color: #fff; border-color: var(--rouge); }
.btn.discret { background: transparent; color: inherit; border-color: currentColor; padding: 4px 10px; }
.rond { width: 40px; height: 40px; border-radius: 50%; border: none; display: grid; place-items: center; cursor: pointer; color: #fff; flex: none; }
.rond.rouge { background: var(--rouge); } .rond.vert { background: var(--vert); }
.rond[disabled] { opacity: .35; cursor: default; }
.durees { display: flex; gap: 6px; flex-wrap: wrap; align-items: center; font-size: .85em; }
.chip { border: 1px solid var(--bord); background: transparent; color: var(--primary-text-color); border-radius: 99px;
  padding: 4px 12px; font: inherit; cursor: pointer; }
.chip.actif { background: var(--primary-text-color); color: var(--card-background-color, #fff); border-color: transparent; font-weight: 600; }
.grille { display: grid; grid-template-columns: repeat(auto-fill, minmax(118px, 1fr)); gap: 8px; }
.zone { display: flex; flex-direction: column; gap: 4px; padding: 10px; border-radius: 14px; background: var(--fond2);
  border: 1px solid transparent; cursor: pointer; user-select: none; }
.zone .zico { width: 32px; height: 32px; border-radius: 9px; display: grid; place-items: center; background: var(--bord); color: var(--secondary-text-color); }
.zone .znom { font-weight: 600; }
.zone .zetat { font-size: .78em; color: var(--secondary-text-color); }
.zone.on { border-color: var(--vert); background: color-mix(in srgb, var(--vert) 10%, transparent); }
.zone.on .zico { background: color-mix(in srgb, var(--vert) 25%, transparent); color: var(--vert); }
.zone.on .zetat { color: var(--vert); }
.zone.file { border-color: color-mix(in srgb, var(--ambre) 60%, transparent); }
.zone.file .zetat { color: var(--ambre); }
.zone.sel { outline: 2px solid var(--primary-color); }
.selbar { display: flex; align-items: center; justify-content: space-between; gap: 8px; padding: 8px 10px; border-radius: 12px; background: var(--fond2); }
.prog { display: flex; align-items: center; gap: 12px; padding: 10px; border-radius: 14px; background: var(--fond2); }
.prog.inactif { opacity: .6; }
.lettre { width: 36px; height: 36px; border-radius: 10px; display: grid; place-items: center; font-weight: 800;
  background: color-mix(in srgb, var(--bleu) 18%, transparent); color: var(--bleu); flex: none; }
.ptxt { flex: 1; min-width: 0; }
.pl1 { font-weight: 600; }
.pl2 { margin-top: 4px; display: flex; gap: 3px; font-size: .8em; }
.j { width: 20px; height: 20px; border-radius: 6px; display: grid; place-items: center; background: var(--bord); color: var(--secondary-text-color); }
.j.oui { background: color-mix(in srgb, var(--vert) 22%, transparent); color: var(--vert); font-weight: 700; }
.bloc { display: flex; flex-direction: column; gap: 10px; }
.plan { border-radius: 16px; overflow: hidden; background: radial-gradient(ellipse at 50% 40%, color-mix(in srgb, #8cc46a 14%, transparent), transparent 70%); }
.plan svg { display: block; width: 100%; height: auto; }
.plan .nappe, .plan .pastille, .plan circle[data-a] { cursor: pointer; }
.plan .pastille:hover circle { filter: brightness(1.15); }
.lettre, .ptxt, .crayon { cursor: pointer; }
.crayon { color: var(--secondary-text-color); --mdc-icon-size: 20px; }
.prog.ouvert { outline: 2px solid var(--primary-color); }
.editeur { display: flex; flex-direction: column; gap: 10px; padding: 14px; border-radius: 14px; margin-top: -4px;
  border: 1px solid var(--bord); background: color-mix(in srgb, var(--primary-color) 4%, transparent); }
.ed-tete { display: flex; justify-content: space-between; align-items: center; }
.x { border: none; background: none; color: var(--secondary-text-color); font-size: 1.1em; cursor: pointer; }
.ed-l { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
.ed-lbl { min-width: 90px; color: var(--secondary-text-color); font-size: .85em; }
.seg, .jours, .deps { display: flex; gap: 6px; flex-wrap: wrap; align-items: center; }
.jb { width: 34px; height: 34px; border-radius: 10px; border: 1px solid var(--bord); background: transparent;
  color: var(--secondary-text-color); font: inherit; font-weight: 700; cursor: pointer; }
.jb.oui { background: color-mix(in srgb, var(--vert) 22%, transparent); color: var(--vert); border-color: var(--vert); }
.dep { display: inline-flex; align-items: center; gap: 4px; padding: 4px 6px 4px 12px; border-radius: 99px; font-weight: 600;
  background: color-mix(in srgb, var(--bleu) 16%, transparent); color: var(--bleu); }
.dep button { border: none; background: none; color: inherit; cursor: pointer; font-size: .85em; }
.ajout { display: inline-flex; gap: 6px; align-items: center; }
.ajout input { font: inherit; padding: 4px 8px; border-radius: 10px; border: 1px solid var(--bord);
  background: var(--card-background-color); color: var(--primary-text-color); }
.pas { display: inline-flex; align-items: center; gap: 4px; }
.pas button { width: 30px; height: 30px; border-radius: 9px; border: 1px solid var(--bord); background: var(--fond2);
  color: var(--primary-text-color); font-size: 1.1em; cursor: pointer; }
.pas b { min-width: 64px; text-align: center; font-variant-numeric: tabular-nums; }
.ed-zones { display: grid; grid-template-columns: repeat(auto-fill, minmax(230px, 1fr)); gap: 6px; }
.ed-z { display: flex; justify-content: space-between; align-items: center; padding: 4px 4px 4px 10px; border-radius: 10px; background: var(--fond2); }
.ed-z.zero span:first-child { color: var(--secondary-text-color); }
.ed-total { font-size: .9em; color: var(--secondary-text-color); }
.ed-pied { display: flex; justify-content: flex-end; gap: 8px; }
.btn[disabled] { opacity: .4; cursor: default; }
`;

customElements.define("rainbird-card", RainbirdCard);
window.customCards = window.customCards || [];
window.customCards.push({ type: "rainbird-card", name: "Rain Bird ESP-ME3", description: "Arrosage : zones, file, programmes" });
