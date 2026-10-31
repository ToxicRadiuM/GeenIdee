"""Buddy-behuizing voor de Waveshare ESP32-S3-Touch-AMOLED-1.75 (-G).
Maten uit de officiële Waveshare-maattekening. Alle maten in mm.
z-as = diepte: z=0 is de voorkant van de behuizing, naar achteren positief.
"""
import math, numpy as np, trimesh
from manifold3d import Manifold, set_circular_segments
set_circular_segments(128)

# ---------- bord ----------
GLAS_D   = 48.96   # buitenmaat glas
ZICHT_D  = 45.0    # opening voorkant (zichtbaar scherm 43.76 + een randje)
BORD_DIEP = 10.40  # voorkant glas -> achterkant messing afstandsbusjes
PCB_ACHTER = 6.90  # voorkant glas -> achterkant printplaat
MOUNTS = [(0.0, 20.5), (-13.75, -14.7), (13.75, -14.7)]   # M2-busjes (achterkant)

# ---------- behuizing ----------
SPEL   = 0.35          # speling rond het glas
WAND   = 2.4
LIP    = 1.4           # rand voor het glas
BUIT_D = GLAS_D + 2 * SPEL + 2 * WAND   # ~54.5
BINN_D = GLAS_D + 2 * SPEL              # ~49.66
Z_BUS  = LIP + BORD_DIEP                # achterkant busjes
PLUG   = 3.0           # hoe ver de achterkant in de voorkant steekt
PLAAT  = 2.0           # tussenplaat achter het bord (schroeven)
HOLTE  = 15.0          # ruimte voor speaker + GPS-antenne
ACHTER = 2.0           # achterwand
PASSING = 0.25         # speling plug in voorkant

def cyl(h, r, r2=None, center=False):
    return Manifold.cylinder(h, r, r if r2 is None else r2, 0, center)

def box(x, y, z, center=True):
    return Manifold.cube([x, y, z], center)

def radiaal_gat(hoek_deg, z, breed, hoog, diep=12):
    """rechthoekig gat door de zijwand, op hoek (0 = rechts, 90 = boven)"""
    b = box(diep, breed, hoog).translate([BUIT_D / 2, 0, z])
    return b.rotate([0, 0, hoek_deg])

def druppel_gat(hoek_deg, z, d, diep=12):
    # rond gat met een 45-graden puntje erboven (print zonder support)
    c = cyl(diep, d / 2, center=True).rotate([0, 90, 0])
    k = box(diep, d / 2 * 1.414, d / 2 * 1.414).rotate([45, 0, 0])
    return (c + k).translate([BUIT_D / 2, 0, z]).rotate([0, 0, hoek_deg])

def dak_sleuf(hoek_deg, z, breed, hoog, diep=12):
    # rechthoekige sleuf met een schuin dak (45 graden) bovenop
    b = box(diep, breed, hoog).translate([0, 0, 0])
    dak = Manifold.hull(box(diep, breed, 0.01).translate([0, 0, hoog / 2]) + box(diep, 0.4, 0.01).translate([0, 0, hoog / 2 + breed / 2 - 0.2]))
    return (b + dak).translate([BUIT_D / 2, 0, z]).rotate([0, 0, hoek_deg])

def rond_gat(hoek_deg, z, d, diep=12):
    c = cyl(diep, d / 2, center=True).rotate([0, 90, 0]).translate([BUIT_D / 2, 0, z])
    return c.rotate([0, 0, hoek_deg])

# =================== deel 1: voorkant ===================
L1 = Z_BUS + PLUG
voor = cyl(L1, BUIT_D / 2)
voor -= cyl(L1, BINN_D / 2).translate([0, 0, LIP])                    # kamer voor het bord
voor -= cyl(LIP + 0.01, ZICHT_D / 2 + 1.2, ZICHT_D / 2).translate([0, 0, -0.005])  # schuine zichtrand
# afgeronde voorrand (afschuining buitenkant)
voor -= (cyl(1.6, BUIT_D / 2 + 1, BUIT_D / 2 + 1) - cyl(1.6, BUIT_D / 2 - 1.2, BUIT_D / 2 + .4)).translate([0, 0, -0.001])
z_usb = LIP + PCB_ACHTER + 1.6
# v2: USB-C zat ~2 mm te laag -> open sleuf tot de achterrand (z 7,6 .. 14,8), past ook voor een dikke stekker
voor -= box(14, 12.8, L1 - 7.6 + 0.02).translate([BUIT_D / 2, 0, 7.6 + (L1 - 7.6) / 2 + 0.01])
voor -= dak_sleuf(180, LIP + PCB_ACHTER - 0.4, 13.0, 4.0)  # micro-SD links (v3: met schuin dak, geen support)
for h in (146, 214):                               # PWR (boven) en BOOT (onder) links: gaatje voor een pen/paperclip
    voor -= druppel_gat(h, LIP + PCB_ACHTER + 1.6, 3.6)

