# Smalle schermklem (idee van zip 1 'teslatelephone', maar alleen het haakje): hangt over de bovenrand van het scherm,
# Buddy-bekertje (tesla_buddy_beker_*) schroeft er bovenop met een kort boutje. Ondersteboven printen = geen support.
import math, numpy as np, trimesh
from manifold3d import Manifold, CrossSection, set_circular_segments
set_circular_segments(160)
exec(open('tesla_houder.py').read().split('R_BODY =')[0].split('set_circular_segments(160)')[1])   # cyl/hexa/draad/export
R_SCH = 9.15; KOP_AF = 24.0; KOP_H = 3.5
GAT = 16.4       # schermdikte bovenrand (zelfde als zip 1)
WAND = 3.0; BREED = 50.0; DIEP = 36.0; BRUG = 10.0
ACHTER = 21.0; VOOR = 10.0; NOK = 0.6
def klem():
    # gebruiksstand: z omhoog, brug boven, lipjes hangen omlaag; y+ = voorkant (bestuurder)
    k = Manifold.cube([BREED, DIEP, BRUG], True).translate([0, 0, BRUG / 2])
    for y0, L in ((-GAT / 2 - WAND, ACHTER), (GAT / 2, VOOR)):
        k += Manifold.cube([BREED, WAND, L + 0.01], False).translate([-BREED / 2, y0, -L])
        # nokje binnenkant onderaan het lipje, met 45 graden schuinte
        s = 1 if y0 < 0 else -1; yb = (y0 + WAND) if y0 < 0 else y0
        prof = CrossSection([[(0, -L), (s * NOK, -L + 1.0), (s * NOK, -L + 2.0), (0, -L + 2.0 + NOK)]]) if s > 0 else \
               CrossSection([[(0, -L), (0, -L + 2.0 + NOK), (s * NOK, -L + 2.0), (s * NOK, -L + 1.0)]])
        n = Manifold.extrude(prof, BREED).rotate([0, 90, 0])
        n = Manifold.extrude(prof, BREED)            # profiel in (y,z), uitrekken langs x
        n = n.rotate([90, 0, 0]).rotate([0, 0, 90]).translate([-BREED / 2, yb, 0])
        k += n
    k -= draad(BRUG + 0.4, R_SCH + 0.35).translate([0, 0, -0.2])
    k -= cyl(1.0, R_SCH + 1.3, R_SCH + 0.3).rotate([180, 0, 0]).translate([0, 0, BRUG + 0.01])   # inloop boven
    return k.rotate([180, 0, 0]).translate([0, 0, BRUG])     # ondersteboven: brug plat op het bed
export(klem(), 'scherm_klem')
bout = hexa(KOP_H, KOP_AF) + draad(12.0, R_SCH).translate([0, 0, KOP_H - 0.01])
bout += cyl(1.0, R_SCH + 0.55, R_SCH - 0.25).translate([0, 0, KOP_H + 12.0 - 1.0]) ^ cyl(30, 30)
export(bout, 'scherm_klem_bout')
