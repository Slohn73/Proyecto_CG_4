"""
Exporta a FBX la animación de un archivo .blend de Penta con la MISMA configuración con la que se
exportó Animacion_EntradaPenta.fbx, para que todas las animaciones compartan exactamente el mismo
esqueleto (nombres de huesos, ejes locales y pose de enlace) y se puedan reproducir sobre un solo
modelo.

Por qué hace falta: Animación_CaídaPenta.fbx es el FBX original de Mixamo (nodos $AssimpFbx$_PreRotation,
sin nodo "Armature"), mientras que Animacion_EntradaPenta.fbx salió del exportador de Blender. Los
espacios locales de los huesos son distintos, así que las claves de uno no sirven para el otro.
El .blend de la caída tiene el mismo armature (41 huesos, misma pose de reposo) con la acción de la
caída; al exportarlo igual que la entrada se obtiene un clip compatible sin retargeting.

El .blend NO se guarda: los cambios (escala del objeto Armature) solo existen en memoria.

Uso (desde luchaLibreCompuG/luchaLibreCompuG):
  blender -b --factory-startup "../../Animaciones_Penta/Animacion_CaidaPenta.blend" --python herramientas/exportar_animacion_penta.py -- ^
          modelos/modelos_importados/penta_animado/Penta_Caida.fbx --nombre Caida [--con-malla] [--escala 20]

  --con-malla : exporta también las mallas y texturas (por defecto solo el esqueleto y su animación)
  --escala S  : escala del objeto Armature durante la exportación (en la entrada es 20; en el .blend de
                la caída es 30 solo por encuadre de cámara). Es uniforme: no altera el movimiento.
  --nombre N  : nombre del clip dentro del FBX (queda como "Armature|N")
  --simplificar F : reducción de claves del exportador (0 = una clave por cuadro; la entrada usó 1.0)
"""
import sys
import bpy

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if not argv:
    raise SystemExit("Falta la ruta del FBX de salida")
salida = argv[0]
con_malla = "--con-malla" in argv
escala = float(argv[argv.index("--escala") + 1]) if "--escala" in argv else 20.0
simplificar = float(argv[argv.index("--simplificar") + 1]) if "--simplificar" in argv else 0.0

arm = bpy.data.objects["Armature"]
accion = arm.animation_data.action
if "--nombre" in argv:
    accion.name = argv[argv.index("--nombre") + 1]
inicio, fin = (int(round(v)) for v in accion.frame_range)
print(f"[exportar] acción '{accion.name}' cuadros {inicio}-{fin}, escala Armature {tuple(arm.scale)} -> {escala}")

# Solo se exporta la acción activa: las demás acciones del archivo (restos de otras importaciones)
# se quitan de la sesión en memoria
for a in list(bpy.data.actions):
    if a != accion:
        bpy.data.actions.remove(a)

arm.scale = (escala, escala, escala)

for o in bpy.data.objects:
    o.select_set(o == arm or (con_malla and o.type == "MESH" and o.parent == arm))
bpy.context.view_layer.objects.active = arm

bpy.ops.export_scene.fbx(
    filepath=salida,
    use_selection=True,
    object_types={"ARMATURE", "MESH"} if con_malla else {"ARMATURE"},
    apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_NONE",  # Igual que la entrada: unidades (x100) horneadas en los nodos -> Armature escala 2000
    global_scale=1.0,
    axis_forward="-Z",
    axis_up="Y",
    add_leaf_bones=True,                   # La entrada trae los nodos hoja "_end" (no deforman ni se animan)
    primary_bone_axis="Y",
    secondary_bone_axis="X",
    use_armature_deform_only=False,
    bake_anim=True,
    bake_anim_use_all_bones=True,
    bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=True,        # Clip con nombre "Armature|<acción>" (solo queda la acción activa)
    bake_anim_force_startend_keying=True,
    bake_anim_step=1.0,
    bake_anim_simplify_factor=simplificar,
    path_mode="COPY",
    embed_textures=con_malla,
)
print(f"[exportar] escrito {salida}")
