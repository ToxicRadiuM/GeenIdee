#!/usr/bin/env python3
"""Buddy: vaste flitsers voor heel Nederland in één klein bestand.

Bron: OpenStreetMap (© OpenStreetMap-bijdragers, ODbL). Twee manieren:
  python tools/flitsers_nl.py csv  data/flitsers-nl.csv  uit/flitsers.json   (bestand van speedcams.world, ook OSM)
  python tools/flitsers_nl.py osm  -                     uit/flitsers.json   (rechtstreeks uit OpenStreetMap via Overpass)
Punten die dicht bij elkaar liggen (één paal met meerdere rijstroken) worden één flitser.
Uitvoer: {"versie": "...", "bron": "...", "c": [[lat, lon, limiet of 0, soort 0=snelheid 1=rood licht], ...]}
"""
import csv, json, math, sys, time, urllib.request, urllib.parse

SAMEN_M = 45


def afstand(a, b):
    k = math.cos(math.radians((a[0] + b[0]) / 2))
    return math.hypot((a[0] - b[0]) * 111195, (a[1] - b[1]) * 111195 * k)


def lees_csv(pad):
    out = []
    with open(pad, encoding='utf-8') as f:
        rijen = [r for r in f if not r.startswith('#')]
    for r in csv.DictReader(rijen):
        try:
            la, lo = float(r['latitude']), float(r['longitude'])
        except (KeyError, ValueError):
            continue
        lim = r.get('maxspeed') or ''
        lim = int(lim) if lim.isdigit() and (r.get('unit') or 'kmh') == 'kmh' else 0
        out.append((la, lo, lim, 1 if r.get('type') == 'red_light' else 0))
    return out


def lees_osm():
    q = ('[out:json][timeout:180];area["ISO3166-1"="NL"][admin_level=2]->.nl;'
         'node["highway"="speed_camera"](area.nl);out body;')
    for srv in ('https://overpass-api.de/api/interpreter', 'https://overpass.kumi.systems/api/interpreter', 'https://overpass.private.coffee/api/interpreter'):
        try:
            req = urllib.request.Request(srv, data=urllib.parse.urlencode({'data': q}).encode(), headers={'User-Agent': 'Buddy-dashboardrobot/1.0'})
            j = json.load(urllib.request.urlopen(req, timeout=240))
            out = []
            for e in j.get('elements', []):
                tg = e.get('tags', {})
                ms = (tg.get('maxspeed') or '').split()[0] if tg.get('maxspeed') else ''
                rl = 1 if 'red' in (tg.get('enforcement') or tg.get('camera:type') or tg.get('speed_camera') or '') else 0
                out.append((e['lat'], e['lon'], int(ms) if ms.isdigit() else 0, rl))
            if len(out) > 300:
                return out
        except Exception as ex:
            print('overpass', srv, ex, file=sys.stderr)
    raise SystemExit('OpenStreetMap niet bereikbaar')


def samenvoegen(pt):
    pt = sorted(pt)
    groepen = []
    for p in pt:
        for g in groepen:
            if g[4] == p[3] and afstand((g[0] / g[3], g[1] / g[3]), p) < SAMEN_M:
                g[0] += p[0]; g[1] += p[1]; g[3] += 1
                if p[2]: g[2].append(p[2])
                break
        else:
            groepen.append([p[0], p[1], [p[2]] if p[2] else [], 1, p[3]])
    uit = []
    for g in groepen:
        lim = max(set(g[2]), key=g[2].count) if g[2] else 0
        uit.append([round(g[0] / g[3], 5), round(g[1] / g[3], 5), lim, g[4]])
    return uit


if __name__ == '__main__':
    soort, bron, doel = sys.argv[1], sys.argv[2], sys.argv[3]
    pt = lees_csv(bron) if soort == 'csv' else lees_osm()
    c = samenvoegen(pt)
    json.dump({'versie': time.strftime('%d-%m-%Y'), 'bron': '© OpenStreetMap-bijdragers (ODbL)' + (' via speedcams.world' if soort == 'csv' else ''),
               'n': len(c), 'c': c}, open(doel, 'w'), separators=(',', ':'))
    print(f'{len(pt)} punten -> {len(c)} flitsers in {doel}')