# =================== deel 2: achterkant ===================
Z0 = Z_BUS                                  # voorkant tussenplaat raakt de busjes
L_PLUG = PLUG
L_ACHT = HOLTE + ACHTER
plug_r = BINN_D / 2 - PASSING
OVERG = BUIT_D / 2 - plug_r   # v3: 45 graden schuine overgang, dan is er geen support nodig
achter = cyl(L_PLUG, plug_r).translate([0, 0, Z0]) + cyl(OVERG, plug_r, BUIT_D / 2).translate([0, 0, Z0 + L_PLUG]) + cyl(L_ACHT - OVERG, BUIT_D / 2).translate([0, 0, Z0 + L_PLUG + OVERG])
# holte (achter de tussenplaat)
achter -= cyl(L_PLUG - PLAAT + L_ACHT - ACHTER, plug_r - 1.6).translate([0, 0, Z0 + PLAAT])
achter -= cyl(L_ACHT - ACHTER, BUIT_D / 2 - WAND).translate([0, 0, Z0 + L_PLUG])
# vlakke 'plank' bovenin voor de GPS-antenne (antenne plakken, keramiek naar boven)
plank_y = 17.5
# v3: plank weggehaald (die had support nodig). De GPS-antenne gaat plat op de tussenplaat, keramiek naar achteren.
# tussenplaat: schroefgaten M2 + verzinking aan de holte-kant
for (x, y) in MOUNTS:
    achter += cyl(PLAAT, 3.2).translate([x, y, Z0])            # oogje rond het busje
    achter -= cyl(PLAAT + 2, 1.15).translate([x, y, Z0 - 1])   # M2 doorgaand
# v2: inkeping in de plug + 1,2 mm in de wand voor de USB-C-stekker
achter -= box(10, 12.8, L_PLUG + 1.2).translate([BUIT_D / 2 - 2, 0, Z0 + (L_PLUG + 1.2) / 2 - 0.01])
# doorvoeren in de tussenplaat: header onder, SPK/BAT-stekkertjes boven, GPS-kabeltje + midden
achter -= box(26, 7.5, 10).translate([-0.5, -18.6, Z0])
for xs in (-7.4, 7.4):
    achter -= box(8.6, 8, 10).translate([xs, 19.0, Z0])
achter -= box(18, 15, 10).translate([10.5, -4, Z0])
achter -= box(14, 12, 10).translate([-9, 2, Z0])
z_back = Z0 + L_PLUG + L_ACHT
# achterwand wordt een los dekseltje (dan hoeft er niets in de lucht geprint te worden)
achter -= cyl(ACHTER + 1, BUIT_D / 2 - WAND).translate([0, 0, z_back - ACHTER])

# =================== deel 3: dekseltje met speakerrooster ===================
SP = 0.2
deksel = cyl(1.4, BUIT_D / 2) + cyl(2.2, BUIT_D / 2 - WAND - SP).translate([0, 0, -2.2])   # flens buiten + ring die erin steekt
deksel -= cyl(2.2, BUIT_D / 2 - WAND - SP - 1.6).translate([0, 0, -2.21])                        # ring hol
deksel = deksel - (cyl(1.0, BUIT_D / 2 + 1) - cyl(1.0, BUIT_D / 2, BUIT_D / 2 - 1.0)).translate([0, 0, 0.41])  # v3: schuine buitenrand (aan de bedkant, geen overhang)
for i in range(-5, 6):
    for j in range(-5, 6):
        x = i * 3.6 + (1.8 if j % 2 else 0); y = j * 3.1
        if math.hypot(x, y - 0) < 13.5:
            deksel -= cyl(6, 0.95).translate([x, y - 2, -3])
for k in range(4):   # nokjes die een rond speakertje (tot 30 mm) vasthouden
    ang = math.radians(45 + 90 * k); r = 15.8
    deksel += box(3, 3, 3.5).rotate([0, 0, 45 + 90 * k]).translate([r * math.cos(ang), r * math.sin(ang) - 2, -1.75])

# =================== deel 3: voetje ===================
KANTEL = 12.0                     # scherm iets omhoog gekanteld, naar de bestuurder
lengte_pod = z_back
pod_r = BUIT_D / 2 + 0.25
voet_basis = Manifold.hull(Manifold.batch_hull([cyl(1, 7).translate([sx * 18, sy * 15, 0]) for sx in (-1, 1) for sy in (-1, 1)]) +
                           Manifold.batch_hull([cyl(1, 5).translate([sx * 15, sy * 11, 9]) for sx in (-1, 1) for sy in (-1, 1)]))
# pod-as langs y; voorkant (scherm) wijst naar -y en kantelt omhoog
t_ = math.radians(KANTEL)
pod_vorm = cyl(lengte_pod + 30, pod_r, center=True).rotate([90, 0, 0]).rotate([-KANTEL, 0, 0])
H = 3.0 + 15 * math.tan(t_) + pod_r / math.cos(t_)
pod_vorm = pod_vorm.translate([0, 0, H])
zadel = voet_basis + Manifold.hull(box(50, 30, 1).translate([0, 0, 0.5]) + box(44, 26, 1).translate([0, 0, 15]))
zadel = zadel - pod_vorm
zadel = zadel ^ box(80, 80, 40).translate([0, 0, 20])
# tape-vlak onder is vlak; rubberen dopjes optioneel

def save(m, naam):
    mesh = m.to_mesh()
    v = np.array(mesh.vert_properties)[:, :3]; f = np.array(mesh.tri_verts)
    t = trimesh.Trimesh(v, f, process=True)
    t.export(f'/home/claude/case/{naam}.stl')
    b = t.bounds; print(naam, 'maat', np.round(b[1] - b[0], 1), 'watertight', t.is_watertight, 'vol', round(t.volume / 1000, 1), 'cm3')
    return t

t1 = save(voor, 'buddy_1_voorkant')
# achterkant geprint met de achterwand op het bed: omdraaien
t2 = save(achter.translate([0, 0, -Z0]), 'buddy_2_achterkant')          # tussenplaat op het bed
t3 = save(deksel.rotate([180, 0, 0]).translate([0, 0, 1.4]), 'buddy_3_dekseltje')  # buitenkant op het bed
t4 = save(zadel, 'buddy_4_voetje')
# preview: alles in elkaar
samen = [voor, achter]
save(voor + achter + deksel.translate([0, 0, z_back]), '_preview_pod')
