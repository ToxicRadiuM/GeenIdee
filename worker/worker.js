// Dashboardmaatje: eigen tussenstation voor flitsers.nl, de wegenkaart (PDOK), live verkeersinfo (NDW) en gedeelde flitsers.
// /flitsers geeft de gemelde controles van flitsers.nl en de ANWB als JSON, met latitude en longitude erbij.
// /verkeer geeft de actuele files en werkzaamheden van de ANWB als JSON (de app vult aan met NDW).
// /zoek en /route zijn voor de navigatie: adres opzoeken en een route met afslagen.
// Alleen voor persoonlijk gebruik. Vul hieronder het adres van je eigen GitHub Pages-site in.
const TOEGESTAAN = 'https://toxicradium.github.io';   // jouw GitHub Pages-adres (al ingevuld)

export default {
  async fetch(request, env, ctx) {
    const origin = request.headers.get('Origin') || '';
    const cors = {
      'Access-Control-Allow-Origin': origin === TOEGESTAAN ? origin : TOEGESTAAN,
      'Vary': 'Origin',
    };
    if (request.method === 'OPTIONS') return new Response(null, { headers: { ...cors, 'Access-Control-Allow-Methods': 'GET, POST', 'Access-Control-Allow-Headers': 'Content-Type' } });
    if (origin && origin !== TOEGESTAAN) return new Response('Niet toegestaan', { status: 403, headers: cors });

    const url = new URL(request.url);
    if (request.method !== 'GET' && url.pathname !== '/backup') return new Response('Alleen GET', { status: 405, headers: cors });
    if (url.pathname.startsWith('/cams')) return cams(url, env, cors);
    if (url.pathname === '/flitsers') return flitsers(env, ctx, cors);
    if (url.pathname === '/verkeer') return verkeer(ctx, cors);
    if (url.pathname === '/backup') return backup(request, url, env, cors);
    if (url.pathname === '/osm') return osm(url, env, ctx, cors);
    let target, ttl;
    if (url.pathname === '/rss') { target = 'https://flitsers.nl/flitsers.rss'; ttl = 300; }
    else if (url.pathname === '/ndw') { target = 'https://opendata.ndw.nu/actueel_beeld.xml.gz'; ttl = 60; }
    else if (url.pathname === '/brug') { target = 'https://opendata.ndw.nu/planningsfeed_brugopeningen.xml.gz'; ttl = 120; }
    else if (url.pathname === '/msi') { target = 'https://opendata.ndw.nu/Matrixsignaalinformatie.xml.gz'; ttl = 60; }
    else if (url.pathname.startsWith('/pdok/rws/nationaal-wegenbestand-wegen/')) { target = 'https://api.pdok.nl' + url.pathname.slice(5) + url.search; ttl = 604800; }
    // Navigatie: adres zoeken (PDOK Locatieserver) en route met afslagen (OSRM)
    else if (url.pathname === '/zoek') { target = 'https://api.pdok.nl/bzk/locatieserver/search/v3_1/free' + url.search; ttl = 86400; }
    else if (/^\/route\/[-\d.]+,[-\d.]+;[-\d.]+,[-\d.]+$/.test(url.pathname)) { target = 'https://router.project-osrm.org/route/v1/driving' + url.pathname.slice(6) + url.search; ttl = 60; }
    else return new Response('Onbekend adres', { status: 404, headers: cors });

    const cache = caches.default;
    const key = new Request(target);
    let res = await cache.match(key);
    if (!res) {
      const up = await fetch(target, { headers: { 'User-Agent': 'Buddy persoonlijk gebruik' } });
      res = new Response(up.body, up);
      res.headers.set('Cache-Control', 'public, max-age=' + ttl);
      if (up.ok) ctx.waitUntil(cache.put(key, res.clone()));
    }
    const out = new Response(res.body, res);
    for (const k in cors) out.headers.set(k, cors[k]);
    return out;
  },
};

