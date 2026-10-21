#!/usr/bin/env python3
"""
Buddy: maximumsnelheden van Rijkswaterstaat (Wegkenmerkendatabase, "Maximum snelheden")
omzetten naar kleine tegels die Buddy onderweg kan ophalen.

Gebruik (dit draait automatisch op GitHub, zie .github/workflows/snelheden.yml):
    python tools/wkd_tiles.py download Snelheden.gpkg          # nieuwste maand ophalen
    python tools/wkd_tiles.py tegels Snelheden.gpkg uit/        # tegels maken

Bron: https://downloads.rijkswaterstaatdata.nl/wkd/geogegevens/Geopackage/Maximum%20Snelheden/
Velden (volgens NDW): WVK_ID, WEGNUMMER, MAXSHD, BEGINTIJD, EINDTIJD, MAXSHD_ALT, VAN, NAAR, KENM_RICHT, LENGTE ...

Tegel = vak van 0,03 graad noord-zuid x 0,05 graad oost-west (dezelfde maat als de OSM-vakken in Buddy),
met 600-900 m overlap zodat je bij de rand niet "leeg" rijdt.
Uitvoer per tegel: uit/t/<ty>_<tx>.json  =  {"v": versie, "w": [[lim, 0, [lat,lon,lat,lon...], [v,van,tot]|null, 0, richting, wegnummer], ...]}
  lim       = limiet buiten het tijdvak (of de gewone limiet)
  [v,a,b]   = tijdafhankelijk: tussen minuut a en b van de dag geldt v (bijv. 100 van 06:00 tot 19:00)
  richting  = 1 alleen in de tekenrichting van de lijn, -1 alleen tegen de tekenrichting in, 0 beide kanten
"""
import json, math, os, re, sqlite3, struct, sys, time, urllib.request
from collections import defaultdict

BASIS = 'https://downloads.rijkswaterstaatdata.nl/wkd/geogegevens/Geopackage/Maximum%20Snelheden/'
TILE_LAT, TILE_LON, PAD_LAT, PAD_LON = 0.03, 0.05, 0.006, 0.009
GELDIG = {5, 15, 20, 30, 40, 50, 60, 70, 80, 90, 100, 120, 130}


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


# ---------------------------------------------------------------- download
def nieuwste_map():
    html = urllib.request.urlopen(BASIS, timeout=60).read().decode('utf-8', 'replace')
    datums = set(re.findall(r'href="(\d{2})-(\d{2})-(\d{4})/"', html))
    if not datums:
        raise SystemExit('Geen maandmappen gevonden op ' + BASIS)
    d, m, j = max(datums, key=lambda x: (x[2], x[1], x[0]))
    return f'{d}-{m}-{j}'


def download(doel):
    map_ = nieuwste_map()
    url = BASIS + map_ + '/Snelheden.gpkg'
    log('downloaden', url)
    with urllib.request.urlopen(url, timeout=120) as r, open(doel + '.deel', 'wb') as f:
        n = 0
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            f.write(b); n += len(b)
            if n % (100 << 20) < (1 << 20):
                log(f'  {n >> 20} MB')
    os.replace(doel + '.deel', doel)
    with open(doel + '.versie', 'w') as f:
        f.write(map_)
    log('klaar', os.path.getsize(doel) >> 20, 'MB, versie', map_)


# ---------------------------------------------------------------- GeoPackage lezen
def gpkg_geom(blob):
    """GeoPackage-geometrie -> shapely. Kop: 'GP', versie, vlaggen, srs_id, envelop, daarna WKB."""
    from shapely import wkb
    if not blob or blob[:2] != b'GP':
        return None
    flags = blob[3]
    env = (flags >> 1) & 7
    off = 8 + {0: 0, 1: 32, 2: 48, 3: 48, 4: 64}.get(env, 0)
    if flags & 0x10:  # leeg
        return None
    return wkb.loads(bytes(blob[off:]))


