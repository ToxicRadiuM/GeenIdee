# Voetje v2: klemt om de KLEMRING heen (die zit om de naad). Kanaal met randjes: schuift niet naar voren/achteren,
# en de wangen lopen 12 graden voorbij het midden van de ring -> klikt vast. Print zonder support, plat op het bed.
import math, numpy as np, trimesh
from manifold3d import Manifold, set_circular_segments
set_circular_segments(160)
def cyl(h, r, r2=None, center=False): return Manifold.cylinder(h, r, r if r2 is None else r2, 0, center)
def box(x, y, z): return Manifold.cube([x, y, z], True)
R_BODY = 27.23                      # buitenkant behuizing
R_RING = R_BODY + 0.05 + 1.6        # buitenkant klemring (normaal)
KANTEL = 12.0; t_ = math.radians(KANTEL)
POD_L = 32.4; RING_MID = 21.9       # ring zit ~2 mm achter de naad (z 17,4 .. 26,4 vanaf de voorkant)
YC = -POD_L / 2 + RING_MID          # midden van de ring langs de as (pod gecentreerd op het zadel)
R_KL = R_RING + 0.15; WAND = 2.8; GRAAD = 12.0; KAN_B = 9.0 + 0.4; LIP = 1.6
pod_r = R_BODY + 0.25
H = 3.0 + 15 * math.tan(t_) + pod_r / math.cos(t_)
def plaats(m): return m.rotate([-KANTEL, 0, 0]).translate([0, 0, H])
basis = Manifold.hull(Manifold.batch_hull([cyl(1, 7).translate([sx * 18, sy * 15, 0]) for sx in (-1, 1) for sy in (-1, 1)]) +
                      Manifold.batch_hull([cyl(1, 5).translate([sx * 15, sy * 11, 9]) for sx in (-1, 1) for sy in (-1, 1)]))
zadel = basis + Manifold.hull(box(50, 30, 1).translate([0, 0, 0.5]) + box(44, 26, 1).translate([0, 0, 15]))
zadel -= plaats(cyl(POD_L + 40, pod_r, center=True).rotate([90, 0, 0]))
kz = R_KL * math.sin(math.radians(GRAAD))
breed = KAN_B + 2 * LIP
klem = box(2 * (R_KL + WAND), breed, R_KL + kz + 40).translate([0, YC, kz - (R_KL + kz + 40) / 2])
klem -= cyl(breed + 2, R_BODY + 0.3, center=True).rotate([90, 0, 0]).translate([0, YC, 0])          # randjes: om de behuizing
klem -= cyl(KAN_B, R_KL, center=True).rotate([90, 0, 0]).translate([0, YC, 0])                     # kanaal: om de ring
for sx in (-1, 1):   # schuin inloopkantje bovenaan, dan drukt hij er makkelijk in
    klem -= box(3.2, breed + 2, 3.2).rotate([0, 45, 0]).translate([sx * (R_KL * math.cos(math.radians(GRAAD)) + 0.4), YC, kz])
zadel = zadel - plaats(cyl(KAN_B, R_KL, center=True).rotate([90, 0, 0]).translate([0, YC, 0])) + plaats(klem)
zadel = zadel ^ box(90, 90, 90).translate([0, 0, 45])
me = zadel.to_mesh(); t = trimesh.Trimesh(np.array(me.vert_properties)[:, :3], np.array(me.tri_verts), process=False)
t.export('/home/claude/GeenIdee/behuizing/buddy_4_voetje_klemring.stl')
nn = t.face_normals; c = t.triangles_center; bad = (nn[:, 2] < -0.72) & (c[:, 2] > 0.3)
print('watertight', t.is_watertight, 'maat', np.round(t.extents, 1), 'delen', len(zadel.decompose()), 'overhang mm2', round(t.area_faces[bad].sum(), 1))