// Gedeelde flitsers voor een groepje (bijvoorbeeld jij en collega's). Heeft een KV-opslag nodig met de naam FLITS.
const MOBIEL_UREN = 3, MAX_PER_GROEP = 800;
async function cams(url, env, cors) {
  const json = (obj, status = 200) => new Response(JSON.stringify(obj), { status, headers: { ...cors, 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });
  if (!env.FLITS) return json({ error: 'KV-opslag FLITS is nog niet gekoppeld aan de Worker.' }, 501);
  const g = (url.searchParams.get('g') || '').toLowerCase();
  if (!/^[a-z0-9]{6,32}$/.test(g)) return json({ error: 'Groepscode moet 6 tot 32 letters of cijfers zijn.' }, 400);
  const p0 = url.pathname;
  if (p0 === '/cams/ping' || p0 === '/cams/pings' || p0 === '/cams/lid' || p0 === '/cams/bord' || p0 === '/cams/hier' || p0 === '/cams/weg') return groep(url, env, g, json);
  const key = 'g:' + g, nu = Date.now();
  let lijst = [];
  try { lijst = (await env.FLITS.get(key, 'json')) || []; } catch (e) { lijst = []; }
  lijst = lijst.filter((c) => c.k !== 'mobile' || nu - c.t < MOBIEL_UREN * 3600e3);
  const schoon = (v) => (v || '').replace(/[^A-Za-z\u00C0-\u017F '\-]/g, '').slice(0, 30);
  const p = url.pathname;
  if (p === '/cams') return json({ cams: lijst });
  if (p === '/cams/add') {
    const lat = parseFloat(url.searchParams.get('lat')), lon = parseFloat(url.searchParams.get('lon'));
    const kind = url.searchParams.get('k') === 'mobile' ? 'mobile' : 'fixed';
    let lim = parseInt(url.searchParams.get('lim'), 10); if (!(lim >= 5 && lim <= 130)) lim = null;
    const id = (url.searchParams.get('id') || '').replace(/[^a-zA-Z0-9]/g, '').slice(0, 24);
    if (!(lat > 50.6 && lat < 53.7 && lon > 3.0 && lon < 7.4) || !id) return json({ error: 'Ongeldige plek.' }, 400);
    const dicht = lijst.find((c) => c.k === kind && Math.hypot((c.lat - lat) * 111195, (c.lon - lon) * 111195 * Math.cos(lat * Math.PI / 180)) < 40);
    if (dicht) { dicht.t = nu; if (lim) dicht.lim = lim; }
    else { lijst = lijst.filter((c) => c.id !== id); lijst.push({ id, lat: +lat.toFixed(6), lon: +lon.toFixed(6), lim, k: kind, t: nu, n: schoon(url.searchParams.get('n')), p: schoon(url.searchParams.get('p')) }); }
    if (lijst.length > MAX_PER_GROEP) lijst = lijst.slice(-MAX_PER_GROEP);
    await env.FLITS.put(key, JSON.stringify(lijst));
    return json({ ok: true, n: lijst.length });
  }
  if (p === '/cams/del') {
    const id = url.searchParams.get('id') || '';
    lijst = lijst.filter((c) => c.id !== id);
    await env.FLITS.put(key, JSON.stringify(lijst));
    return json({ ok: true, n: lijst.length });
  }
  return json({ error: 'Onbekend adres' }, 404);
}

// ---------------------------------------------------------------------------
// /flitsers: gemelde controles van flitsers.nl als JSON met coördinaten.
//   { success: true, flitsers: [ { id, weg, hm, richting, omschrijving, latitude, longitude, tijd } ] }
//
// De feed geeft alleen wegnummer + hectometerpaal. Per weg bouwt de Worker één keer een tabel
// "hectometerpaal -> plek" uit de PDOK-wegenkaart en bewaart die in de KV-opslag FLITS.
// Dat bouwen gaat in kleine stukjes (één pagina per aanroep, op de achtergrond na het antwoord),
// zodat het binnen de rekentijd van het gratis Cloudflare-plan blijft. Daarna is opzoeken vrijwel gratis.
// Zonder KV werkt het ook, maar dan wordt de tabel na een tijdje opnieuw opgebouwd.
// ---------------------------------------------------------------------------
const FL_MAX_UUR = 4;          // meldingen ouder dan 4 uur weglaten
const FL_MAX_ITEMS = 60;       // nooit meer dan 60 meldingen verwerken
const FL_PAGINA = 100;         // wegvakken per PDOK-pagina (250 gaf time-outs bij PDOK)
const FL_PAGINAS_PER_KEER = 1; // PDOK-pagina's per aanroep. Betaald Cloudflare-plan? Zet gerust op 10
const FL_VERNIEUW_DAGEN = 90;  // wegtabel na 90 dagen opnieuw opbouwen
// ANWB-flitsers. Let op: dit is geen officiële API maar het adres dat de ANWB-website zelf gebruikt.
// De ANWB staat hergebruik zonder schriftelijke toestemming niet toe; alleen voor persoonlijk gebruik.
// Werkt het niet meer of wil je het niet? Zet FL_ANWB_AAN op false.
const FL_ANWB_AAN = true;
const FL_ANWB = 'https://api.anwb.nl/traffic/traffic-info/v1/incidents/incidents-desktop';
const FL_PDOK = 'https://api.pdok.nl/rws/nationaal-wegenbestand-wegen/ogc/v1/collections/wegvakken/items';
const FL_KV = 'weg:v2:';       // naam in de KV-opslag (v2: andere paginagrootte dan v1)
const flWegen = new Map();     // snelle kopie van de wegtabellen (blijft even bewaard tussen aanroepen)
let flFout = { t: 0, tekst: '' }; // laatste PDOK-fout, om te laten zien in het antwoord

async function flitsers(env, ctx, cors) {
  const json = (obj, status = 200, cc = 'no-store') =>
    new Response(JSON.stringify(obj), { status, headers: { ...cors, 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': cc } });
  const cache = caches.default;
  const sleutel = new Request('https://dashboardmaatje.intern/flitsers-v2');
  try {
    const bewaard = await cache.match(sleutel);
    if (bewaard) return json(await bewaard.json(), 200, 'public, max-age=60');

    // 1. Bronnen ophalen. Elke bron mag los mislukken; de andere werkt dan gewoon door.
    const bronnen = {};
    let meldingen = [];
    try { meldingen = leesFeed(await haalRss(ctx)); bronnen.flitsersNl = 'ok'; }
    catch (e) { bronnen.flitsersNl = 'mislukt: ' + String((e && e.message) || e).slice(0, 100); }
    let anwb = [];
    if (FL_ANWB_AAN) {
      try { anwb = leesAnwb(await haalAnwb(ctx)); bronnen.anwb = 'ok'; }
      catch (e) { bronnen.anwb = 'mislukt: ' + String((e && e.message) || e).slice(0, 100); }
    }
    if (!bronnen.flitsersNl.startsWith('ok') && !(bronnen.anwb || '').startsWith('ok')) throw new Error('geen enkele bron bereikbaar (' + JSON.stringify(bronnen) + ')');
    const nu = Date.now();
    const recent = meldingen.filter((m) => !m.pub || nu - m.pub < FL_MAX_UUR * 3600e3).slice(0, FL_MAX_ITEMS);

    // 2. Wegtabellen laden en, waar nodig, een stukje verder opbouwen
    const wegNamen = [...new Set(recent.filter((m) => m.weg).map((m) => m.weg))];
    const tabellen = {};
    for (const w of wegNamen) tabellen[w] = await laadWeg(env, w);
    // Wegen met de meeste meldingen eerst
    const telling = (w) => recent.filter((m) => m.weg === w).length;
    const teBouwen = wegNamen.filter((w) => !tabellen[w].klaar || nu - tabellen[w].t > FL_VERNIEUW_DAGEN * 864e5)
      .sort((a, b) => telling(b) - telling(a));
    // Het opbouwen gebeurt NA het antwoord (op de achtergrond), zodat een trage PDOK
    // de app nooit laat wachten. De volgende aanroep ziet het resultaat.
    if (teBouwen.length) ctx.waitUntil(bouwOpAchtergrond(env, teBouwen.map((w) => tabellen[w])));
    const pdokFout = nu - flFout.t < 10 * 60e3 ? flFout.tekst : '';

    // 3. Uitvoer maken
    const uit = [];
    let zonderPlek = 0;
    // ANWB eerst: die geeft de exacte plek op de juiste rijbaan.
    for (const a of anwb) uit.push(a);
    for (const m of recent) {
      // Zelfde flitser al bij de ANWB (zelfde weg, hectometerpaal hooguit 300 m verschil)? Dan overslaan.
      if (m.weg && anwb.some((a) => a.weg === m.weg && a.hm != null && Math.abs(a.hm - m.hm) <= 0.3)) continue;
      const tab = m.weg ? tabellen[m.weg] : null;
      const pts = tab ? tab.hm[m.hm.toFixed(1)] : null;
      if (!pts || !pts.length) { zonderPlek++; continue; }
      pts.forEach((pt, n) => {
        uit.push({
          id: m.id + (n ? '_' + n : ''),
          weg: m.weg,
          hm: m.hm,
          richting: m.richting,
          omschrijving: m.omschrijving,
          latitude: pt[0],
          longitude: pt[1],
          tijd: m.pub ? new Date(m.pub).toISOString() : null,
          bron: 'flitsers.nl',
        });
      });
    }
    const nogBezig = wegNamen.filter((w) => !tabellen[w].klaar);
    const resultaat = { success: true, flitsers: uit, gemeld: recent.length + anwb.length, zonderPlek, wegenNogBezig: nogBezig, bronnen, bijgewerkt: new Date(nu).toISOString() };
    if (pdokFout) resultaat.waarschuwing = 'PDOK even niet bereikbaar: ' + pdokFout;
    // Kort bewaren, maar korter als er nog wegen worden opgebouwd
    const bronFout = Object.values(bronnen).some((v) => v !== 'ok');
    const ttl = nogBezig.length || pdokFout || bronFout ? 20 : 60;
    ctx.waitUntil(cache.put(sleutel, new Response(JSON.stringify(resultaat), { headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=' + ttl } })));
    return json(resultaat, 200, 'public, max-age=' + ttl);
  } catch (e) {
    return json({ success: false, error: 'Flitsers ophalen mislukt: ' + String((e && e.message) || e).slice(0, 200) }, 502);
  }
}

async function bouwOpAchtergrond(env, lijst) {
  let paginas = FL_PAGINAS_PER_KEER;
  for (const tab of lijst) {
    if (paginas <= 0) break;
    if (tab.bezig) continue; // wordt al opgebouwd door een andere aanroep
    tab.bezig = true;
    try { paginas -= await bouwVerder(tab, paginas); flFout = { t: 0, tekst: '' }; }
    catch (e) { flFout = { t: Date.now(), tekst: String((e && e.message) || e).slice(0, 120) }; paginas = 0; }
    finally { tab.bezig = false; }
    if (env.FLITS) { try { await env.FLITS.put(FL_KV + tab.weg, JSON.stringify(tab)); } catch (e) {} }
  }
}

// ---------- ANWB ----------
async function haalAnwb(ctx) {
  const cache = caches.default;
  const key = new Request(FL_ANWB);
  let res = await cache.match(key);
  if (!res) {
    const ctl = new AbortController();
    const tm = setTimeout(() => ctl.abort(), 10000);
    let up;
    try { up = await fetch(FL_ANWB, { headers: { 'User-Agent': 'Buddy persoonlijk gebruik', Accept: 'application/json' }, signal: ctl.signal }); }
    finally { clearTimeout(tm); }
    if (!up.ok) throw new Error('ANWB gaf status ' + up.status);
    const tekst = await up.text();
    res = new Response(tekst, { headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=120' } });
    ctx.waitUntil(cache.put(key, res.clone()));
  }
  return await res.json();
}

// ---------- /verkeer: files en werkzaamheden van de ANWB ----------
// Antwoord: { success, bijgewerkt, meldingen: [ { id, k, weg, van, naar, richting, tekst, vertraging, lengte, start, eind, pts } ] }
//   k: 'jam' (file), 'accident' (ongeluk), 'closure' (weg dicht), 'ramp' (op-/afrit dicht), 'lane' (werk/rijstrook dicht)
//   vertraging in seconden, lengte in meters, pts = [[lat, lon], ...]
async function verkeer(ctx, cors) {
  const json = (obj, status = 200) => new Response(JSON.stringify(obj), { status, headers: { ...cors, 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': status === 200 ? 'public, max-age=60' : 'no-store' } });
  if (!FL_ANWB_AAN) return json({ success: false, error: 'ANWB-koppeling staat uit (FL_ANWB_AAN).' }, 503);
  try {
    return json(leesAnwbVerkeer(await haalAnwb(ctx), Date.now()));
  } catch (e) {
    return json({ success: false, error: 'ANWB-verkeer ophalen mislukt: ' + String((e && e.message) || e).slice(0, 200) }, 502);
  }
}

function leesAnwbVerkeer(j, nu) {
  if (!j || j.success !== true || !Array.isArray(j.roads)) throw new Error('onverwacht antwoord van de ANWB');
  const tekst = (v, max) => (typeof v === 'string' ? v.replace(/\s+/g, ' ').trim().slice(0, max) : '');
  const tijd = (v) => { const t = typeof v === 'string' ? Date.parse(v) : NaN; return isFinite(t) ? t : null; };
  const goedePlek = (p) => p && isFinite(+p.lat) && isFinite(+p.lon) && +p.lat > 50.6 && +p.lat < 53.7 && +p.lon > 3.0 && +p.lon < 7.4;
  const uit = [], gezien = new Set();
  for (const r of j.roads) {
    for (const s of Array.isArray(r && r.segments) ? r.segments : []) {
      for (const cat of ['jams', 'roadworks']) {
        for (const x of Array.isArray(s && s[cat]) ? s[cat] : []) {
          if (!x || typeof x !== 'object' || uit.length >= 400) continue;
          const weg = tekst(x.road, 6).toUpperCase();
          if (!/^[AN]\d{1,3}$/.test(weg)) continue;
          const start = tijd(x.start), eind = tijd(x.stop);
          if ((start && start > nu) || (eind && eind < nu)) continue; // gepland of al voorbij
          const ev = (Array.isArray(x.events) ? x.events : []).map((e) => tekst(e && e.text, 80)).filter(Boolean);
          const alles = ev.join(' ').toLowerCase();
          const type = tekst(x.incidentType, 30);
          let k = null;
          // Alleen voor vrachtverkeer/bussen? Dan niet relevant voor een personenauto.
          if (/vrachtwagen|vrachtverkeer|zware voertuigen|autobussen/.test(alles) && !/ongeluk|stilstaand|langzaam/.test(alles)) continue;
          if (/spookrijder/.test(alles)) k = 'spook';
          else if (/ongeluk/.test(alles)) k = 'accident';
          else if (type === 'road-closed') k = /op- en afrit|oprit|afrit|verbindingsweg/.test(alles) ? 'ramp' : 'closure';
          else if (type === 'stationary-traffic' || type === 'slow-traffic' || type === 'queuing-traffic') k = 'jam';
          else if (type === 'roadworks' || cat === 'roadworks') k = 'lane';
          if (!k) continue; // bv. 'unknown' (alleen voor vrachtverkeer) overslaan
          let pts = typeof x.polyline === 'string' && x.polyline.length < 20000 ? ontcijferLijn(x.polyline) : [];
          pts = pts.filter((p) => p[0] > 50.6 && p[0] < 53.7 && p[1] > 3.0 && p[1] < 7.4);
          if (!pts.length) {
            if (goedePlek(x.fromLoc)) pts.push([+x.fromLoc.lat, +x.fromLoc.lon]);
            if (goedePlek(x.toLoc) && (+x.toLoc.lat !== +x.fromLoc?.lat || +x.toLoc.lon !== +x.fromLoc?.lon)) pts.push([+x.toLoc.lat, +x.toLoc.lon]);
          }
          if (!pts.length) continue;
          if (pts.length > 80) { const st = Math.ceil(pts.length / 80), houd = []; for (let i = 0; i < pts.length; i += st) houd.push(pts[i]); houd.push(pts[pts.length - 1]); pts = houd; }
          pts = pts.map((p) => [Math.round(p[0] * 1e5) / 1e5, Math.round(p[1] * 1e5) / 1e5]);
          const id = 'anwb_' + String(x.id == null ? weg + '_' + pts[0].join('_') : x.id).replace(/[^A-Za-z0-9_.-]/g, '').slice(0, 40);
          if (gezien.has(id)) continue;
          gezien.add(id);
          const van = tekst(s.start, 60), naar = tekst(s.end, 60);
          const vertraging = Number(x.delay), lengte = Number(x.distance);
          uit.push({
            id, k, weg,
            van: tekst(x.from, 80), naar: tekst(x.to, 80),
            richting: van && naar ? van + ' richting ' + naar : '',
            tekst: ev[0] || '',
            vertraging: isFinite(vertraging) && vertraging > 0 ? Math.round(vertraging) : null,
            lengte: isFinite(lengte) && lengte > 0 ? Math.round(lengte) : null,
            start: start ? new Date(start).toISOString() : null,
            eind: eind ? new Date(eind).toISOString() : null,
            pts,
          });
        }
      }
    }
  }
  const bijgewerkt = tijd(j.dateTime);
  return { success: true, bijgewerkt: new Date(bijgewerkt || nu).toISOString(), meldingen: uit };
}

// Google-polyline (precisie 5) naar [[lat, lon], ...]
function ontcijferLijn(str) {
  const pts = []; let i = 0, lat = 0, lon = 0;
  while (i < str.length && pts.length < 5000) {
    let res = 0, sh = 0, b;
    do { b = str.charCodeAt(i++) - 63; res |= (b & 31) << sh; sh += 5; } while (b >= 32 && i < str.length);
    lat += res & 1 ? ~(res >> 1) : res >> 1;
    res = 0; sh = 0;
    do { b = str.charCodeAt(i++) - 63; res |= (b & 31) << sh; sh += 5; } while (b >= 32 && i < str.length);
    lon += res & 1 ? ~(res >> 1) : res >> 1;
    pts.push([lat / 1e5, lon / 1e5]);
  }
  return pts;
}

// Opbouw van het ANWB-antwoord: roads[].segments[] = { start, end, radars: [ { id, road, HM, loc: {lat, lon}, from, to, events } ] }
function leesAnwb(j) {
  if (!j || j.success !== true || !Array.isArray(j.roads)) throw new Error('onverwacht antwoord van de ANWB');
  const tijd = typeof j.dateTime === 'string' && isFinite(Date.parse(j.dateTime)) ? new Date(Date.parse(j.dateTime)).toISOString() : new Date().toISOString();
  const tekst = (v, max) => (typeof v === 'string' ? v.replace(/\s+/g, ' ').trim().slice(0, max) : '');
  const uit = [], gezien = new Set();
  for (const r of j.roads) {
    for (const s of Array.isArray(r && r.segments) ? r.segments : []) {
      for (const x of Array.isArray(s && s.radars) ? s.radars : []) {
        if (!x || typeof x !== 'object' || uit.length >= 200) continue;
        const weg = tekst(x.road, 6).toUpperCase();
        if (!/^[AN]\d{1,3}$/.test(weg)) continue;
        const lat = Number(x.loc && x.loc.lat), lon = Number(x.loc && x.loc.lon);
        if (!isFinite(lat) || !isFinite(lon) || lat < 50.6 || lat > 53.7 || lon < 3.0 || lon > 7.4) continue;
        const id = 'anwb_' + String(x.id == null ? weg + '_' + lat + '_' + lon : x.id).replace(/[^A-Za-z0-9_.-]/g, '').slice(0, 40);
        if (gezien.has(id)) continue;
        gezien.add(id);
        const hm = Number(x.HM);
        const van = tekst(s.start, 60), naar = tekst(s.end, 60);
        const soort = tekst(x.events && x.events[0] && x.events[0].text, 60) || 'Snelheidscontrole';
        const tussen = tekst(x.from, 60) && tekst(x.to, 60) ? ' tussen ' + tekst(x.from, 60) + ' en ' + tekst(x.to, 60) : '';
        uit.push({
          id, weg,
          hm: isFinite(hm) ? Math.round(hm * 10) / 10 : null,
          richting: van && naar ? van + ' richting ' + naar : '',
          omschrijving: (soort + tussen).slice(0, 160),
          latitude: Math.round(lat * 1e6) / 1e6,
          longitude: Math.round(lon * 1e6) / 1e6,
          tijd,
          bron: 'anwb',
        });
      }
    }
  }
  return uit;
}

async function haalRss(ctx) {
  const target = 'https://flitsers.nl/flitsers.rss';
  const cache = caches.default;
  const key = new Request(target);
  let res = await cache.match(key);
  if (!res) {
    const up = await fetch(target, { headers: { 'User-Agent': 'Buddy persoonlijk gebruik' } });
    if (!up.ok) throw new Error('flitsers.nl gaf status ' + up.status);
    res = new Response(up.body, up);
    res.headers.set('Cache-Control', 'public, max-age=300');
    ctx.waitUntil(cache.put(key, res.clone()));
  }
  return await res.text();
}

// Feed uitlezen zonder DOMParser (bestaat niet in een Worker)
function leesFeed(xml) {
  const uit = [];
  const tekst = (blok, tag) => {
    const m = blok.match(new RegExp('<' + tag + '\\b[^>]*>([\\s\\S]*?)</' + tag + '>', 'i'));
    if (!m) return '';
    return m[1].replace(/^\s*<!\[CDATA\[([\s\S]*?)\]\]>\s*$/, '$1')
      .replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#0*39;|&apos;/g, "'").replace(/&amp;/g, '&')
      .trim();
  };
  const items = String(xml || '').match(/<item\b[\s\S]*?<\/item>/gi) || [];
  for (const blok of items.slice(0, 200)) {
    const titel = tekst(blok, 'title').replace(/\s+/g, ' ').slice(0, 300);
    if (!titel) continue;
    const guid = tekst(blok, 'guid').replace(/[^A-Za-z0-9_-]/g, '').slice(0, 64);
    const pub = Date.parse(tekst(blok, 'pubDate')) || 0;
    const m = titel.match(/^\s*([AN])\s?(\d{1,3})\b(.*?)\bhmp\s+(\d+(?:[.,]\d+)?)/i);
    const o = { id: guid || 'f' + hash(titel + pub), pub, weg: null, hm: null, richting: '', omschrijving: 'Snelheidscontrole' };
    if (m) {
      const hm = parseFloat(m[4].replace(',', '.'));
      if (isFinite(hm) && hm < 1000) {
        o.weg = m[1].toUpperCase() + (+m[2]);
        o.hm = Math.round(hm * 10) / 10;
        const midden = m[3].trim();
        const bij = midden.search(/\sbij\s/i);
        o.richting = (bij >= 0 ? midden.slice(0, bij) : midden).trim().slice(0, 120);
        if (bij >= 0) o.omschrijving = ('Snelheidscontrole ' + midden.slice(bij).trim()).slice(0, 160);
      }
    }
    uit.push(o);
  }
  return uit;
}

function hash(s) { let h = 0; for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0; return (h >>> 0).toString(36); }

// ---------- Wegtabellen ----------
// Een tabel: { weg: 'A73', w: '073', url, klaar, t, n, hm: { '17.0': [[lat, lon], ...] } }
function startUrl(w) { return FL_PDOK + '?f=json&limit=' + FL_PAGINA + '&wegnummer=' + encodeURIComponent(w); }

async function laadWeg(env, weg) {
  if (flWegen.has(weg)) return flWegen.get(weg);
  let tab = null;
  if (env.FLITS) { try { tab = await env.FLITS.get(FL_KV + weg, 'json'); } catch (e) { tab = null; } }
  if (!tab || !tab.hm) {
    const nr = weg.slice(1);
    tab = { weg, w: nr.padStart(3, '0'), url: null, klaar: false, t: 0, n: 0, hm: {} };
    tab.url = startUrl(tab.w);
  }
  tab.bezig = false;
  flWegen.set(weg, tab);
  return tab;
}

// Haalt hooguit `max` pagina's op voor deze weg. Geeft het aantal gebruikte pagina's terug.
async function bouwVerder(tab, max) {
  if (tab.klaar) { tab.klaar = false; tab.n = 0; tab.url = startUrl(tab.w); } // vernieuwen
  const letter = tab.weg[0];
  let gebruikt = 0;
  while (tab.url && gebruikt < max) {
    gebruikt++;
    const r = await fetch(tab.url, { headers: { 'User-Agent': 'Buddy persoonlijk gebruik', Accept: 'application/geo+json, application/json' } });
    if (!r.ok) throw new Error('PDOK gaf status ' + r.status + (r.status === 500 ? ' (te druk, volgende keer opnieuw)' : ''));
    const j = await r.json();
    for (const f of Array.isArray(j.features) ? j.features : []) {
      const pr = f.properties || {};
      if (String(pr.wegnummer) !== tab.w) continue;
      if (pr.routeltr && String(pr.routeltr).toUpperCase() !== letter) continue;
      const b = pr.beginkm, e = pr.eindkm, g = f.geometry;
      if (typeof b !== 'number' || typeof e !== 'number' || b === e || !g || !Array.isArray(g.coordinates)) continue;
      const lijn = g.type === 'MultiLineString' ? [].concat(...g.coordinates) : g.coordinates;
      if (!lijn || lijn.length < 2) continue;
      tab.n++;
      const lo = Math.min(b, e), hi = Math.max(b, e);
      for (let k = Math.ceil(lo * 10 - 1e-6); k <= Math.floor(hi * 10 + 1e-6); k++) {
        const pt = puntOpLijn(lijn, (k / 10 - b) / (e - b));
        if (!pt) continue;
        const sl = (k / 10).toFixed(1);
        voegToe(tab.hm[sl] || (tab.hm[sl] = []), pt);
      }
    }
    const volgende = (j.links || []).find((l) => l.rel === 'next');
    tab.url = volgende && typeof volgende.href === 'string' && volgende.href.startsWith('https://api.pdok.nl/') ? volgende.href : null;
  }
  if (!tab.url) {
    // Niets gevonden met '073'? Dan nog één keer proberen met '73'.
    const kaal = String(+tab.weg.slice(1));
    if (tab.n === 0 && tab.w !== kaal) { tab.w = kaal; tab.url = startUrl(kaal); }
    else { tab.klaar = true; tab.t = Date.now(); }
  }
  return gebruikt;
}

function voegToe(lijst, pt) {
  // Twee punten dichter dan 15 m bij elkaar tellen als één
  for (const q of lijst) if (afstand(q[0], q[1], pt[0], pt[1]) < 15) return;
  if (lijst.length < 4) lijst.push(pt);
}

function afstand(la1, lo1, la2, lo2) {
  const R = 6371e3, r = Math.PI / 180;
  const dLa = (la2 - la1) * r, dLo = (lo2 - lo1) * r;
  const a = Math.sin(dLa / 2) ** 2 + Math.cos(la1 * r) * Math.cos(la2 * r) * Math.sin(dLo / 2) ** 2;
  return 2 * R * Math.asin(Math.sqrt(a));
}

// Punt op een lijn (coördinaten [lon, lat]) op fractie t van de lengte. Geeft [lat, lon].
function puntOpLijn(c, t) {
  if (!isFinite(t)) return null;
  const L = []; let tot = 0;
  for (let i = 1; i < c.length; i++) { const d = afstand(c[i - 1][1], c[i - 1][0], c[i][1], c[i][0]); L.push(d); tot += d; }
  const rond = (v) => Math.round(v * 1e6) / 1e6;
  if (!tot) return [rond(c[0][1]), rond(c[0][0])];
  const wil = Math.max(0, Math.min(1, t)) * tot; let acc = 0;
  for (let i = 0; i < L.length; i++) {
    if (acc + L[i] >= wil) {
      const f = L[i] ? (wil - acc) / L[i] : 0;
      return [rond(c[i][1] + (c[i + 1][1] - c[i][1]) * f), rond(c[i][0] + (c[i + 1][0] - c[i][0]) * f)];
    }
    acc += L[i];
  }
  const e = c[c.length - 1]; return [rond(e[1]), rond(e[0])];
}

// ---------------------------------------------------------------------------
// Groepsberichten (zwaai, koffie, file, pech, hartjes, bijna thuis, mijlpaal) en leaderboard.
// Alles in dezelfde KV-opslag FLITS: 'p:<groep>' voor berichten, 'l:<groep>' voor het leaderboard.
// Er wordt geen exacte plek gedeeld, behalve bij 'file' (die moet op de kaart om te waarschuwen).
// ---------------------------------------------------------------------------
const GR_SOORTEN = ['zwaai', 'koffie', 'koffieJa', 'koffieStraks', 'file', 'pech', 'hart', 'thuis', 'mijlpaal', 'gezien', 'feli', 'test'];
const GR_BEWAAR_MS = 2 * 3600e3, GR_MAX = 100, GR_LEDEN = 50, GR_PAUZE_MS = 120e3; // elk soort bericht hooguit eens per 2 minuten

async function groep(url, env, g, json) {
  const q = (k) => url.searchParams.get(k) || '';
  const naam = (v) => v.replace(/[^A-Za-zÀ-ſ '\-]/g, '').trim().slice(0, 20);
  const auto = (v) => v.replace(/[^A-Za-z0-9À-ſ '\-]/g, '').trim().slice(0, 30);
  const idv = (v) => v.replace(/[^a-zA-Z0-9]/g, '').slice(0, 24);
  const getal = (v, max) => { const n = Math.round(Number(v)); return isFinite(n) && n >= 0 ? Math.min(n, max) : 0; };
  const nu = Date.now(), p = url.pathname;

  if (p === '/cams/ping' || p === '/cams/pings') {
    const key = 'p:' + g;
    let lijst = [];
    try { lijst = (await env.FLITS.get(key, 'json')) || []; } catch (e) { lijst = []; }
    lijst = lijst.filter((x) => x && nu - x.t < GR_BEWAAR_MS);
    if (p === '/cams/pings') {
      const sinds = Number(q('sinds')) || 0;
      return json({ pings: lijst.filter((x) => x.t > sinds), nu });
    }
    const s = q('s'), van = idv(q('van')), id = idv(q('id'));
    if (!GR_SOORTEN.includes(s)) return json({ error: 'Onbekende soort bericht.' }, 400);
    if (!van || !id) return json({ error: 'Afzender of id ontbreekt.' }, 400);
    if (lijst.some((x) => x.id === id)) return json({ ok: true, dubbel: true, nu });
    // Niet spammen: dezelfde soort van dezelfde afzender hooguit eens per 2 minuten
    if (s !== 'koffieJa' && s !== 'koffieStraks' && s !== 'gezien' && s !== 'feli' && lijst.some((x) => x.van === van && x.s === s && nu - x.t < GR_PAUZE_MS)) return json({ ok: true, te_snel: true, nu }); // antwoorden op koffie mogen altijd
    const b = { id, van, s, t: nu, n: naam(q('n')), a: auto(q('a')), p: naam(q('p')) };
    const aan = idv(q('aan')); if (aan) b.aan = aan;
    if (s === 'gezien') { const r = idv(q('r')); if (!aan || !r) return json({ error: 'Gezien zonder ontvanger of bericht.' }, 400); b.r = r; }
    if (s === 'file') {
      const lat = parseFloat(q('lat')), lon = parseFloat(q('lon'));
      if (!(lat > 50.6 && lat < 53.7 && lon > 3.0 && lon < 7.4)) return json({ error: 'Ongeldige plek voor de file.' }, 400);
      b.lat = +lat.toFixed(5); b.lon = +lon.toFixed(5);
    }
    if (s === 'mijlpaal') b.w = getal(q('w'), 1e6);
    lijst.push(b);
    if (lijst.length > GR_MAX) lijst = lijst.slice(-GR_MAX);
    await env.FLITS.put(key, JSON.stringify(lijst));
    return json({ ok: true, nu });
  }

  // Uit de groep stappen: van het leaderboard en uit "in de buurt" halen
  if (p === '/cams/weg') {
    const van = idv(q('van'));
    if (!van) return json({ error: 'Afzender ontbreekt.' }, 400);
    let leden = [], map = {};
    try { leden = (await env.FLITS.get('l:' + g, 'json')) || []; } catch (e) {}
    try { map = (await env.FLITS.get('h:' + g, 'json')) || {}; } catch (e) {}
    const n0 = leden.length; leden = leden.filter((x) => x && x.van !== van);
    if (leden.length !== n0) await env.FLITS.put('l:' + g, JSON.stringify(leden));
    if (map[van]) { delete map[van]; await env.FLITS.put('h:' + g, JSON.stringify(map)); }
    return json({ ok: true });
  }

  // Collega in de buurt: je positie wordt 3 minuten bewaard en NOOIT teruggegeven, alleen namen van wie binnen 600 m is (en vanaf Buddy v63: tot 5 km, met de afstand afgerond op hele km)
  if (p === '/cams/hier') {
    const key = 'h:' + g, van = idv(q('van'));
    if (!van) return json({ error: 'Afzender ontbreekt.' }, 400);
    let map = {};
    try { map = (await env.FLITS.get(key, 'json')) || {}; } catch (e) { map = {}; }
    for (const k of Object.keys(map)) if (!map[k] || nu - map[k].t > 3 * 60e3) delete map[k];
    if (q('uit')) { delete map[van]; await env.FLITS.put(key, JSON.stringify(map)); return json({ ok: true, buren: [] }); }
    const lat = parseFloat(q('lat')), lon = parseFloat(q('lon'));
    if (!(lat > 50.6 && lat < 53.7 && lon > 3.0 && lon < 7.4)) return json({ error: 'Ongeldige plek.' }, 400);
    const buren = [];
    for (const [k, v] of Object.entries(map)) {
      if (k === van) continue;
      const d = Math.hypot((v.lat - lat) * 111195, (v.lon - lon) * 111195 * Math.cos(lat * Math.PI / 180));
      if (d < 600) buren.push({ van: k, n: v.n, a: v.a });
      else if (q('ver') && d < 5000) buren.push({ van: k, n: v.n, a: v.a, d: Math.max(1, Math.round(d / 1000)) }); // v63: verder weg, alleen hele km
    }
    // Zuinig met schrijven (gratis plan: 1000 per dag): alleen opslaan als je echt verplaatst bent of de vorige oud wordt
    const ik = map[van];
    const verplaatst = !ik || Math.hypot((ik.lat - lat) * 111195, (ik.lon - lon) * 111195 * Math.cos(lat * Math.PI / 180)) > 150 || nu - ik.t > 150e3;
    if (verplaatst) {
      map[van] = { lat: +lat.toFixed(4), lon: +lon.toFixed(4), t: nu, n: naam(q('n')), a: auto(q('a')) };
      if (Object.keys(map).length > GR_LEDEN) { const oudste = Object.entries(map).sort((a, b) => a[1].t - b[1].t)[0]; delete map[oudste[0]]; }
      await env.FLITS.put(key, JSON.stringify(map));
    }
    return json({ ok: true, buren });
  }

  // Leaderboard
  const key = 'l:' + g;
  let leden = [];
  try { leden = (await env.FLITS.get(key, 'json')) || []; } catch (e) { leden = []; }
  if (p === '/cams/bord') return json({ leden, nu });
  const van = idv(q('van'));
  if (!van) return json({ error: 'Afzender ontbreekt.' }, 400);
  const lid = { van, n: naam(q('n')), a: auto(q('a')), km: getal(q('km'), 1e6), netjes: getal(q('netjes'), 1e5), gemeld: getal(q('gemeld'), 1e5), stijl: getal(q('stijl'), 100), ritten: getal(q('ritten'), 1e5), t: nu };
  // 0-100 en 0-80 in tienden van seconden; onrealistische tijden worden 0 (= geen tijd)
  const nul = getal(q('nul'), 1000); lid.nul = nul >= 35 && nul <= 150 ? nul : 0;
  const tachtig = getal(q('tachtig'), 1000); lid.tachtig = tachtig >= 25 && tachtig <= 90 ? tachtig : 0; // 0-80, idem
  // Weekchallenge: waarde van deze week; bij een nieuwe week schuift de oude door naar 'vorige' (voor de winnaar)
  const oud = leden.find((x) => x && x.van === van);
  // Soepelste rijder van de week (0-100, gemeten met de bewegingssensor)
  const zw = getal(q('zw'), 1e5), zv = getal(q('zv'), 100);
  if (zw && zv) lid.zacht = { w: zw, v: zv }; else if (oud && oud.zacht) lid.zacht = oud.zacht;
  const ww = getal(q('ww'), 1e5), wv = getal(q('wv'), 1e6);
  if (ww) {
    lid.week = { w: ww, v: wv };
    if (oud && oud.week && oud.week.w < ww) lid.vorige = oud.week;
    else if (oud && oud.vorige) lid.vorige = oud.vorige;
  } else if (oud) { if (oud.week) lid.week = oud.week; if (oud.vorige) lid.vorige = oud.vorige; }
  lid.bd = q('bd').replace(/[^a-z0-9.]/g, '').slice(0, 80);
  lid.lv = getal(q('lv'), 999);
  lid.jr = /^(0[1-9]|1[0-2])(0[1-9]|[12]\d|3[01])$/.test(q('jr')) ? q('jr') : '';
  leden = leden.filter((x) => x && x.van !== van);
  leden.push(lid);
  if (leden.length > GR_LEDEN) leden = leden.sort((x, y) => y.t - x.t).slice(0, GR_LEDEN);
  await env.FLITS.put(key, JSON.stringify(leden));
  return json({ ok: true, nu });
}

// ---------------------------------------------------------------------------
// Back-up van Buddy: alles wat hij onthoudt, onder een geheime herstelcode van 12 tekens.
//   GET  /backup?code=XXXXXXXXXXXX   -> { ok, t, data }
//   POST /backup?code=XXXXXXXXXXXX   (body: JSON als text/plain) -> { ok, t }
// ---------------------------------------------------------------------------
const BK_MAX = 900 * 1024;
async function backup(request, url, env, cors) {
  const json = (obj, status = 200) => new Response(JSON.stringify(obj), { status, headers: { ...cors, 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });
  if (!env.FLITS) return json({ error: 'KV-opslag FLITS is nog niet gekoppeld aan de Worker.' }, 501);
  const code = (url.searchParams.get('code') || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
  if (!/^[A-Z0-9]{12}$/.test(code)) return json({ error: 'Ongeldige herstelcode.' }, 400);
  const key = 'b:' + code, nu = Date.now();
  if (request.method === 'GET') {
    let b = null;
    try { b = await env.FLITS.get(key, 'json'); } catch (e) { b = null; }
    if (!b) return json({ error: 'Geen back-up gevonden met deze code.' }, 404);
    return json({ ok: true, t: b.t, data: b.data });
  }
  if (request.method !== 'POST') return json({ error: 'Alleen GET of POST.' }, 405);
  const tekst = await request.text();
  if (tekst.length > BK_MAX) return json({ error: 'Back-up te groot.' }, 413);
  let data;
  try { data = JSON.parse(tekst); } catch (e) { return json({ error: 'Back-up is geen geldige JSON.' }, 400); }
  if (!data || typeof data !== 'object' || Array.isArray(data)) return json({ error: 'Back-up heeft een verkeerde vorm.' }, 400);
  for (const [k, v] of Object.entries(data)) {
    if (!/^dashboardmaatje-[a-z0-9-]{1,40}$/.test(k) || typeof v !== 'string') return json({ error: 'Back-up bevat een onbekend onderdeel: ' + k.slice(0, 40) }, 400);
  }
  // Niet vaker dan eens per minuut (zuinig met het gratis plan)
  let oud = null;
  try { oud = await env.FLITS.get(key, 'json'); } catch (e) { oud = null; }
  if (oud && nu - oud.t < 60e3) return json({ ok: true, t: oud.t, te_snel: true });
  await env.FLITS.put(key, JSON.stringify({ t: nu, data }));
  return json({ ok: true, t: nu });
}
// ---------------------------------------------------------------------------
// /osm: maximumsnelheden en vaste flitsers uit OpenStreetMap, via deze Worker.
//   GET /osm?soort=wegen&lat=..&lon=..   -> wegen met hun limiet in een vak van ca. 3,3 x 3,4 km
//   GET /osm?soort=flits&lat=..&lon=..   -> vaste flitsers en trajecten in een gebied van ca. 30 x 30 km
//   GET /osm?soort=werk&lat=..&lon=..    -> wegwerkzaamheden in een gebied van ca. 35 x 35 km
// Het antwoord is precies wat Overpass zelf geeft, zodat de app het direct kan lezen.
// Elk vak wordt één keer opgehaald en bewaard in de KV-opslag FLITS (wegen 30 dagen, flitsers 7 dagen, werk 1 dag).
// Daardoor hoeft de app de overvolle openbare Overpass-servers bijna nooit meer zelf te vragen.
// Let op: de app rekent met dezelfde vakmaten (OSM_VAK). Verander je ze hier, doe dat daar ook.
// ---------------------------------------------------------------------------
const OSM_MIRRORS = [
  'https://overpass-api.de/api/interpreter',
  'https://overpass.private.coffee/api/interpreter',
  'https://overpass.kumi.systems/api/interpreter',
  'https://maps.mail.ru/osm/tools/overpass/api/interpreter',
];
const OSM_WEGTYPES = 'motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link';
const OSM_VAK = {
  wegen: { dLat: 0.03, dLon: 0.05, padLat: 0.006, padLon: 0.009, versDagen: 30 },
  flits: { dLat: 0.10, dLon: 0.15, padLat: 0.11, padLon: 0.17, versDagen: 7 },
  werk:  { dLat: 0.10, dLon: 0.15, padLat: 0.13, padLon: 0.2, versDagen: 1 },   // wegwerkzaamheden: elke dag vers
  laad:  { dLat: 0.02, dLon: 0.03, padLat: 0.004, padLon: 0.006, versDagen: 30 },  // laadpalen in de buurt (laadpauze)
};

async function osm(url, env, ctx, cors) {
  const soort = url.searchParams.get('soort');
  const vak = OSM_VAK[soort];
  const fout = (tekst, status) => new Response(JSON.stringify({ error: tekst }), { status, headers: { ...cors, 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });
  if (!vak) return fout('soort moet wegen, flits, werk of laad zijn', 400);
  const lat = parseFloat(url.searchParams.get('lat')), lon = parseFloat(url.searchParams.get('lon'));
  if (!(lat > 49 && lat < 55.5 && lon > 1.5 && lon < 10)) return fout('lat/lon ontbreekt of ligt buiten het gebied', 400);

  const ty = Math.floor(lat / vak.dLat), tx = Math.floor(lon / vak.dLon);
  const sleutel = 'osm:v2:' + soort + ':' + ty + ':' + tx; // v2: ook losse punten met enforcement=maxspeed
  const klaar = (tekst, bron) => new Response(tekst, { headers: { ...cors, 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'X-Buddy-Bron': bron, 'Access-Control-Expose-Headers': 'X-Buddy-Bron' } });

  // 1. Al bewaard en nog vers?
  let oud = null;
  if (env.FLITS) {
    try { oud = await env.FLITS.getWithMetadata(sleutel); } catch (e) { oud = null; }
    if (oud && oud.value && oud.metadata && Date.now() - oud.metadata.t < vak.versDagen * 864e5) return klaar(oud.value, 'bewaard');
  }

  // 2. Ophalen bij Overpass
  const s = (ty * vak.dLat - vak.padLat).toFixed(4), w = (tx * vak.dLon - vak.padLon).toFixed(4);
  const n = ((ty + 1) * vak.dLat + vak.padLat).toFixed(4), e = ((tx + 1) * vak.dLon + vak.padLon).toFixed(4);
  const bb = s + ',' + w + ',' + n + ',' + e;
  const query = soort === 'wegen'
    ? '[out:json][timeout:25];way["highway"~"^(' + OSM_WEGTYPES + ')$"](' + bb + ');out tags geom;'
    : soort === 'laad'
    ? '[out:json][timeout:20];(node["amenity"="charging_station"](' + bb + ');way["amenity"="charging_station"](' + bb + '););out center tags;'
    : soort === 'werk'
    ? '[out:json][timeout:25];(way["highway"="construction"](' + bb + ');way["construction:highway"](' + bb + ');node["highway"="construction"](' + bb + ');way["highway"]["construction"](' + bb + '););out center tags 400;'
    : '[out:json][timeout:25];node["highway"="speed_camera"](' + bb + ')->.c;node["enforcement"="maxspeed"](' + bb + ')->.e;rel["type"="enforcement"]["enforcement"~"^(maxspeed|average_speed)$"](' + bb + ')->.r;node(r.r)->.m;(.c;.e;.m;.r;);out body;';
  try {
    const tekst = await osmVraag(query);
    if (env.FLITS) ctx.waitUntil(env.FLITS.put(sleutel, tekst, { metadata: { t: Date.now() } }).catch(() => {}));
    return klaar(tekst, 'overpass');
  } catch (err) {
    // 3. Overpass faalt: liever een oudere versie dan niks
    if (oud && oud.value) return klaar(oud.value, 'oud');
    return fout('OpenStreetMap nu niet bereikbaar (' + String((err && err.message) || err).slice(0, 120) + ')', 503);
  }
}

// Vraagt de servers één voor één, maar wacht niet langer dan 6 s voordat de volgende ook mag.
// De eerste die een goed antwoord geeft, wint. Elke server krijgt hooguit 25 s.
function osmVraag(query) {
  return new Promise((ok, nee) => {
    let gestart = 0, mislukt = 0, klaar = false, laatste = '';
    const ctls = [];
    const volgende = () => {
      if (klaar || gestart >= OSM_MIRRORS.length) return;
      const adres = OSM_MIRRORS[gestart++], host = new URL(adres).host;
      const ctl = new AbortController(); ctls.push(ctl);
      const tm = setTimeout(() => ctl.abort(), 25000);
      const wacht = setTimeout(volgende, 6000);
      fetch(adres, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded', 'User-Agent': 'Buddy persoonlijk gebruik' }, body: 'data=' + encodeURIComponent(query), signal: ctl.signal })
        .then(async (r) => {
          if (!r.ok) throw new Error('http ' + r.status);
          const tekst = await r.text();
          // Overpass geeft soms "200 OK" met een foutmelding erin
          if (!tekst.startsWith('{') || !tekst.includes('"elements"') || /"remark"\s*:\s*"[^"]*(runtime error|timed out|out of memory)/i.test(tekst)) throw new Error('server druk');
          return tekst;
        })
        .then((tekst) => {
          clearTimeout(tm);
          if (klaar) return;
          klaar = true; clearTimeout(wacht);
          ctls.forEach((c) => { if (c !== ctl) { try { c.abort(); } catch (e) {} } });
          ok(tekst);
        })
        .catch((e) => {
          clearTimeout(tm);
          if (klaar) return;
          laatste = host + ': ' + (e && e.name === 'AbortError' ? 'te traag' : (e && e.message) || 'fout');
          mislukt++;
          if (mislukt >= OSM_MIRRORS.length) { klaar = true; nee(new Error(laatste)); }
          else if (mislukt >= gestart) { clearTimeout(wacht); volgende(); }
        });
    };
    volgende();
  });
}
