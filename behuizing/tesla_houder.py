# Remix van "Phone holder on screen Tesla" (Printables/MakerWorld, modulair): de schermklem, het draaiarmpje en de
# grote kartelmoer blijven; de telefoonhouder + asje worden vervangen door een Buddy-bekertje met onze klemring 'strak',
# een boutje met zeskantkop (zit binnen in het bekertje) en een eigen kartelmoer. Alles zonder support te printen.
import math, numpy as np, trimesh
from manifold3d import Manifold, CrossSection, set_circular_segments
set_circular_segments(160)
def cyl(h, r, r2=None, center=False): return Manifold.cylinder(h, r, r if r2 is None else r2, 0, center)
def hexa(h, af): return Manifold.cylinder(h, af / math.sqrt(3), af / math.sqrt(3), 6)   # af = sleutelwijdte
def draad(lengte, r, ecc=0.55, spoed=3.5):   # draad als 'gedraaide excentrische cirkel': makkelijk te printen, past altijd op elkaar
    cs = CrossSection.circle(r, 96).translate([ecc, 0])
    return Manifold.extrude(cs, lengte, int(lengte / spoed * 96), 360.0 * lengte / spoed)
def export(m, naam):
    me = m.to_mesh(); tm = trimesh.Trimesh(np.array(me.vert_properties)[:, :3], np.array(me.tri_verts), process=False)
    tm.export(f'/home/claude/GeenIdee/behuizing/{naam}.stl')
    nn = tm.face_normals; c = tm.triangles_center; bad = (nn[:, 2] < -0.72) & (c[:, 2] > 0.3)
    print(naam, 'watertight', tm.is_watertight, 'maat', np.round(tm.extents, 1), 'delen', len(m.decompose()), 'overhang mm2', round(float(tm.area_faces[bad].sum()), 1))
R_BODY = 27.23; R_SCH = 9.15     # draad: kern r 9.15 + 0.55 = 9.7 (past in het gat van 20 mm in het armpje)
KOP_AF = 24.0; KOP_H = 3.5; BODEM = 6.0
# 1. bekertje
def beker(speling, naam):
    R_IN = R_BODY - 0.05 + speling; R_UIT = R_IN + 2.6; W = 10.0
    z_naad = BODEM + KOP_H * 0 + 1.0 + 17.1          # Buddy rust op de boutkop (die zit verzonken in de bodem)
    zk = z_naad                                       # midden van de kraag
    b = cyl(zk + W / 2, R_UIT) - cyl(200, R_IN).translate([0, 0, BODEM])
    b -= cyl(1.2, R_IN + 1.0, R_IN, center=True).translate([0, 0, zk + W / 2 - 0.59])          # inloopschuinte boven
    hs = (zk - W / 2) - 1.5 - 7.0 - (BODEM + 1.0)     # sleuf voor de USB-kabel met puntdak
    b -= Manifold.cube([14.0, 20.0, hs], False).translate([-7.0, R_IN - 5, BODEM + 1.0])
    b -= Manifold.cube([9.9, 20.0, 9.9], True).rotate([0, 45, 0]).translate([0, R_IN + 5, BODEM + 1.0 + hs])
    b -= hexa(KOP_H + 0.01, KOP_AF + 0.5).translate([0, 0, BODEM - KOP_H])                     # zeskant-verzinking voor de boutkop
    b -= cyl(BODEM + 2, 10.15).translate([0, 0, -1])                                           # gat voor de bout
    for i in range(6):                                                                         # geluidsgaatjes rond de bout
        a = math.radians(30 + i * 60); b -= cyl(BODEM + 2, 3.2).translate([math.cos(a) * 18, math.sin(a) * 18, -1])
    export(b, naam)
beker(0.0, 'tesla_buddy_beker_strak'); beker(0.1, 'tesla_buddy_beker_normaal')
# 2. boutje: kop onder (op het bed), draad omhoog
bout = hexa(KOP_H, KOP_AF) + draad(22.0, R_SCH).translate([0, 0, KOP_H - 0.01])
bout += cyl(1.0, R_SCH + 0.55, R_SCH - 0.25).translate([0, 0, KOP_H + 22.0 - 1.0]) ^ cyl(30, 30)   # afgeschuinde punt
export(bout, 'tesla_buddy_bout')
# 3. kartelmoer
moer = cyl(9.0, 16.0)
for i in range(24): moer -= cyl(9.2, 1.4).translate([math.cos(i * math.pi / 12) * 16.6, math.sin(i * math.pi / 12) * 16.6, -0.1])
moer -= draad(9.4, R_SCH + 0.35).translate([0, 0, -0.2])
moer -= cyl(1.0, R_SCH + 1.3, R_SCH + 0.3).translate([0, 0, -0.01])                                 # inloopschuinte
export(moer, 'tesla_buddy_moer')
