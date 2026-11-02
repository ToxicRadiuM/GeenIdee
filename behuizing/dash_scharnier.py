# Bovenstuk voor 'TESLA Y Juniper Enhance dash holder' (dash redukcja): scharniertong tussen de twee oortjes
# (tussenruimte 28 mm, as M5 op 7,5 mm boven de ring), met bovenop een plateau met draad voor ons korte boutje + bekertje.
import math, numpy as np, trimesh
from manifold3d import Manifold, CrossSection, set_circular_segments
set_circular_segments(160)
exec(open('tesla_houder.py').read().split('R_BODY =')[0].split('set_circular_segments(160)')[1])
R_SCH = 9.15
TONG = 27.4; R_AS = 7.0; GAT_AS = 5.5; NEK = 10.5; PLAT = 40.0; PLAT_H = 10.0
def tong():
    # gebruiksstand: as langs x op z=0
    t = cyl(TONG, R_AS, center=True).rotate([0, 90, 0])
    t += Manifold.cube([TONG, 2 * R_AS, NEK], False).translate([-TONG / 2, -R_AS, 0])
    t += Manifold.cube([PLAT, PLAT, PLAT_H], False).translate([-PLAT / 2, -PLAT / 2, NEK])
    t -= cyl(TONG + 2, GAT_AS / 2, center=True).rotate([0, 90, 0])
    a = GAT_AS / 2 / math.sqrt(2) * 2
    t -= Manifold.cube([TONG + 2, a, a], True).rotate([45, 0, 0]).translate([0, 0, -GAT_AS / 2 * 0.41])   # druppelvorm: punt wijst naar boven bij het printen
    t -= draad(PLAT_H + 0.4, R_SCH + 0.35).translate([0, 0, NEK - 0.2])
    t -= cyl(1.0, R_SCH + 0.3, R_SCH + 1.3).translate([0, 0, NEK + PLAT_H - 0.99])     # inloop boven
    return t.rotate([180, 0, 0]).translate([0, 0, NEK + PLAT_H])    # plateau plat op het bed
export(tong(), 'dash_scharnier')
