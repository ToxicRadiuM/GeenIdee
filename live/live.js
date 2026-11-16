// Buddy live: iedereen uit een groep die rijdt, stuurt elke seconde zijn plek. De groep (een Durable Object)
// stuurt dat alleen door naar groepsleden die binnen 5 km rijden. Niets wordt bewaard: valt de verbinding weg, dan ben je weg.
const TOEGESTAAN = 'https://toxicradium.github.io';
const BEREIK = 5000;   // meter

export default {
  async fetch(request, env) {
    const url = new URL(request.url), origin = request.headers.get('Origin') || '';
    if (origin && origin !== TOEGESTAAN) return new Response('Niet toegestaan', { status: 403 });
    if (url.pathname === '/') return new Response('Buddy live draait', { headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
    if (url.pathname !== '/live') return new Response('Niet gevonden', { status: 404 });
    if (request.headers.get('Upgrade') !== 'websocket') return new Response('Verwacht een websocket', { status: 426 });
    const g = (url.searchParams.get('g') || '').toLowerCase();
    if (!/^[a-z0-9]{6,32}$/.test(g)) return new Response('Groepscode klopt niet', { status: 400 });
    return env.GROEP.get(env.GROEP.idFromName(g)).fetch(request);
  },
};

const schoon = (v, n) => String(v || '').replace(/[^A-Za-z0-9À-ſ '\-]/g, '').trim().slice(0, n);
const afst = (a, b) => Math.hypot((a.lat - b.lat) * 111195, (a.lon - b.lon) * 111195 * Math.cos(a.lat * Math.PI / 180));

export class BuddyGroep {
  constructor(state) { this.state = state; }

  async fetch(request) {
    const url = new URL(request.url);
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

  weg(ws, van) { const uit = JSON.stringify({ t: 'weg', van }); for (const ander of this.state.getWebSockets()) if (ander !== ws) try { ander.send(uit); } catch (e) {} }
  async webSocketClose(ws) { const a = ws.deserializeAttachment(); if (a) this.weg(ws, a.van); }
  async webSocketError(ws) { const a = ws.deserializeAttachment(); if (a) this.weg(ws, a.van); }
}
