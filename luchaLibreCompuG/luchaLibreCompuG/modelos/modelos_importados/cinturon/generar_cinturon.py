# -*- coding: utf-8 -*-
"""
Genera cinturon_lucha_libre.blend (cinturon de campeonato ORIGINAL, sin addons).
Uso:  blender -b --factory-startup --python generar_cinturon.py
Escribe junto a este script:  cinturon_lucha_libre.blend  y  textures/*.png
Convencion de ejes: el cinturon corre a lo largo de X, altura en Z,
la cara frontal mira hacia -Y (vista Front de Blender).
"""
import bpy, bmesh, math, os
import numpy as np
from mathutils import Vector, Matrix

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
TEX_DIR = os.path.join(OUT_DIR, "textures")
BLEND_PATH = os.path.join(OUT_DIR, "cinturon_lucha_libre.blend")
os.makedirs(TEX_DIR, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = "Escena_Cinturon"

# ----------------------------------------------------------------------------
# TEXTURAS (numpy, 512x512, PNG externos)
# ----------------------------------------------------------------------------
N = 512


def cell_noise(n, cells, seed):
    rng = np.random.default_rng(seed)
    pts = rng.random((cells, cells, 2))
    ys, xs = np.mgrid[0:n, 0:n] / n * cells
    gx, gy = np.floor(xs).astype(int), np.floor(ys).astype(int)
    f1 = np.full((n, n), 9.0)
    f2 = np.full((n, n), 9.0)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            cx, cy = gx + dx, gy + dy
            px = cx + pts[cy % cells, cx % cells, 0]
            py = cy + pts[cy % cells, cx % cells, 1]
            d = np.hypot(xs - px, ys - py)
            f2 = np.minimum(f2, np.maximum(f1, d))
            f1 = np.minimum(f1, d)
    return f1, f2


def normal_from_height(h, strength):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * strength
    nz = np.ones_like(h)
    l = np.sqrt(dx * dx + dy * dy + nz * nz)
    return np.stack([-dx / l * 0.5 + 0.5, -dy / l * 0.5 + 0.5, nz / l * 0.5 + 0.5], -1)


def gray(a):
    return np.repeat(np.clip(a, 0, 1)[..., None], 3, -1)


def save_img(name, rgb, srgb):
    rgb = np.clip(rgb, 0, 1)
    h, w = rgb.shape[:2]
    rgba = np.concatenate([rgb, np.ones((h, w, 1))], -1).astype(np.float32)
    img = bpy.data.images.new(name, w, h, alpha=False)
    img.colorspace_settings.name = "sRGB" if srgb else "Non-Color"
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = os.path.join(TEX_DIR, name + ".png")
    img.file_format = "PNG"
    img.save()
    return img


def make_textures():
    tex = {}
    rng = np.random.default_rng(7)
    lin = np.linspace(0, 1, N, endpoint=False)
    u, v = np.meshgrid(lin, lin)

    # --- cuero (tileable) ---
    f1, f2 = cell_noise(N, 28, 3)
    crack = np.clip((f2 - f1) * 3.5, 0, 1)
    crack = crack * crack * (3 - 2 * crack)
    fine = rng.random((N, N))
    h = 0.75 * crack + 0.25 * fine
    col = np.array([0.022, 0.012, 0.007])
    base = col[None, None, :] * (0.55 + 0.9 * h[..., None])
    tex["cuero"] = {
        "base": save_img("cuero_basecolor", base, True),
        "rough": save_img("cuero_roughness", gray(0.7 + 0.25 * (1 - h)), False),
        "normal": save_img("cuero_normal", normal_from_height(h, 2.2), False),
    }

    # --- oro grabado (se mapea 0..1 una vez por placa) ---
    r = np.hypot(u - 0.5, v - 0.5) * 2
    th = np.arctan2(v - 0.5, u - 0.5)
    ring = 0.5 + 0.5 * np.sin(2 * np.pi * (r * 9 + 0.18 * np.sin(8 * th)))
    petal = 0.5 + 0.5 * np.cos(16 * th) * np.clip(1.2 - r, 0, 1)
    grain = rng.random((N, N))
    h = 0.55 * ring + 0.3 * petal + 0.15 * grain
    gold = np.array([1.0, 0.74, 0.22])
    base = gold[None, None, :] * (0.65 + 0.4 * h[..., None])
    tex["oro"] = {
        "base": save_img("oro_basecolor", base, True),
        "rough": save_img("oro_roughness", gray(0.22 + 0.3 * (1 - h)), False),
        "metal": save_img("oro_metallic", gray(np.ones((N, N))), False),
        "normal": save_img("oro_normal", normal_from_height(h, 1.6), False),
    }

    # --- metal secundario (plata cepillada) ---
    rows = rng.random((N, 1))
    h = 0.6 * np.repeat(rows, N, 1) + 0.4 * rng.random((N, N))
    silver = np.array([0.6, 0.62, 0.66])
    base = silver[None, None, :] * (0.8 + 0.4 * h[..., None])
    tex["metal"] = {
        "base": save_img("metal_basecolor", base, True),
        "rough": save_img("metal_roughness", gray(0.28 + 0.2 * h), False),
        "metal": save_img("metal_metallic", gray(np.ones((N, N))), False),
        "normal": save_img("metal_normal", normal_from_height(h, 0.8), False),
    }

    # --- gema (facetas radiales) ---
    facet = np.abs(np.sin(4 * th)) * np.clip(1 - r, 0, 1) + 0.15 * np.abs(np.sin(r * 12))
    ruby = np.array([0.55, 0.01, 0.03])
    base = ruby[None, None, :] * (0.5 + 1.3 * facet[..., None])
    tex["gema"] = {
        "base": save_img("gema_basecolor", base, True),
        "rough": save_img("gema_roughness", gray(0.06 + 0.1 * (1 - facet)), False),
        "normal": save_img("gema_normal", normal_from_height(facet, 3.0), False),
    }
    return tex


def make_material(name, tex, color, metallic, rough, nstrength):
    mat = bpy.data.materials.new(name)
    try:
        mat.use_nodes = True
    except Exception:
        pass
    nt = mat.node_tree
    bsdf = next((n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if bsdf is None:
        nt.nodes.clear()
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
        out = nt.nodes.new("ShaderNodeOutputMaterial")
        nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    bsdf.location = (300, 0)
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = rough
    mat.diffuse_color = (*color, 1)
    mat.metallic = metallic
    mat.roughness = rough

    def tnode(img, y):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = img
        n.location = (-400, y)
        return n

    y = 300
    if "base" in tex:
        n = tnode(tex["base"], y); y -= 280
        nt.links.new(n.outputs["Color"], bsdf.inputs["Base Color"])
    if "rough" in tex:
        n = tnode(tex["rough"], y); y -= 280
        nt.links.new(n.outputs["Color"], bsdf.inputs["Roughness"])
    if "metal" in tex:
        n = tnode(tex["metal"], y); y -= 280
        nt.links.new(n.outputs["Color"], bsdf.inputs["Metallic"])
    if "normal" in tex:
        n = tnode(tex["normal"], y)
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nm.location = (0, y)
        nm.inputs["Strength"].default_value = nstrength
        nt.links.new(n.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


TEX = make_textures()
MAT_CUERO = make_material("MAT_Cuero", TEX["cuero"], (0.022, 0.012, 0.007), 0.0, 0.7, 0.8)
MAT_ORO = make_material("MAT_Oro", TEX["oro"], (1.0, 0.74, 0.22), 1.0, 0.3, 0.6)
MAT_METAL = make_material("MAT_Detalle_Metal", TEX["metal"], (0.6, 0.62, 0.66), 1.0, 0.3, 0.4)
MAT_GEMA = make_material("MAT_Gema", TEX["gema"], (0.55, 0.01, 0.03), 0.0, 0.1, 0.5)

# ----------------------------------------------------------------------------
# GEOMETRIA
# ----------------------------------------------------------------------------
HALF_L = 0.65      # medio largo de la correa  -> 1.30 m total
HALF_H = 0.07      # media altura de la correa -> 0.14 m
STRAP_T = 0.010    # grosor correa (centrada en d=0)
PLATE_BACK = 0.005 # cara trasera de las placas = cara frontal de la correa
PLATE_T = 0.010
TOP = PLATE_BACK + PLATE_T   # 0.015: nivel de la cara frontal de las placas


def V(bm, x, d, z):
    """d = profundidad hacia el espectador (frente = -Y)."""
    return bm.verts.new((x, -d, z))


def superellipse(a, b, n, lobes=0.0, lobe_n=12):
    def f(t):
        c, s = math.cos(t), math.sin(t)
        k = 1 + lobes * math.cos(lobe_n * t)
        x = a * math.copysign(abs(c) ** (2 / n), c) * k
        z = b * math.copysign(abs(s) ** (2 / n), s) * k
        return x, z
    return f


def revolve_solid(bm, outline, profile, n, thick, cx=0.0, cz=0.0):
    """Solido por anillos. profile=[(rho, profundidad)...]; rho 0 = vertice central."""
    def rows(off):
        c, rs = None, []
        for rho, d in profile:
            if rho == 0:
                c = V(bm, cx, d - off, cz)
                continue
            row = []
            for i in range(n):
                ox, oz = outline(2 * math.pi * i / n)
                row.append(V(bm, cx + ox * rho, d - off, cz + oz * rho))
            rs.append(row)
        return c, rs

    cf, rf = rows(0.0)
    cb, rb = rows(thick)

    def surf(c, rs):
        if c is not None:
            for i in range(n):
                bm.faces.new((c, rs[0][i], rs[0][(i + 1) % n]))
        for r in range(len(rs) - 1):
            for i in range(n):
                bm.faces.new((rs[r][i], rs[r + 1][i], rs[r + 1][(i + 1) % n], rs[r][(i + 1) % n]))

    def wall(a, b):
        for i in range(n):
            bm.faces.new((a[i], b[i], b[(i + 1) % n], a[(i + 1) % n]))

    surf(cf, rf)
    surf(cb, rb)
    wall(rf[-1], rb[-1])
    if cf is None:
        wall(rf[0], rb[0])


def poly_solid(bm, pts, d_front, d_back, cx=0.0, cz=0.0):
    """Poligono estrellado (abanico desde el centro) extruido."""
    n = len(pts)
    cf = V(bm, cx, d_front, cz)
    cb = V(bm, cx, d_back, cz)
    f = [V(bm, cx + x, d_front, cz + z) for x, z in pts]
    b = [V(bm, cx + x, d_back, cz + z) for x, z in pts]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((cf, f[i], f[j]))
        bm.faces.new((cb, b[j], b[i]))
        bm.faces.new((f[i], b[i], b[j], f[j]))


def add_box(bm, cx, cd, cz, sx, sd, sz):
    r = bmesh.ops.create_cube(bm, size=1.0)
    for v in r["verts"]:
        v.co = (cx + v.co.x * sx, -(cd + v.co.y * sd), cz + v.co.z * sz)


def add_dome(bm, cx, d, cz, rad, flat=0.6):
    try:
        r = bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=1.0)
    except TypeError:
        r = bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, diameter=1.0)
    for v in r["verts"]:
        v.co = (cx + v.co.x * rad, -(d + v.co.y * rad * flat), cz + v.co.z * rad)


def text_into_bm(bm, text, width, cx, cz, base_d, depth):
    curve = bpy.data.curves.new("tmp_txt", "FONT")
    curve.body = text
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    curve.extrude = 0.05
    curve.resolution_u = 3
    tmp = bpy.data.objects.new("tmp_txt", curve)
    scene.collection.objects.link(tmp)
    dg = bpy.context.evaluated_depsgraph_get()
    dg.update()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg), depsgraph=dg)
    scene.collection.objects.unlink(tmp)
    bpy.data.objects.remove(tmp)
    bpy.data.curves.remove(curve)
    xs = [v.co.x for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    s = width / (max(xs) - min(xs))
    z0, z1 = min(zs), max(zs)
    mx = (max(xs) + min(xs)) / 2
    my = (max(v.co.y for v in me.vertices) + min(v.co.y for v in me.vertices)) / 2
    tbm = bmesh.new()
    tbm.from_mesh(me)
    for v in tbm.verts:
        x, y, z = v.co
        d = base_d + (z - z0) / (z1 - z0) * depth
        v.co = (cx + (x - mx) * s, -d, cz + (y - my) * s)
    tmp_mesh = bpy.data.meshes.new("tmp_join")
    tbm.to_mesh(tmp_mesh)
    tbm.free()
    bm.from_mesh(tmp_mesh)
    bpy.data.meshes.remove(tmp_mesh)
    bpy.data.meshes.remove(me)


def assign_uv(bm, tile=None):
    uvl = bm.loops.layers.uv.get("UVMap") or bm.loops.layers.uv.new("UVMap")
    xs = [v.co.x for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    minx, maxx, minz, maxz = min(xs), max(xs), min(zs), max(zs)
    w = max(maxx - minx, 1e-6)
    h = max(maxz - minz, 1e-6)
    for f in bm.faces:
        for l in f.loops:
            x, z = l.vert.co.x, l.vert.co.z
            if tile:
                l[uvl].uv = (x / tile + 0.5, z / tile + 0.5)
            else:
                l[uvl].uv = ((x - minx) / w, (z - minz) / h)


def bend_xy(co, R):
    th = co.x / R
    r = R - co.y
    return Vector((r * math.sin(th), R - r * math.cos(th), co.z))


def finalize(bm, name, mat, coll, root, bend_R=None, tile=None, flat=False):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    assign_uv(bm, tile)
    if bend_R:
        for v in bm.verts:
            v.co = bend_xy(v.co, bend_R)
    for f in bm.faces:
        f.smooth = not flat
    for e in bm.edges:
        if len(e.link_faces) == 2 and e.calc_face_angle(0.0) > math.radians(40):
            e.smooth = False
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(mat)
    obj = bpy.data.objects.new(name, mesh)
    coll.objects.link(obj)
    obj.parent = root
    return obj


def strap_h(x):
    ax = abs(x)
    if ax < 0.57:
        return 1.0
    u = min((ax - 0.57) / (HALF_L - 0.57), 1.0)
    return 0.35 + 0.65 * math.sqrt(1 - u * u)


def build_strap(bm):
    nx, nz = 130, 4
    vf, vb = [], []
    for i in range(nx + 1):
        x = -HALF_L + 2 * HALF_L * i / nx
        h = HALF_H * strap_h(x)
        vf.append([V(bm, x, STRAP_T / 2, h * (-1 + 2 * j / nz)) for j in range(nz + 1)])
        vb.append([V(bm, x, -STRAP_T / 2, h * (-1 + 2 * j / nz)) for j in range(nz + 1)])
    for i in range(nx):
        for j in range(nz):
            bm.faces.new((vf[i][j], vf[i + 1][j], vf[i + 1][j + 1], vf[i][j + 1]))
            bm.faces.new((vb[i][j], vb[i][j + 1], vb[i + 1][j + 1], vb[i + 1][j]))
    for i in range(nx):
        bm.faces.new((vf[i][0], vb[i][0], vb[i + 1][0], vf[i + 1][0]))
        bm.faces.new((vf[i][nz], vf[i + 1][nz], vb[i + 1][nz], vb[i][nz]))
    for j in range(nz):
        bm.faces.new((vf[0][j], vf[0][j + 1], vb[0][j + 1], vb[0][j]))
        bm.faces.new((vf[nx][j], vb[nx][j], vb[nx][j + 1], vf[nx][j + 1]))


def plate_profile(edge=0.9):
    return [(0.0, TOP), (edge, TOP), (1.0, TOP - 0.003)]


def frame_profile(r0, r1, h):
    rm0, rm1 = r0 + (r1 - r0) * 0.3, r0 + (r1 - r0) * 0.7
    return [(r0, TOP), (rm0, TOP + h), (rm1, TOP + h), (r1, TOP)]


def build_variant(label, bend_R, coll, z_offset):
    root = bpy.data.objects.new("Raiz_Cinturon_" + label, None)
    root.empty_display_type = "PLAIN_AXES"
    root.empty_display_size = 0.08
    root.location = (0, 0, z_offset)
    coll.objects.link(root)
    S = "_" + label
    objs = []

    def fin(bm, name, mat, tile=None, flat=False):
        o = finalize(bm, name + S, mat, coll, root, bend_R, tile, flat)
        objs.append(o)
        return o

    # Correa
    bm = bmesh.new(); build_strap(bm)
    fin(bm, "Correa_Cinturon", MAT_CUERO, tile=0.3)

    # Placa central + marco + medallon + estrella + textos
    central = superellipse(0.20, 0.14, 2.6, lobes=0.035, lobe_n=12)
    bm = bmesh.new()
    revolve_solid(bm, central, plate_profile(), 72, PLATE_T)
    fin(bm, "Placa_Central", MAT_ORO)

    bm = bmesh.new()
    revolve_solid(bm, central, frame_profile(0.78, 0.90, 0.005), 72, 0.004)
    fin(bm, "Marco_Central", MAT_METAL)

    bm = bmesh.new()
    circ = lambda t: (math.cos(t), math.sin(t))
    cy = 0.04
    prof = [(0, TOP + 0.016)] + [(r, TOP + 0.016 * math.sqrt(1 - r * r)) for r in (0.3, 0.6, 0.85, 1.0)]
    # outline circular de radio 0.055 -> escalamos con un outline de radio fijo
    revolve_solid(bm, lambda t: (0.055 * math.cos(t), 0.055 * math.sin(t)), prof, 32, 0.004, cz=cy)
    fin(bm, "Medallon_Central", MAT_GEMA)

    bm = bmesh.new()
    pts = []
    for k in range(10):
        rr = 0.043 if k % 2 == 0 else 0.018
        a = math.pi / 2 + k * math.pi / 5
        pts.append((rr * math.cos(a), rr * math.sin(a)))
    poly_solid(bm, pts, TOP + 0.024, TOP + 0.012, cz=cy)
    fin(bm, "Estrella_Central", MAT_METAL)

    bm = bmesh.new()
    text_into_bm(bm, "CAMPEÓN", 0.17, 0.0, -0.058, TOP, 0.004)
    fin(bm, "Texto_Campeon", MAT_METAL)

    bm = bmesh.new()
    text_into_bm(bm, "LUCHA LIBRE", 0.12, 0.0, -0.088, TOP, 0.003)
    fin(bm, "Texto_LuchaLibre", MAT_METAL)

    # Placas laterales
    side = superellipse(0.07, 0.065, 4.0)
    side_x = {"Izq_01": -0.31, "Izq_02": -0.47, "Der_01": 0.31, "Der_02": 0.47}
    bm_marcos, bm_gemas = bmesh.new(), bmesh.new()
    for key, x in side_x.items():
        bm = bmesh.new()
        revolve_solid(bm, side, plate_profile(0.92), 48, PLATE_T, cx=x)
        fin(bm, "Placa_Lateral_" + key, MAT_ORO)
        revolve_solid(bm_marcos, side, frame_profile(0.72, 0.86, 0.004), 48, 0.003, cx=x)
        gprof = [(0, TOP + 0.010)] + [(r, TOP + 0.010 * math.sqrt(1 - r * r)) for r in (0.5, 0.85, 1.0)]
        revolve_solid(bm_gemas, lambda t: (0.026 * math.cos(t), 0.026 * math.sin(t)), gprof, 20, 0.003, cx=x)
    fin(bm_marcos, "Marcos_Laterales", MAT_METAL)
    fin(bm_gemas, "Gemas_Laterales", MAT_GEMA)

    # Remaches
    bm = bmesh.new()
    rd = 0.0075
    for sgn in (-1, 1):
        for x in (0.22, 0.39):
            for z in (-0.05, 0.0, 0.05):
                add_dome(bm, sgn * x, PLATE_BACK, z, rd)
        # esquinas de placas laterales
        for px in (0.31, 0.47):
            for cxs, czs in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
                ox, oz = side(math.atan2(czs, cxs))
                add_dome(bm, sgn * px + ox * 0.9 * 1.0, TOP, oz * 0.9, rd)
    for k in range(4):
        t = math.pi / 4 + k * math.pi / 2
        ox, oz = central(t)
        add_dome(bm, ox * 0.945, TOP - 0.002, oz * 0.945, rd)
    for z in (-0.035, 0.0, 0.035):
        add_dome(bm, -0.60, PLATE_BACK, z, rd)
    fin(bm, "Remaches", MAT_METAL)

    # Hebilla (extremo derecho)
    bm = bmesh.new()
    hx, hd = 0.60, PLATE_BACK + 0.007
    add_box(bm, hx - 0.038, hd, 0, 0.012, 0.014, 0.13)
    add_box(bm, hx + 0.038, hd, 0, 0.012, 0.014, 0.13)
    add_box(bm, hx, hd, 0.059, 0.088, 0.014, 0.012)
    add_box(bm, hx, hd, -0.059, 0.088, 0.014, 0.012)
    add_box(bm, hx, hd + 0.002, 0.0, 0.07, 0.010, 0.010)
    fin(bm, "Hebilla", MAT_ORO, flat=True)

    # Centrar el conjunto en su origen
    allv = [v.co for o in objs for v in o.data.vertices]
    mn = Vector((min(c.x for c in allv), min(c.y for c in allv), min(c.z for c in allv)))
    mx = Vector((max(c.x for c in allv), max(c.y for c in allv), max(c.z for c in allv)))
    c = (mn + mx) / 2
    for o in objs:
        o.data.transform(Matrix.Translation(-c))

    # Punto de colgado: encima de la placa central, en el eje del cinturon
    hang = bpy.data.objects.new("Punto_Colgado" + S, None)
    hang.empty_display_type = "ARROWS"
    hang.empty_display_size = 0.06
    top_pt = bend_xy(Vector((0, 0, 0.16)), bend_R) if bend_R else Vector((0, 0, 0.16))
    hang.location = top_pt - c
    hang.parent = root
    coll.objects.link(hang)
    objs.append(hang)
    return root, objs


# ----------------------------------------------------------------------------
# ESCENA / COLECCIONES
# ----------------------------------------------------------------------------
col_root = bpy.data.collections.new("Cinturon_Lucha_Libre")
scene.collection.children.link(col_root)
col_curv = bpy.data.collections.new("Cinturon_Curvado")
col_neut = bpy.data.collections.new("Cinturon_Neutro")
col_root.children.link(col_curv)
col_root.children.link(col_neut)

root_c, objs_c = build_variant("Curvado", 1.5, col_curv, 0.0)
root_n, objs_n = build_variant("Neutro", None, col_neut, -0.45)

# Camara y luces de preview (coleccion aparte, para no arrastrarlas al anexar)
col_prev = bpy.data.collections.new("Preview_Camara_Luces")
scene.collection.children.link(col_prev)

cam_data = bpy.data.cameras.new("Camara_Preview_Cinturon")
cam_data.lens = 50
cam = bpy.data.objects.new("Camara_Preview_Cinturon", cam_data)
cam.location = (0.0, -3.0, -0.22)
cam.rotation_euler = (math.radians(90), 0, 0)
col_prev.objects.link(cam)
scene.camera = cam


def add_light(name, loc, energy, size, target=(0, 0, -0.2)):
    ld = bpy.data.lights.new(name, "AREA")
    ld.energy = energy
    ld.size = size
    ob = bpy.data.objects.new(name, ld)
    ob.location = loc
    d = Vector(target) - Vector(loc)
    ob.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    col_prev.objects.link(ob)


add_light("Luz_Principal", (-1.6, -2.4, 1.6), 700, 1.5)
add_light("Luz_Auxiliar", (1.9, -2.2, 0.2), 250, 1.0)

world = bpy.data.worlds.new("Mundo_Preview")
try:
    world.use_nodes = True
except Exception:
    pass
bg = next((n for n in world.node_tree.nodes if n.type == "BACKGROUND"), None) if world.node_tree else None
if bg:
    bg.inputs["Color"].default_value = (0.04, 0.04, 0.045, 1)
    bg.inputs["Strength"].default_value = 1.0
scene.world = world
scene.render.resolution_x = 1920
scene.render.resolution_y = 1080

bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH, relative_remap=True)

# ----------------------------------------------------------------------------
# VALIDACION
# ----------------------------------------------------------------------------
print("\n===== VALIDACION =====")
print("Archivo guardado:", os.path.exists(BLEND_PATH), BLEND_PATH)
print("Texturas:", sorted(os.listdir(TEX_DIR)))
print("\nObjetos creados:")
for o in sorted(bpy.data.objects, key=lambda o: o.name):
    if o.type == "MESH":
        uv = [u.name for u in o.data.uv_layers]
        mats = [m.name for m in o.data.materials]
        print(f"  {o.name:34s} verts={len(o.data.vertices):5d} tris~{sum(len(p.vertices)-2 for p in o.data.polygons):5d} uv={uv} mat={mats}")
    else:
        print(f"  {o.name:34s} [{o.type}]")
print("\nMateriales creados:")
for m in bpy.data.materials:
    print("  ", m.name, "| usuarios:", m.users)
for lab, objs, root in (("Curvado", objs_c, root_c), ("Neutro", objs_n, root_n)):
    pts = [v.co for o in objs if o.type == "MESH" for v in o.data.vertices]
    mn = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
    mx = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    print(f"\n{lab}: bbox min={tuple(round(a,3) for a in mn)} max={tuple(round(a,3) for a in mx)}")
    print(f"  centro={tuple(round(a,3) for a in (mn+mx)/2)}  dimensiones={tuple(round(a,3) for a in (mx-mn))}")
sin_uv = [o.name for o in bpy.data.objects if o.type == "MESH" and not o.data.uv_layers]
sin_mat = [o.name for o in bpy.data.objects if o.type == "MESH" and not o.data.materials]
print("\nSin UV:", sin_uv, "| Sin material:", sin_mat)

