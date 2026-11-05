# -*- coding: utf-8 -*-
"""
Convierte los modelos que solo existen como .blend a OBJ para cargarlos con Assimp.
No modifica los .blend originales (se abren, se ajustan en memoria y se cierran sin guardar).

Uso (desde la carpeta luchaLibreCompuG/luchaLibreCompuG):
    blender -b --factory-startup --python herramientas/convertir_blend.py

Genera:
    modelos/modelos_importados/cinturon/cinturon_curvado.obj (+ .mtl)
    modelos/modelos_importados/cinturon/cinturon_neutro.obj  (+ .mtl)
    modelos/modelos_importados/espectador/espectador_A..F.obj (+ .mtl)  -> solo el cuerpo
    modelos/modelos_importados/espectador/espectador_silla.obj (+ .mtl)
    modelos/modelos_importados/set-of-four-4-low-poly-cameras/source/camara_*.obj (+ .mtl)

Convenciones de salida (las mismas que los OBJ ya exportados por el equipo):
    eje Y arriba, frente del modelo hacia +Z, unidades de Blender (metros),
    origen en la raíz de cada objeto (el cinturón queda centrado y los espectadores
    tienen el origen en el centro del asiento, a nivel del suelo).

Los espectadores se colorean con atributos de color por vértice, que el formato OBJ y el
shader del proyecto no usan. Por eso cada material se convierte en un material propio por
espectador cuyo color difuso es el promedio del color por vértice de sus caras; el script
imprime la desviación para comprobar que el color es uniforme y no se pierde información.
"""
import bpy
import os
from mathutils import Vector

BASE = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "modelos", "modelos_importados"))


