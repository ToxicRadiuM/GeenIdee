# Buddy: matrixborden boven de snelweg (NDW open data) omzetten naar een klein lijstje voor de app.
# Alleen borden die nu iets tonen: een snelheid, een rood kruis of "rijstrook dicht verderop".
# Uitvoer: msi.json = {"t": <tijd>, "b": [[lat, lon, koers, "A12", "R", strook, "90"|"X"|"<"|">"], ...]}
import gzip, io, json, os, sys, time, zipfile, urllib.request
import xml.etree.ElementTree as ET
import shapefile
from pyproj import Transformer

U = 'https://opendata.ndw.nu/'
uit = sys.argv[1] if len(sys.argv) > 1 else 'msi.json'
cache = sys.argv[2] if len(sys.argv) > 2 else 'msi_locaties.json'

def haal(naam, tries=3):
    for i in range(tries):
        try:
            return urllib.request.urlopen(urllib.request.Request(U + naam, headers={'User-Agent': 'Buddy-msi'}), timeout=180).read()
        except Exception as e:
            print('download', naam, 'mislukt:', e); time.sleep(5)
    raise SystemExit(1)

def ln(tag): return tag.rsplit('}', 1)[-1]

# 1. locaties (maandelijks bestand, bewaard in de cache)
loc = None
if os.path.exists(cache) and time.time() - os.path.getmtime(cache) < 7 * 86400:
    try: loc = json.load(open(cache))
    except Exception: loc = None
if not loc:
    z = zipfile.ZipFile(io.BytesIO(haal('ndw_msi_shapefiles_latest.zip')))
    namen = z.namelist(); print('in zip:', namen[:20])
    shp = [n for n in namen if n.lower().endswith('.shp')]
    loc = {'id': {}, 'pos': {}}
    for s in shp:
        b = s[:-4]
        try:
            r = shapefile.Reader(shp=io.BytesIO(z.read(b + '.shp')), dbf=io.BytesIO(z.read(b + '.dbf')), shx=io.BytesIO(z.read(b + '.shx')))
        except Exception as e:
            print('kan', s, 'niet lezen:', e); continue
        velden = [f[0] for f in r.fields[1:]]; print(s, 'velden:', velden)
        prj = (z.read(b + '.prj').decode('latin1') if (b + '.prj') in namen else '')
        rd = 'Amersfoort' in prj or 'RD_New' in prj or '28992' in prj
        tr = Transformer.from_crs('EPSG:28992', 'EPSG:4326', always_xy=True) if rd else None
        eerste = True
        for sr in r.iterShapeRecords():
            if not sr.shape.points: continue
            x, y = sr.shape.points[0]
            if tr or abs(x) > 1000: lo, la = (tr or Transformer.from_crs('EPSG:28992', 'EPSG:4326', always_xy=True)).transform(x, y)
            else: lo, la = x, y
            rec = dict(zip(velden, sr.record))
            if eerste: print('voorbeeld:', rec, la, lo); eerste = False
            p = [round(la, 5), round(lo, 5), int(float(rec.get('bearing') or -1))]
            for k, v in rec.items():   # alles wat op een uuid lijkt
                if isinstance(v, str) and len(v) == 36 and v.count('-') == 4: loc['id'][v.lower()] = p
            low = {k.lower(): v for k, v in rec.items()}
            weg = str(low.get('road') or low.get('weg') or low.get('wegnummer') or '').upper().replace(' ', '')
            cw = str(low.get('carriagew0') or low.get('carriagew') or low.get('carriageway') or low.get('rijbaan') or low.get('baan') or '').upper()
            strook = str(low.get('lane') or low.get('rijstrook') or low.get('strook') or '')
            km = low.get('km') or low.get('kmpos') or low.get('hectometer')
            if weg and km not in (None, ''):
                try: loc['pos']['%s|%s|%s|%.2f' % (weg, cw, strook, float(km))] = p
                except Exception: pass
    print('locaties:', len(loc['id']), 'met id,', len(loc['pos']), 'met weg/km')
    json.dump(loc, open(cache, 'w'))

# 2. wat de borden nu tonen
data = gzip.decompress(haal('Matrixsignaalinformatie.xml.gz'))
borden, n = [], 0
for ev, el in ET.iterparse(io.BytesIO(data), events=('end',)):
    if ln(el.tag) != 'event': continue
    n += 1; d = {}; toon = None; red = False
    if n <= 3 or (n < 4000 and n % 1000 == 0): print('voorbeeld event:', ET.tostring(el, encoding='unicode')[:900].replace('\n', ' '))
    for c in el.iter():
        t = ln(c.tag)
        if t in ('sign_id', 'road', 'carriageway', 'lane', 'km'): d[t] = (c.text or '').strip()
        elif t == 'speedlimit': toon = (c.text or '').strip()
        elif t == 'lane_closed': toon = 'X'
        elif t == 'merge_left': toon = '<'
        elif t == 'merge_right': toon = '>'
        elif t == 'lane_closed_ahead' and not toon: toon = '<'
        elif t == 'red_ring' and (c.text or '').strip().lower() in ('true', '1'): red = True
    el.clear()
    if not toon: continue
    p = loc['id'].get(d.get('sign_id', '').lower())
    if not p:
        try: p = loc['pos'].get('%s|%s|%s|%.2f' % (d.get('road', '').upper().replace(' ', ''), d.get('carriageway', '').upper(), d.get('lane', ''), float(d.get('km') or 0)))
        except Exception: p = None
    if not p: continue
    borden.append([p[0], p[1], p[2] if len(p) > 2 else -1, d.get('road', ''), d.get('carriageway', ''), d.get('lane', ''), toon + ('!' if red and toon not in 'X<>' else '')])
print('events:', n, 'borden met beeld en plek:', len(borden))
json.dump({'t': int(time.time()), 'b': borden}, open(uit, 'w'), separators=(',', ':'))
