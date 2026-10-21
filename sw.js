/* Buddy: bewaart de app op je telefoon zodat hij ook zonder internet opstart.
   - Eigen bestanden: eerst internet (zodat updates meteen binnenkomen), lukt dat niet binnen 4 s dan de bewaarde versie.
   - Lettertypen van Google: uit de bewaarde versie, op de achtergrond verversen.
   - Al het andere (je Worker, PDOK, OpenStreetMap, weer…) gaat gewoon via internet en wordt hier niet bewaard. */
const KAST = 'buddy-v5';
const KERN = ['./', './index.html', './manifest.webmanifest', './icon-192.png', './icon-512.png', './icon-maskable-512.png', './msi-locaties.json'];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(KAST).then((c) => c.addAll(KERN)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys().then((ks) => Promise.all(ks.filter((k) => k !== KAST).map((k) => caches.delete(k)))).then(() => self.clients.claim()));
});

function metTijdslimiet(p, ms) {
  return new Promise((ok, nee) => { const t = setTimeout(() => nee(new Error('te traag')), ms); p.then((r) => { clearTimeout(t); ok(r); }, (e) => { clearTimeout(t); nee(e); }); });
}

self.addEventListener('fetch', (e) => {
  const r = e.request;
  if (r.method !== 'GET') return;
  const u = new URL(r.url);
  if (u.origin === self.location.origin) {
    e.respondWith(
      metTijdslimiet(fetch(r), 4000)
        .then((res) => { if (res && res.ok) { const kopie = res.clone(); caches.open(KAST).then((c) => c.put(r, kopie)); } return res; })
        .catch(() => caches.match(r, { ignoreSearch: true }).then((m) => m || (r.mode === 'navigate' ? caches.match('./index.html') : Response.error())))
    );
    return;
  }
  if (u.hostname === 'fonts.googleapis.com' || u.hostname === 'fonts.gstatic.com') {
    e.respondWith(caches.match(r).then((m) => {
      const net = fetch(r).then((res) => { if (res && (res.ok || res.type === 'opaque')) { const kopie = res.clone(); caches.open(KAST).then((c) => c.put(r, kopie)); } return res; }).catch(() => m);
      return m || net;
    }));
  }
});
