# -*- coding: utf-8 -*-
"""
Separa el brazo derecho del árbitro para poder animarlo por código (tecla T).

El FBX original no tiene esqueleto, huesos ni animaciones: son 5 mallas estáticas. Sin embargo,
el brazo derecho (dentro de la malla "Cuerpo") y la manga derecha (dentro de la playera
"manga_ralgla_cuello_en_v") son piezas de geometría desconectadas del resto. Este script las
aparta sin modificar ningún vértice, para que el brazo gire como pieza rígida sobre el hombro.

No modifica el FBX original (se importa, se separa en memoria y no se guarda el .blend).

Uso (desde la carpeta luchaLibreCompuG/luchaLibreCompuG):
    blender -b --factory-startup --python herramientas/separar_arbitro.py

Genera, en modelos/modelos_importados/arbitro-ruper-design/separado/:
    arbitro_cuerpo.fbx          todo el árbitro excepto el brazo y la manga derechos
    arbitro_brazo_derecho.fbx   brazo derecho + manga derecha

Ambos archivos comparten el mismo sistema de coordenadas: el origen es el centro de la base de la
caja envolvente del modelo COMPLETO (el mismo punto que usaba Pivot::BaseCenter con el FBX
original), así que se cargan con Pivot::Original y el árbitro queda exactamente donde estaba.
El script imprime el punto del hombro en ese sistema (metros, Y arriba, frente hacia +Z) para
copiarlo a ArbitroParams::HOMBRO_DER en AnimacionArbitro.h. Los FBX incluyen la fecha de
exportación, así que al regenerarlos cambia su hash aunque la geometría sea la misma.
"""
import bpy
import bmesh
import os
from mathutils import Vector, Matrix

AQUI = os.path.dirname(os.path.abspath(__file__))
CARPETA = os.path.abspath(os.path.join(AQUI, "..", "modelos", "modelos_importados", "arbitro-ruper-design"))
ORIGINAL = os.path.join(CARPETA, "source", "manga ralgla_cuello en v.fbx")
SALIDA = os.path.join(CARPETA, "separado")

# Articulación del hombro derecho (coordenadas de Blender del modelo original, metros, Z arriba,
# frente hacia -Y). Se eligió dentro de la manga, sobre el extremo superior del brazo (z = 1.44),
# y se comprobó con renders que al levantar el brazo ~100° la manga no deja huecos visibles de frente.
HOMBRO_ORIGINAL = Vector((-0.21, 0.07, 1.43))


def islas(ob):
    """Piezas conectadas de una malla: lista de listas de índices de vértice."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.verts.ensure_lookup_table()
    vistos, piezas = set(), []
    for v in bm.verts:
        if v.index in vistos:
            continue
        pila, pieza = [v], []
        vistos.add(v.index)
        while pila:
            x = pila.pop()
            pieza.append(x.index)
            for e in x.link_edges:
                y = e.other_vert(x)
                if y.index not in vistos:
                    vistos.add(y.index)
                    pila.append(y)
        piezas.append(pieza)
    bm.free()
    return piezas


def centro(ob, indices):
    return sum((ob.data.vertices[i].co for i in indices), Vector()) / len(indices)


def separar(ob, indices, nombre):
    """Mueve los vértices indicados (y sus caras) a un objeto nuevo."""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    seleccion = set(indices)
    for v in ob.data.vertices:
        v.select = v.index in seleccion
    for e in ob.data.edges:
        e.select = all(i in seleccion for i in e.vertices)
    for p in ob.data.polygons:
        p.select = all(i in seleccion for i in p.vertices)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.separate(type="SELECTED")
    bpy.ops.object.mode_set(mode="OBJECT")
    nuevo = [o for o in bpy.context.selected_objects if o is not ob][0]
    nuevo.name = nombre
    return nuevo


def exportar(ruta, objetos):
    bpy.ops.object.select_all(action="DESELECT")
    for ob in objetos:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = objetos[0]
    bpy.ops.export_scene.fbx(
        filepath=ruta,
        use_selection=True,
        object_types={"MESH"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        axis_forward="-Z",
        axis_up="Y",
        use_mesh_modifiers=False,
        mesh_smooth_type="FACE",
        use_custom_props=False,
        add_leaf_bones=False,
        bake_anim=False,
        path_mode="STRIP",
        embed_textures=False,
    )
    print("  ->", os.path.relpath(ruta, CARPETA), round(os.path.getsize(ruta) / 1e6, 2), "MB")


def a_motor(p):
    """Blender (Z arriba, frente -Y) -> espacio normalizado del motor (Y arriba, frente +Z)."""
    return Vector((p.x, p.z, -p.y))


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=ORIGINAL)
    mallas = [o for o in bpy.data.objects if o.type == "MESH"]

    # Coordenadas del mundo directamente en los vértices (la importación deja rotaciones/escalas en los objetos)
    for ob in mallas:
        ob.data.transform(ob.matrix_world)
        ob.matrix_world = Matrix.Identity(4)

    # Origen: centro de la base de la caja del modelo completo (igual que Pivot::BaseCenter)
    mn = Vector((1e9,) * 3)
    mx = Vector((-1e9,) * 3)
    for ob in mallas:
        for v in ob.data.vertices:
            mn = Vector(map(min, mn, v.co))
            mx = Vector(map(max, mx, v.co))
    origen = Vector(((mn.x + mx.x) * 0.5, (mn.y + mx.y) * 0.5, mn.z))
    for ob in mallas:
        ob.data.transform(Matrix.Translation(-origen))
    print("Caja original:", tuple(round(c, 4) for c in mn), tuple(round(c, 4) for c in mx), "origen", tuple(round(c, 4) for c in origen))

    cuerpo = bpy.data.objects["Cuerpo"]
    camisa = bpy.data.objects["manga_ralgla_cuello_en_v"]
    # Brazo derecho del personaje = lado -X (mira hacia -Y en Blender); la manga es la pieza
    # lateral de la playera. Se exige que exista exactamente una pieza de cada una.
    brazo = [p for p in islas(cuerpo) if centro(cuerpo, p).x < -0.25 and centro(cuerpo, p).z > 0.8]
    manga = [p for p in islas(camisa) if centro(camisa, p).x < -0.15 and centro(camisa, p).z > 1.2]
    assert len(brazo) == 1 and len(manga) == 1, (len(brazo), len(manga))
    print("Brazo derecho:", len(brazo[0]), "vertices; manga derecha:", len(manga[0]), "vertices")

    # Sin texturas en los materiales: el motor asigna arbitro.png a todos (diffuseOverrides "*"),
    # igual que con el FBX original, cuyas texturas .tga no existen.
    for m in bpy.data.materials:
        if m.node_tree:
            for n in list(m.node_tree.nodes):
                if n.type == "TEX_IMAGE":
                    m.node_tree.nodes.remove(n)

    obj_brazo = separar(cuerpo, brazo[0], "Brazo_derecho")
    obj_manga = separar(camisa, manga[0], "Manga_derecha")

    os.makedirs(SALIDA, exist_ok=True)
    resto = [o for o in bpy.data.objects if o.type == "MESH" and o not in (obj_brazo, obj_manga)]
    exportar(os.path.join(SALIDA, "arbitro_cuerpo.fbx"), resto)
    exportar(os.path.join(SALIDA, "arbitro_brazo_derecho.fbx"), [obj_brazo, obj_manga])

    # Dato para AnimacionArbitro.h (espacio normalizado del motor, metros)
    hombro = a_motor(HOMBRO_ORIGINAL - origen)
    print("HOMBRO_DER (motor):", tuple(round(c, 4) for c in hombro))


main()
