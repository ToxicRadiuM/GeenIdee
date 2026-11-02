# 'dash redukcja' (TESLA Y Juniper Enhance dash holder) zonder oortjes: dezelfde buis (gat 39,2 mm, 20 mm diep)
# met daarop ons bekertje met de klemring 'strak'. In één stuk, buis op het bed, zonder support (bodem is een brug van 39 mm).
import math, numpy as np, trimesh
from manifold3d import Manifold, Mesh, set_circular_segments
set_circular_segments(160)
exec(open('tesla_houder.py').read().split('R_BODY =')[0].split('set_circular_segments(160)')[1])
R_BODY = 27.23; BUIS_H = 20.0; R_GAT = 19.61; R_BUIS = 27.75
def maak(speling, naam):
    R_IN = R_BODY - 0.05 + speling; R_UIT = R_IN + 2.6; W = 10.0; BODEM = 3.0
    buis = cyl(BUIS_H, R_BUIS) - cyl(BUIS_H + 1, R_GAT).translate([0, 0, -0.5])
    buis = buis - (cyl(1.2, R_BUIS + 1, R_BUIS - 0.2) ^ cyl(1.2, 40)).translate([0, 0, -0.01]) + (cyl(1.2, R_BUIS - 0.9, R_BUIS) - cyl(2, R_GAT)).translate([0, 0, -0.01])  # schuin randje onder (tegen olifantenvoet)
    kraag = cyl(2.1, R_BUIS, R_UIT).translate([0, 0, BUIS_H])                                  # 45 graden overgang naar de bredere beker
    z0 = BUIS_H; zk = z0 + BODEM + 1.0 + 17.1                                                  # zelfde hoogte als in het gewone bekertje
    b = cyl(zk + W / 2 - z0, R_UIT).translate([0, 0, z0]) - cyl(200, R_IN).translate([0, 0, z0 + BODEM])
    b -= cyl(1.2, R_IN + 1.0, R_IN, center=True).translate([0, 0, zk + W / 2 - 0.59])
    hs = (zk - W / 2) - 1.5 - 7.0 - (z0 + BODEM + 1.0)
    b -= Manifold.cube([14.0, 20.0, hs], False).translate([-7.0, R_IN - 5, z0 + BODEM + 1.0])
    b -= Manifold.cube([9.9, 20.0, 9.9], True).rotate([0, 45, 0]).translate([0, R_IN + 5, z0 + BODEM + 1.0 + hs])
    export(buis + kraag + b, naam)
maak(0.0, 'dash_ring_beker_strak'); maak(0.1, 'dash_ring_beker_normaal')
