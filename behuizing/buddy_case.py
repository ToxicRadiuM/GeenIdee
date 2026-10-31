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
LIP    = 2.0           # rand voor het glas (v4: dikker, dan print hij als dicht vlak i.p.v. losse vulling)
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

# =================== deel 2: achterkant (v7: één stuk mét achterwand, geen los dekseltje meer) ===================
# Geprint met de achterwand op het bed. In plaats van een tussenplaat drukken 3 paaltjes op de messing busjes van het bord.
Z0 = Z_BUS                                  # hier zitten de busjes
L_PLUG = PLUG
L_ACHT = HOLTE + ACHTER
plug_r = BINN_D / 2 - PASSING
OVERG = BUIT_D / 2 - plug_r                 # 45 graden schuine overgang buitenkant
R_HOL = BUIT_D / 2 - WAND                   # binnenkant hoofdwand
r_plug_in = plug_r - 1.6
achter = cyl(L_PLUG, plug_r).translate([0, 0, Z0]) + cyl(OVERG, plug_r, BUIT_D / 2).translate([0, 0, Z0 + L_PLUG]) + cyl(L_ACHT - OVERG, BUIT_D / 2).translate([0, 0, Z0 + L_PLUG + OVERG])
z_back = Z0 + L_PLUG + L_ACHT
# holte: plug-deel, dan 45 graden schuin naar de wijdere holte (geen overhang als hij achterwand-onder print)
achter -= cyl(L_PLUG + 0.02, r_plug_in).translate([0, 0, Z0 - 0.01])
achter -= cyl(R_HOL - r_plug_in + 0.1, r_plug_in - 0.05, R_HOL + 0.05).translate([0, 0, Z0 + L_PLUG - 0.05])
achter -= cyl(z_back - ACHTER - (Z0 + L_PLUG + R_HOL - r_plug_in) + 0.2, R_HOL).translate([0, 0, Z0 + L_PLUG + R_HOL - r_plug_in - 0.2])
VOORSPAN = 0.15
# paaltjes op de busjes, met een pinnetje dat in het M2-gaatje valt (centreert het bord)
for (x, y) in MOUNTS:
    achter += cyl(z_back - ACHTER - Z0 + 0.01 + VOORSPAN, 3.0).translate([x, y, Z0 - VOORSPAN])   # v8: iets langer -> klemt het bord
    # v7c: ribbetje van paaltje naar de wand -> geen losse eilandjes meer (die werden pap: draadjes en klodders)
    ang = math.degrees(math.atan2(y, x)); rp = math.hypot(x, y)
    rib = box(R_HOL - rp + 0.6, 1.8, z_back - ACHTER - (Z0 + L_PLUG + R_HOL - r_plug_in) + 0.01)
    achter += rib.translate([(rp + R_HOL + 0.6) / 2, 0, (Z0 + L_PLUG + R_HOL - r_plug_in) + (z_back - ACHTER - (Z0 + L_PLUG + R_HOL - r_plug_in)) / 2]).rotate([0, 0, ang])
# speakerrooster v7f: groot rond gat in de buitenste laagjes, daarna twee lagen schuine latjes eroverheen:
# eerst //// en daarbovenop \\\\ (kruislings, als een gaasje). Lange rechte lijnen, niks kleins.
ROOSTER_R, OPEN_DIK, LAT_B, LAT_GAT = 12.5, 0.8, 1.4, 2.2
LAAG = (ACHTER - OPEN_DIK) / 2                      # 0,6 mm per richting
achter -= cyl(OPEN_DIK + 0.5, ROOSTER_R).translate([0, -2, z_back - OPEN_DIK])
def latten_gaten(hoek, z0):
    g = None
    for k in range(-12, 13):
        yk = k * (LAT_B + LAT_GAT) + (LAT_B + LAT_GAT) / 2
        s_ = box(2 * ROOSTER_R + 4, LAT_GAT, LAAG + 0.02).translate([0, yk, 0])
        g = s_ if g is None else g + s_
    return (g.rotate([0, 0, hoek]) ^ cyl(LAAG + 0.04, ROOSTER_R, center=True)).translate([0, -2, z0])