def zoek_laag(db):
    """De tabel met een MAXSHD-kolom, plus de naam van de geometriekolom en de EPSG-code."""
    lagen = [r[0] for r in db.execute("SELECT table_name FROM gpkg_contents WHERE data_type='features'")]
    # Liefst de laag met wegvakdelen (VAN/NAAR), dan geldt de limiet precies voor het goede stuk weg
    lagen.sort(key=lambda t: 0 if 'deel' in t.lower() else 1)
    for t in lagen:
        kol = [r[1] for r in db.execute(f'PRAGMA table_info("{t}")')]
        if any(k.upper() == 'MAXSHD' for k in kol):
            g = db.execute('SELECT column_name, srs_id FROM gpkg_geometry_columns WHERE table_name=?', (t,)).fetchone()
            srs = g[1]
            org = db.execute('SELECT organization, organization_coordsys_id FROM gpkg_spatial_ref_sys WHERE srs_id=?', (srs,)).fetchone()
            epsg = org[1] if org and str(org[0]).upper() == 'EPSG' else srs
            return t, kol, g[0], int(epsg)
    raise SystemExit('Geen laag met MAXSHD gevonden. Lagen: ' + ', '.join(lagen))


def getal(v):
    if v is None:
        return None
    s = str(v).strip().replace(',', '.')
    try:
        return float(s)
    except ValueError:
        return None


def snelheid(v):
    n = getal(v)
    if n is None:
        return None
    n = int(round(n))
    return n if n in GELDIG else None


def minuten(v):
    """'06:00', '6:00', '0600', '600', 600, '06:00:00' -> minuten van de dag, anders None"""
    if v is None:
        return None
    s = str(v).strip()
    if not s:
        return None
    m = re.match(r'^(\d{1,2})[:.](\d{2})', s)
    if m:
        return (int(m.group(1)) % 24) * 60 + int(m.group(2))
    d = re.sub(r'\D', '', s)
    if len(d) in (3, 4):
        return (int(d[:-2]) % 24) * 60 + int(d[-2:])
    if len(d) in (1, 2):
        return (int(d) % 24) * 60
    return None


def richting(v):
    s = str(v or '').strip().upper()
    if s in ('H', 'HEEN', 'M', 'MEE'):
        return 1
    if s in ('T', 'TERUG', 'TEGEN'):
        return -1
    return 0


def wegnr(v):
    s = str(v or '').strip().upper()
    d = re.sub(r'\D', '', s)
    return str(int(d)) if d else ''


