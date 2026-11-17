// Buddy live: iedereen uit een groep die rijdt, stuurt elke seconde zijn plek. De groep (een Durable Object)
// stuurt dat alleen door naar groepsleden die binnen 10 km rijden. Niets wordt bewaard: valt de verbinding weg, dan ben je weg.
const TOEGESTAAN = 'https://toxicradium.github.io';
const BEREIK = 10000;   // meter (v200: 10 km)

export default {
  async fetch(request, env) {
    const url = new URL(request.url), origin = request.headers.get('Origin') || '';
    if (origin && origin !== TOEGESTAAN) return new Response('Niet toegestaan', { status: 403 });
    if (url.pathname === '/') return new Response('Buddy live draait', { headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
    const cors = { 'Access-Control-Allow-Origin': TOEGESTAAN, 'Access-Control-Allow-Methods': 'GET, POST', 'Access-Control-Allow-Headers': 'Content-Type', 'Vary': 'Origin' };
    if (request.method === 'OPTIONS') return new Response(null, { headers: cors });
    const g = (url.searchParams.get('g') || '').toLowerCase();
    if (!/^[a-z0-9]{6,32}$/.test(g)) return new Response('Groepscode klopt niet', { status: 400, headers: cors });
    const groep = env.GROEP.get(env.GROEP.idFromName(g));
    if (url.pathname === '/verover' || url.pathname === '/spoor' || url.pathname === '/sporen') {   // v192: gemeentes veroveren en ghosts
      const r = await groep.fetch(request); const h = new Headers(r.headers); for (const [k, v] of Object.entries(cors)) h.set(k, v); h.set('Content-Type', 'application/json'); h.set('Cache-Control', 'no-store');
      return new Response(r.body, { status: r.status, headers: h });
    }
    if (url.pathname !== '/live') return new Response('Niet gevonden', { status: 404 });
    if (request.headers.get('Upgrade') !== 'websocket') return new Response('Verwacht een websocket', { status: 426 });
    return groep.fetch(request);
  },
};

const schoon = (v, n) => String(v || '').replace(/[^A-Za-z0-9À-ſ '\-]/g, '').trim().slice(0, n);
const afst = (a, b) => Math.hypot((a.lat - b.lat) * 111195, (a.lon - b.lon) * 111195 * Math.cos(a.lat * Math.PI / 180));

export class BuddyGroep {
  constructor(state) { this.state = state; }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === '/verover') return this.verover(request, url);
    if (url.pathname === '/spoor') return this.spoorBewaar(request);
    if (url.pathname === '/sporen') return this.sporen(url);
    const van = schoon(url.searchParams.get('van'), 24).replace(/[^a-zA-Z0-9]/g, '');
    if (!van) return new Response('Afzender ontbreekt', { status: 400 });
    // Dezelfde afzender al verbonden (bv. na herverbinden)? Oude verbinding sluiten.
    for (const ws of this.state.getWebSockets()) { const a = ws.deserializeAttachment(); if (a && a.van === van) try { ws.close(1000, 'vervangen'); } catch (e) {} }
    const paar = new WebSocketPair(), [klant, server] = Object.values(paar);
    this.state.acceptWebSocket(server);
    server.serializeAttachment({ van, n: schoon(url.searchParams.get('n'), 20), a: schoon(url.searchParams.get('a'), 30) });
    return new Response(null, { status: 101, webSocket: klant });
  }

  async webSocketMessage(ws, raw) {
    let m; try { m = JSON.parse(raw); } catch (e) { return; }
    const ik = ws.deserializeAttachment() || {}, nu = Date.now();
    if (m.t === 'p') {   // plek: {t:'p', lat, lon, hd, v}
      const lat = +m.lat, lon = +m.lon;
      if (!(lat > 50.6 && lat < 53.7 && lon > 3.0 && lon < 7.4)) return;
      ik.lat = +lat.toFixed(5); ik.lon = +lon.toFixed(5); ik.hd = Math.round(+m.hd || 0) % 360; ik.v = Math.max(0, Math.min(250, Math.round(+m.v || 0))); ik.ts = nu;
      ws.serializeAttachment(ik);
      const uit = JSON.stringify({ t: 'p', van: ik.van, n: ik.n, a: ik.a, lat: ik.lat, lon: ik.lon, hd: ik.hd, v: ik.v });
      for (const ander of this.state.getWebSockets()) { if (ander === ws) continue; const b = ander.deserializeAttachment();
        if (b && b.ts && nu - b.ts < 30e3 && afst(ik, b) < BEREIK) try { ander.send(uit); } catch (e) {} }
      return;
    }
    if (m.t === 'stil') { delete ik.lat; delete ik.ts; ws.serializeAttachment(ik); this.weg(ws, ik.van); return; }
    if (m.t === 'x' && m.aan) {   // iets voor één maatje (raket, nitro-blok, raak), alleen als die in de buurt rijdt
      for (const ander of this.state.getWebSockets()) { const b = ander.deserializeAttachment();
        if (b && b.van === m.aan && b.ts && ik.ts && afst(ik, b) < BEREIK) try { ander.send(JSON.stringify({ t: 'x', van: ik.van, n: ik.n, d: m.d || {} })); } catch (e) {} }
    }
  }

  // ---- gemeentes veroveren: per maand de km per gemeente per groepslid ----
  async verover(request, url) {
    const maand = /^\d{4}-\d{2}$/.test(url.searchParams.get('m') || '') ? url.searchParams.get('m') : new Date().toISOString().slice(0, 7);
    const sleutel = 'v:' + maand; let tab = (await this.state.storage.get(sleutel)) || { gm: {}, plek: {} };
    if (request.method === 'POST') {
      let b; try { b = await request.json(); } catch (e) { return new Response('{"error":"geen json"}', { status: 400 }); }
      const van = schoon(b.van, 24).replace(/[^a-zA-Z0-9]/g, ''), n = schoon(b.n, 20); if (!van) return new Response('{"error":"van ontbreekt"}', { status: 400 });
      let tel = 0; for (const [gm0, km0] of Object.entries(b.km || {})) { const gm = schoon(gm0, 40), km = Math.max(0, Math.min(20000, +km0 || 0)); if (!gm || tel++ > 400) continue;
        tab.gm[gm] = tab.gm[gm] || {}; tab.gm[gm][van] = { n, km: Math.round(km * 10) / 10 };
        const p = (b.plek || {})[gm0]; if (Array.isArray(p) && !tab.plek[gm] && p[0] > 50.6 && p[0] < 53.7 && p[1] > 3 && p[1] < 7.4) tab.plek[gm] = [+(+p[0]).toFixed(3), +(+p[1]).toFixed(3)]; }
      await this.state.storage.put(sleutel, tab);
    }
    return new Response(JSON.stringify({ maand, ...tab }));
  }
  // ---- ghosts: ritten van de groep (30 dagen bewaard) ----
  async spoorBewaar(request) {
    let b; try { b = await request.json(); } catch (e) { return new Response('{"error":"geen json"}', { status: 400 }); }
    const van = schoon(b.van, 24).replace(/[^a-zA-Z0-9]/g, ''), n = schoon(b.n, 20), t0 = Math.round(+b.t0 || 0);
    if (!van || !t0 || !Array.isArray(b.p) || b.p.length < 20 || b.p.length > 4000) return new Response('{"error":"spoor klopt niet"}', { status: 400 });
    const p = b.p.filter((q) => Array.isArray(q) && q[0] > 50.6 && q[0] < 53.7 && q[1] > 3 && q[1] < 7.4 && isFinite(q[2])).map((q) => [+(+q[0]).toFixed(5), +(+q[1]).toFixed(5), Math.round(+q[2])]);
    let la0 = 90, la1 = 0, lo0 = 180, lo1 = 0; for (const q of p) { la0 = Math.min(la0, q[0]); la1 = Math.max(la1, q[0]); lo0 = Math.min(lo0, q[1]); lo1 = Math.max(lo1, q[1]); }
    await this.state.storage.put('s:' + van + ':' + t0, { van, n, t0, bb: [la0, lo0, la1, lo1], p });
    // opruimen: ouder dan 30 dagen weg, en per persoon hooguit 40 ritten
    const alle = await this.state.storage.list({ prefix: 's:' }), per = {}, weg = [];
    for (const [k, v] of alle) { if (!v || Date.now() - v.t0 > 30 * 864e5) { weg.push(k); continue; } (per[v.van] = per[v.van] || []).push([k, v.t0]); }
    for (const l of Object.values(per)) if (l.length > 40) l.sort((a, c) => a[1] - c[1]).slice(0, l.length - 40).forEach((x) => weg.push(x[0]));
    if (weg.length) await this.state.storage.delete(weg);
    return new Response('{"ok":true}');
  }
  async sporen(url) {
    const lat = +url.searchParams.get('lat'), lon = +url.searchParams.get('lon'), van = (url.searchParams.get('van') || '').replace(/[^a-zA-Z0-9]/g, '');
    const m = 0.15, uit = [];   // ~15 km rond je heen
    for (const [, v] of await this.state.storage.list({ prefix: 's:' })) { if (!v || v.van === van || Date.now() - v.t0 > 30 * 864e5) continue;
      if (lat < v.bb[0] - m || lat > v.bb[2] + m || lon < v.bb[1] - m * 1.6 || lon > v.bb[3] + m * 1.6) continue; uit.push(v); }
    uit.sort((a, b) => b.t0 - a.t0);
    return new Response(JSON.stringify({ sporen: uit.slice(0, 12) }));
  }

  weg(ws, van) { const uit = JSON.stringify({ t: 'weg', van }); for (const ander of this.state.getWebSockets()) if (ander !== ws) try { ander.send(uit); } catch (e) {} }
  async webSocketClose(ws) { const a = ws.deserializeAttachment(); if (a) this.weg(ws, a.van); }
  async webSocketError(ws) { const a = ws.deserializeAttachment(); if (a) this.weg(ws, a.van); }
}
