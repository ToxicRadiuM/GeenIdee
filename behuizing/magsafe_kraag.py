# Buddy+ in de auto: kraag (zelfde maat als de klemring 'strak') + een schijf achter de Buddy met een plekje
# voor een standaard MagSafe-metaalring (56/46 mm). Zo klikt hij op een MagSafe-telefoonhouder (bv. Spigen voor de Tesla).
# Print: schijf plat op het bed (de kant die tegen de magneet komt), armen en kraag staan rechtop. Geen support.
import math, numpy as np, trimesh
from manifold3d import Manifold, set_circular_segments
set_circular_segments(180)
def cyl(h, r, r2=None, center=False): return Manifold.cylinder(h, r, r if r2 is None else r2, 0, center)
R_BODY = 27.23
def maak(speling, naam):
    R_IN = R_BODY - 0.05 + speling; R_UIT = R_IN + 2.6; W = 10.0
    ACHTER_NAAD = 17.1      # zo ver steekt de achterkant achter de naad uit
    SCHIJF = 3.0; GAT = 0.8 # schijf en diepte van het ringplekje
    z_schijf_top = SCHIJF
    z_kraag_mid = SCHIJF + 1.0 + ACHTER_NAAD    # naad ligt hier (1 mm lucht achter de Buddy)
    schijf = cyl(SCHIJF, 30.0) - cyl(SCHIJF + 2, 15.0).translate([0, 0, -1])          # open midden (kabel/lucht)
    schijf -= (cyl(GAT + 0.01, 28.4) - cyl(GAT + 1, 22.6).translate([0, 0, -0.5])).translate([0, 0, -0.01])   # plekje voor de ring (56/46 + speling)
    kraag = (cyl(W, R_UIT) - cyl(W + 2, R_IN).translate([0, 0, -1])).translate([0, 0, z_kraag_mid - W / 2])
    for sg in (1,):   # inloopschuinte (alleen boven: daar schuift de Buddy erin)
        z = z_kraag_mid + sg * (W / 2 - 0.59)
        kraag -= cyl(1.2, R_IN + 1.0 if sg > 0 else R_IN, R_IN if sg > 0 else R_IN + 1.0, center=True).translate([0, 0, z])
    # dichte wand tussen schijf en kraag (geen overhang), met een sleuf voor de USB-kabel en het geluid
    armen = cyl(z_kraag_mid - W / 2 + 0.5, R_UIT) - cyl(200, R_IN).translate([0, 0, -1])
    hs = (z_kraag_mid - W / 2) - 1.5 - 7.0 - (SCHIJF + 1.0)   # sleuf met een puntdak (45 graden), dan hoeft er niets te overbruggen
    armen -= Manifold.cube([14.0, 20.0, hs], False).translate([-7.0, R_IN - 5, SCHIJF + 1.0])
    armen -= Manifold.cube([9.9, 20.0, 9.9], True).rotate([0, 45, 0]).translate([0, R_IN + 5, SCHIJF + 1.0 + hs])
    deel = schijf + kraag + armen
    deel -= cyl(200, R_IN).translate([0, 0, SCHIJF + 0.01])   # niets binnen de Buddy
    me = deel.to_mesh(); tm = trimesh.Trimesh(np.array(me.vert_properties)[:, :3], np.array(me.tri_verts), process=False)
    tm.export(f'/home/claude/GeenIdee/behuizing/{naam}.stl')
    nn = tm.face_normals; c = tm.triangles_center; bad = (nn[:, 2] < -0.72) & (c[:, 2] > 0.3)
    print(naam, 'watertight', tm.is_watertight, 'maat', np.round(tm.extents, 1), 'delen', len(deel.decompose()), 'overhang mm2', round(float(tm.area_faces[bad].sum()), 1))
maak(0.0, 'buddy_magsafe_strak')
maak(0.1, 'buddy_magsafe_normaal')