# ---------------------------------------------------------------- tegels maken
def tegels(bron, uit):
    from shapely.ops import substring
    from pyproj import Transformer

    db = sqlite3.connect(bron)
    laag, kol, gkol, epsg = zoek_laag(db)
    log('laag', laag, 'geometrie', gkol, 'EPSG', epsg)
    log('kolommen', ', '.join(kol))
    K = {k.upper(): k for k in kol}
    # Diagnose: hoe zien de tijd- en limietvelden er echt uit? (komt ook in meta.json)
    diag = {'laag': laag, 'kolommen': kol}
    for veld in ('MAXSHD', 'MAXSHD_ALT', 'BEGINTIJD', 'EINDTIJD', 'KENM_RICHT', 'VAN', 'NAAR'):
        if veld in K:
            rijen = db.execute(f'SELECT "{K[veld]}", COUNT(*) FROM "{laag}" GROUP BY 1 ORDER BY 2 DESC LIMIT 12').fetchall()
            diag[veld] = [[str(a), b] for a, b in rijen]
            log(veld, diag[veld])

    def k(n):
        return f'"{K[n]}"' if n in K else 'NULL'

    meters = epsg != 4326
    naar_wgs = Transformer.from_crs(epsg, 4326, always_xy=True).transform if meters else None
    sql = (f'SELECT {k("MAXSHD")}, {k("MAXSHD_ALT")}, {k("BEGINTIJD")}, {k("EINDTIJD")}, {k("VAN")}, {k("NAAR")}, '
           f'{k("KENM_RICHT")}, {k("WEGNUMMER")}, {k("LENGTE")}, "{gkol}" FROM "{laag}"')
    vakken = defaultdict(list)
    n = goed = tijd_n = 0
    for rij in db.execute(sql):
        n += 1
        if n % 200000 == 0:
            log(f'  {n} rijen, {goed} met limiet')
        maxshd, alt, bt, et, van, naar, kr, wnr, lengte, blob = rij
        lim = snelheid(maxshd)
        if lim is None:
            continue
        try:
            g = gpkg_geom(blob)
        except Exception:
            continue
        if g is None or g.is_empty:
            continue
        lijnen = list(g.geoms) if g.geom_type.startswith('Multi') else [g]
        # Geldt de limiet maar voor een deel van het wegvak (VAN..NAAR, in meters)? Dan dat stukje uitknippen.
        v0, v1, L = getal(van), getal(naar), getal(lengte)
        delen = []
        for ln in lijnen:
            if ln.geom_type != 'LineString' or ln.length == 0:
                continue
            if meters and v0 is not None and v1 is not None and L and L > 0 and not (v0 <= 0.5 and v1 >= L - 0.5):
                a, b = sorted((max(0.0, v0), min(L, v1)))
                f = ln.length / L
                try:
                    ln = substring(ln, a * f, b * f)
                except Exception:
                    pass
                if ln.is_empty or ln.geom_type != 'LineString':
                    continue
            delen.append(ln.simplify(2.0 if meters else 0.00002))
        if not delen:
            continue
        a_lim = snelheid(alt)
        tb, te = minuten(bt), minuten(et)
        c = [lim, tb, te] if (a_lim and tb is not None and te is not None and tb != te) else None
        if c: tijd_n += 1
        basis = a_lim if c else lim
        rr, wn = richting(kr), wegnr(wnr)
        for ln in delen:
            xs = [c[0] for c in ln.coords]; ys = [c[1] for c in ln.coords]
            if meters:
                xs, ys = naar_wgs(xs, ys)
            pts = []
            for x, y in zip(xs, ys):
                pts += [round(y, 5), round(x, 5)]
            if len(pts) < 4:
                continue
            minx, miny, maxx, maxy = min(xs), min(ys), max(xs), max(ys)
            item = json.dumps([basis, 0, pts, c, 0, rr, wn], separators=(',', ':'))  # één keer omzetten, scheelt veel geheugen
            for ty in range(math.floor((miny - PAD_LAT) / TILE_LAT), math.floor((maxy + PAD_LAT) / TILE_LAT) + 1):
                for tx in range(math.floor((minx - PAD_LON) / TILE_LON), math.floor((maxx + PAD_LON) / TILE_LON) + 1):
                    # alleen als de lijn echt in (de rand van) dit vak komt
                    s, w = ty * TILE_LAT - PAD_LAT, tx * TILE_LON - PAD_LON
                    if maxy < s or miny > s + TILE_LAT + 2 * PAD_LAT or maxx < w or minx > w + TILE_LON + 2 * PAD_LON:
                        continue
                    vakken[(ty, tx)].append(item)
        goed += 1
    log(f'{n} rijen gelezen, {goed} met limiet, {len(vakken)} tegels')

    versie = ''
    if os.path.exists(bron + '.versie'):
        versie = open(bron + '.versie').read().strip()
    os.makedirs(os.path.join(uit, 't'), exist_ok=True)
    totaal = 0
    for (ty, tx), items in vakken.items():
        tekst = '{"v":' + json.dumps(versie) + ',"w":[' + ','.join(items) + ']}'
        with open(os.path.join(uit, 't', f'{ty}_{tx}.json'), 'w') as f:
            f.write(tekst)
        totaal += len(tekst)
    meta = {'diagnose': diag, 'tijdafhankelijk': tijd_n, 'versie': versie, 'tegels': len(vakken), 'wegvakken': goed, 'gemaakt': time.strftime('%Y-%m-%d %H:%M'),
            'vak': {'dLat': TILE_LAT, 'dLon': TILE_LON, 'padLat': PAD_LAT, 'padLon': PAD_LON},
            'bron': 'Rijkswaterstaat, Wegkenmerkendatabase: Maximum snelheden (' + BASIS + ')'}
    with open(os.path.join(uit, 'meta.json'), 'w') as f:
        json.dump(meta, f, indent=1)
    with open(os.path.join(uit, 'README.md'), 'w') as f:
        f.write('# Buddy: maximumsnelheden\n\nAutomatisch gemaakt uit de Wegkenmerkendatabase van Rijkswaterstaat. '
                'Niet met de hand aanpassen: deze branch wordt elke maand helemaal vervangen.\n')
    log(f'klaar: {len(vakken)} tegels, {totaal >> 20} MB')


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'download':
        download(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == 'tegels':
        tegels(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(1)
