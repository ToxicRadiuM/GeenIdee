# Voetje v3 "kraag": de klemring zit IN het voetje. Een dichte ring (zelfde maat als de klemring 'strak', die goed werkt)
# die je over de naad schuift: houdt voor- en achterkant bij elkaar én zit vast aan een plaatje voor op het dashboard.
# Print: met de voorkant van de kraag plat op het bed, het plaatje staat dan rechtop achter de ring. Geen support.
import math, numpy as np, trimesh, sys
from manifold3d import Manifold, set_circular_segments
set_circular_segments(180)
def cyl(h, r, r2=None, center=False): return Manifold.cylinder(h, r, r if r2 is None else r2, 0, center)
def box(x, y, z): return Manifold.cube([x, y, z], True)
R_BODY = 27.23
def maak(speling, naam):
    R_IN = R_BODY - 0.05 + speling     # 'strak' klemring = R_BODY - 0.05
    R_UIT = R_IN + 2.6; W = 10.0; KANTEL = 12.0; t = math.radians(KANTEL)
    H = 2.5 + R_UIT * math.cos(t) + (W / 2) * math.sin(t)
    # as van de pod: wijst naar de bestuurder (-y) en 12 graden omhoog
    def langsAs(m): return m.rotate([90, 0, 0]).rotate([-KANTEL, 0, 0]).translate([0, 0, H])   # cilinder langs z -> langs de as
    kraag = langsAs(cyl(W, R_UIT, center=True) - cyl(W + 1, R_IN, center=True))
    # inloopschuinte aan beide kanten van de kraag
    voorvlak_y = -(W / 2) * math.cos(t)
    plaat = box(52, 37, 3).translate([0, voorvlak_y + 18.5 - 6, 1.5])
    onder = kraag ^ box(200, 200, 200).translate([0, 0, 100 - 200 + (H - 17)])   # onderste stuk van de kraag
    web = Manifold.hull(onder + box(40, 20, 3).translate([0, voorvlak_y + 10 - 8, 1.5]))
    voet = kraag + plaat + web
    voet -= langsAs(cyl(80, R_IN, center=True))      # ruimte voor de Buddy, over de hele lengte
    for sg in (-1, 1):   # inloopschuinte aan beide kanten (na de web, anders blijft er een randje hangen)
        voet -= langsAs(cyl(1.2, R_IN + 1.0 if sg < 0 else R_IN, R_IN if sg < 0 else R_IN + 1.0, center=True).translate([0, 0, sg * (W / 2 - 0.59)]))
        voet -= langsAs(cyl(30, R_IN + 1.0, center=True).translate([0, 0, sg * (W / 2 + 15)]))
    voet = voet ^ box(200, 200, 200).translate([0, 0, 100])
    # printstand: as van de pod naar beneden -> voorkant kraag op het bed
    a = np.array([0, -math.cos(t), math.sin(t)])
    m = voet.rotate([KANTEL, 0, 0])            # as nu langs -y
    m = m.rotate([90, 0, 0])                   # -y -> -z: voorkant (bestuurderskant) naar beneden
    k2 = kraag.rotate([KANTEL, 0, 0]).rotate([90, 0, 0]); z0 = k2.bounding_box()[2]   # voorvlak van de kraag = bed
    m = (m ^ box(400, 400, 400).translate([0, 0, z0 + 200])).translate([0, 0, -z0])
    me = m.to_mesh(); tm = trimesh.Trimesh(np.array(me.vert_properties)[:, :3], np.array(me.tri_verts), process=False)
    tm.export(f'/home/claude/GeenIdee/behuizing/{naam}.stl')
    nn = tm.face_normals; c = tm.triangles_center; bad = (nn[:, 2] < -0.72) & (c[:, 2] > 0.3)
    print(naam, 'binnen-d', round(2 * R_IN, 2), 'watertight', tm.is_watertight, 'maat', np.round(tm.extents, 1), 'delen', len(m.decompose()), 'overhang mm2', round(tm.area_faces[bad].sum(), 1))
    return voet
maak(0.0, 'buddy_voetje_kraag_strak')
maak(0.1, 'buddy_voetje_kraag_normaal')