achter -= latten_gaten(45, z_back - OPEN_DIK - LAAG / 2)            # laag tegen het gat: ////
achter -= latten_gaten(-45, z_back - OPEN_DIK - LAAG * 1.5)         # binnenste laag: \\\\
# nokjes die een rond speakertje (tot 30 mm) vasthouden
for k in range(4):
    ang = math.radians(45 + 90 * k); r = 15.8
    achter += box(3, 3, 3.5).rotate([0, 0, 45 + 90 * k]).translate([r * math.cos(ang), r * math.sin(ang) - 2, z_back - ACHTER - 1.75 + 0.01])
# schuine buitenrand achter (bedkant)
achter -= (cyl(1.0, BUIT_D / 2 + 1) - cyl(1.0, BUIT_D / 2, BUIT_D / 2 - 1.0)).translate([0, 0, z_back - 1.0 + 0.001])
# USB-C: inkeping in de plug + 1,2 mm in de wand
achter -= box(10, 12.8, L_PLUG + 1.2).translate([BUIT_D / 2 - 2, 0, Z0 + (L_PLUG + 1.2) / 2 - 0.01])

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
# v6: klemmetje aan de achterkant van het zadel. De zijwanden lopen 12 graden voorbij het midden van de buis door,
# dus de opening is ~0,7 mm smaller dan de behuizing: je drukt hem erin en hij klikt vast (PETG veert mee).
KLEM_W, KLEM_DIK, KLEM_GRAAD = 9.0, 2.4, 12.0
KLEM_Y0 = 5.0                                     # van y=5 tot y=14 (achterste deel, achter de klikvenstertjes)
kz = pod_r * math.sin(math.radians(KLEM_GRAAD))
klem = box(2 * (pod_r + KLEM_DIK), KLEM_W, pod_r + kz + 30).translate([0, KLEM_Y0 + KLEM_W / 2, kz - (pod_r + kz + 30) / 2])
klem -= cyl(lengte_pod + 30, pod_r, center=True).rotate([90, 0, 0])
# afgeschuinde binnenrand bovenaan, zodat hij er makkelijk in glijdt
for sx in (-1, 1):
    klem -= box(3, KLEM_W + 2, 3).rotate([0, 45, 0]).translate([sx * (pod_r * math.cos(math.radians(KLEM_GRAAD)) + 0.3), KLEM_Y0 + KLEM_W / 2, kz])
klem = klem.rotate([-KANTEL, 0, 0]).translate([0, 0, H])
zadel = zadel + klem
zadel = zadel ^ box(80, 80, 80).translate([0, 0, 40])
# tape-vlak onder is vlak; rubberen dopjes optioneel

def save(m, naam):
    mesh = m.to_mesh()
    v = np.array(mesh.vert_properties)[:, :3]; f = np.array(mesh.tri_verts)
    t = trimesh.Trimesh(v, f, process=False)
    t.export(f'/home/claude/case/{naam}.stl')
    b = t.bounds; print(naam, 'maat', np.round(b[1] - b[0], 1), 'watertight', t.is_watertight, 'vol', round(t.volume / 1000, 1), 'cm3')
    return t

# =================== v5: alles in elkaar klikken, zonder schroefjes ===================
# 3 klikarmpjes aan de voorkant grijpen in 3 venstertjes in de achterkant. Het bord zit geklemd tussen het schermrandje
# en de tussenplaat (de schroefgaatjes mogen blijven, maar zijn niet meer nodig).
R_UIT = BUIT_D / 2
# v7: armpjes langer en het haakje minder diep -> buigen ~4x minder ver door, breken niet meer af
ARM_DIK, ARM_BR, ARM_LEN = 1.4, 10.0, 9.5
HAAK_Z0, HAAK_H, HAAK_IN = 7.4, 1.6, 0.5     # haakje: vanaf ARM-basis, hoogte, hoe ver naar binnen
SPEL_K = 0.2
HOEKEN = (90, 210, 330)                          # niet bij USB-C (0) en de knopjes (146/214 zitten alleen in de voorkant)
def ringstuk(r_in, r_uit, breed, z0, h, hoek):
    b = box(r_uit - r_in, breed, h).translate([(r_in + r_uit) / 2, 0, z0 + h / 2])
    return b.rotate([0, 0, hoek])
