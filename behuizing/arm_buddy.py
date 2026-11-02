# Remix van 'iphone tesla landscape' (versia pionowa 95): de cirkel waar het verloopringetje in ging (binnen 55,4 mm)
# wordt zelf de strakke klemring voor Buddy (zelfde maat als de kraag 'strak'), met achteraan een randje als aanslag.
import sys, math, numpy as np, trimesh
from manifold3d import Manifold, Mesh, set_circular_segments
set_circular_segments(200)
exec(open('tesla_houder.py').read().split('R_BODY =')[0].split('set_circular_segments(160)')[1])
SRC = sys.argv[1]
t = trimesh.load(SRC); t.apply_translation(-t.bounds.mean(0)); t.apply_translation([0, 0, -t.bounds[0][2]])
arm = Manifold(Mesh(vert_properties=np.array(t.vertices, np.float32), tri_verts=np.array(t.faces, np.uint32)))
CX, CY, R_OUD = 1.6, -49.67, 27.70
pts = t.vertices[t.vertices[:, 1] < -21.0]
romp = Manifold.hull_points(pts.tolist())
def ring(r_in, r_uit=R_OUD + 0.3): return (cyl(80, r_uit) - cyl(82, r_in).translate([0, 0, -1])).translate([CX, CY, -20])
def maak(r_in, naam, rand=True):
    m = arm + (romp ^ ring(r_in))
    # inloopschuinte aan de voorkant (bed-kant, z=0) zodat Buddy er makkelijk in schuift
    m -= ring(r_in - 0.01, r_in + 0.6) ^ (romp - romp.translate([0, 0, 0.6]))
    if rand:   # aanslag achteraan (bovenkant van de print), trapsgewijs onder 45 graden: geen support nodig
        for rr, d in ((r_in - 0.5, 1.6), (r_in - 1.0, 1.1), (r_in - 1.5, 0.6)):
            m += romp ^ ring(rr) ^ (romp - romp.translate([0, 0, -d]))
    export(m, naam)
maak(27.18, 'arm_buddy_strak'); maak(27.28, 'arm_buddy_normaal')