def export_obj(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for ob in objects:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.wm.obj_export(
        filepath=path,
        export_selected_objects=True,
        apply_modifiers=True,
        export_uv=True,
        export_normals=True,
        export_materials=True,
        export_pbr_extensions=False,
        export_triangulated_mesh=True,
        path_mode="RELATIVE",
        forward_axis="NEGATIVE_Z",
        up_axis="Y",
    )
    print("  ->", os.path.relpath(path, BASE))


def bbox(objects):
    mn = Vector((1e9,) * 3)
    mx = Vector((-1e9,) * 3)
    for ob in objects:
        for v in ob.data.vertices:
            w = ob.matrix_world @ v.co
            for i in range(3):
                mn[i] = min(mn[i], w[i])
                mx[i] = max(mx[i], w[i])
    return mn, mx


# ---------------------------------------------------------------- Cinturón
def convertir_cinturon():
    blend = os.path.join(BASE, "cinturon", "cinturon_lucha_libre.blend")
    bpy.ops.wm.open_mainfile(filepath=blend)
    tex_dir = os.path.join(BASE, "cinturon", "textures")

    # Las imágenes se generaron por script; se asegura que apunten a los PNG de textures/
    for img in bpy.data.images:
        if img.type != "IMAGE":
            continue
        candidate = os.path.join(tex_dir, img.name + ".png")
        if os.path.exists(candidate):
            img.filepath = candidate
            img.source = "FILE"

    for label in ("Curvado", "Neutro"):
        root = bpy.data.objects["Raiz_Cinturon_" + label]
        root.location = (0.0, 0.0, 0.0)
        bpy.context.view_layer.update()
        meshes = [ob for ob in root.children_recursive if ob.type == "MESH"]
        mn, mx = bbox(meshes)
        print("Cinturon %s: %d mallas, tamano %s" % (label, len(meshes), tuple(round(v, 3) for v in (mx - mn))))
        export_obj(os.path.join(BASE, "cinturon", "cinturon_%s.obj" % label.lower()), meshes)


# ---------------------------------------------------------------- Espectadores
def color_promedio(me, attr, mat_index):
    """Promedio y desviación máxima del color por vértice en las caras de un material."""
    cols = []
    for poly in me.polygons:
        if poly.material_index != mat_index:
            continue
        for li in poly.loop_indices:
            if attr.domain == "CORNER":
                c = attr.data[li].color
            else:
                c = attr.data[me.loops[li].vertex_index].color
            cols.append(Vector(c[:3]))
    if not cols:
        return None, 0.0
    avg = sum(cols, Vector((0, 0, 0))) / len(cols)
    dev = max((c - avg).length for c in cols)
    return avg, dev


def material_desde_color(name, color):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = (color.x, color.y, color.z, 1.0)
    bsdf.inputs["Roughness"].default_value = 1.0
    bsdf.inputs["Metallic"].default_value = 0.0
    return mat


def convertir_espectadores():
    blend = os.path.join(BASE, "espectador", "espectadores_lowpoly.blend")
    bpy.ops.wm.open_mainfile(filepath=blend)
    out_dir = os.path.join(BASE, "espectador")

    for letra in "ABCDEF":
        root = bpy.data.objects["Espectador_%s_Root" % letra]
        root.location = (0.0, 0.0, 0.0)
        bpy.context.view_layer.update()
        cuerpo = bpy.data.objects["Espectador_%s_Cuerpo" % letra]
        me = cuerpo.data
        attr = me.color_attributes[0]
        print("Espectador %s (%s, dominio %s)" % (letra, attr.name, attr.domain))
        nuevos = []
        for i, slot in enumerate(cuerpo.material_slots):
            avg, dev = color_promedio(me, attr, i)
            if avg is None:
                nuevos.append(slot.material)
                continue
            print("   %-12s color=%s desviacion_max=%.4f" % (slot.material.name, tuple(round(c, 3) for c in avg), dev))
            nuevos.append(material_desde_color("%s_%s" % (slot.material.name, letra), avg))
        for i, mat in enumerate(nuevos):
            cuerpo.material_slots[i].material = mat
        mn, mx = bbox([cuerpo])
        print("   tamano cuerpo %s, min %s" % (tuple(round(v, 3) for v in (mx - mn)), tuple(round(v, 3) for v in mn)))
        export_obj(os.path.join(out_dir, "espectador_%s.obj" % letra), [cuerpo])

        if letra == "A":
            silla = bpy.data.objects["Espectador_A_Silla"]
            avg, dev = color_promedio(silla.data, silla.data.color_attributes[0], 0)
            silla.material_slots[0].material = material_desde_color("MAT_Silla_Espectador", avg)
            # Altura del asiento: cara horizontal más amplia por debajo del respaldo
            zs = {}
            for poly in silla.data.polygons:
                if abs(poly.normal.z) > 0.9:
                    z = round((silla.matrix_world @ poly.center).z, 3)
                    zs[z] = zs.get(z, 0.0) + poly.area
            print("   silla: caras horizontales (z: area) %s" % sorted(zs.items()))
            export_obj(os.path.join(out_dir, "espectador_silla.obj"), [silla])


# ---------------------------------------------------------------- Cámaras low-poly (GLB)
def convertir_camaras_glb():
    """El Assimp incluido en el proyecto (4.x) no reconoce archivos .glb; se separa cada cámara en un OBJ."""
    carpeta = os.path.join(BASE, "set-of-four-4-low-poly-cameras")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(carpeta, "source", "allCameras-Avatars-merged.glb"))

    # La paleta viene incrustada en el GLB; se enlaza al PNG equivalente de la carpeta textures/
    paleta = bpy.data.images.load(os.path.join(carpeta, "textures", "color-palette_0.png"))
    for mat in bpy.data.materials:
        if mat.node_tree:
            for n in mat.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image and n.image != paleta:
                    n.image = paleta

    salidas = {"broadcastCamera": "camara_broadcast", "videoCamera": "camara_video", "filmCamera": "camara_cine",
               "drone-rotor-wings": "camara_dron"}
    for prefijo, nombre in salidas.items():
        meshes = [ob for ob in bpy.context.scene.objects if ob.type == "MESH" and ob.name.startswith(prefijo)]
        if not meshes:
            print("  AVISO: no se encontro", prefijo)
            continue
        mn, mx = bbox(meshes)
        centro = (mn + mx) * 0.5
        # Se centra cada cámara en X/Y conservando su altura (apoyada en el suelo)
        for ob in meshes:
            ob.matrix_world.translation -= Vector((centro.x, centro.y, 0.0))
        bpy.context.view_layer.update()
        print("Camara %s: tamano %s" % (nombre, tuple(round(v, 3) for v in (mx - mn))))
        export_obj(os.path.join(carpeta, "source", nombre + ".obj"), meshes)


convertir_cinturon()
convertir_espectadores()
convertir_camaras_glb()
print("Conversion terminada")