# v8: DRAAISLUITING (bajonet) i.p.v. klikarmpjes: niets hoeft te buigen, dus niets breekt of springt los.
# 3 nokjes op de plug van de achterkant; in de voorkant 3 L-vormige gleufjes: recht erin, dan 20 graden draaien.
from manifold3d import CrossSection
def boog(r_in, r_uit, a0, a1, z0, h):
    n = max(6, int(abs(a1 - a0) / 2) + 2)
    pts = [(r_uit * math.cos(math.radians(a0 + (a1 - a0) * i / n)), r_uit * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
    pts += [(r_in * math.cos(math.radians(a1 - (a1 - a0) * i / n)), r_in * math.sin(math.radians(a1 - (a1 - a0) * i / n))) for i in range(n + 1)]
    return Manifold.extrude(CrossSection([pts]), h).translate([0, 0, z0])
NOK_HOEK = (95, 215, 335)          # eindstand (na het draaien); USB-C zit op 0
DRAAI = 20.0
NOK_B = 5.0                        # breedte nokje (mm)
NOK_UIT = 1.0                      # hoe ver het nokje uitsteekt
NZ0, NZ1 = Z0 + 0.6, Z0 + 2.0      # nokje van z 13,0 tot 14,4
R_B = BINN_D / 2                   # boring voorkant
G_R = R_B + 1.25                   # diepte gleuf in de voorwand
SP_G = 0.25                        # speling
for hk in NOK_HOEK:
    hb = math.degrees(NOK_B / 2 / plug_r)
    # nokje: onderkant (voorkant) plat, bovenkant 45 graden schuin (print zonder support met de achterwand op het bed)
    nok = Manifold.hull(boog(plug_r - 0.3, plug_r + NOK_UIT, hk - hb, hk + hb, NZ0, 0.35) + boog(plug_r - 0.3, plug_r + 0.02, hk - hb, hk + hb, NZ0, NZ1 - NZ0))
    achter += nok
    # gleuf rondom (draaistuk) in de voorkant: vloer plat, dak 45 graden (print zonder support met de schermrand op het bed)
    gb = math.degrees((NOK_B / 2 + SP_G) / R_B)
    gleuf = Manifold.hull(boog(R_B - 1, G_R, hk - DRAAI - gb, hk + gb, NZ0 - SP_G, 0.35 + 2 * SP_G) + boog(R_B - 1, R_B + 0.02, hk - DRAAI - gb, hk + gb, NZ0 - SP_G, NZ1 - NZ0 + 2 * SP_G + 0.45))   # dak iets steiler dan 45 graden
    voor -= gleuf
    # rechte invoer vanaf de achterrand van de voorkant naar het draaistuk
    voor -= boog(R_B - 1, G_R, hk - DRAAI - gb, hk - DRAAI + gb, NZ0 - SP_G, L1 - NZ0 + 1)
# nokjes op de achterkant mogen niet tegen de oude armuitsparingen zitten (die zijn er niet meer)
t1 = save(voor, 'buddy_1_voorkant')
# achterkant geprint met de achterwand op het bed: omdraaien
t2 = save(achter.rotate([180, 0, 0]).translate([0, 0, z_back]), 'buddy_2_achterkant')   # v7: achterwand op het bed
t4 = save(zadel, 'buddy_4_voetje')
# preview: alles in elkaar
samen = [voor, achter]
save(voor + achter, '_preview_pod')
