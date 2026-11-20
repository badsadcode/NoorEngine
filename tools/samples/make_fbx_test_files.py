"""FBX test files and their references, made by Blender (Milestone 8C, part 7).

Run inside Blender (any version from 3.3 on):

  blender -b --factory-startup -P tools/samples/make_fbx_test_files.py -- export <in.glb> <out.fbx> <out.ref.glb> [exact] [clips]
      imports a glTF and, from that one Blender scene, writes an FBX (Blender's exporter, its default settings:
      leaf bones, -Z forward / Y up, baked animation with its curve simplification) and a GLB (every action).
      The engine's FBX import of the one is compared with its glTF import of the other.

  blender -b --factory-startup -P tools/samples/make_fbx_test_files.py -- reference <in.fbx> <out.ref.glb>
      Blender's own FBX importer reads a third-party FBX and writes it out as a GLB: a second, independent reading
      of the file to compare the engine's with (used for files that can't be committed).

The scene runs at 60 frames a second, so keys aren't thinned to 24.
"""
import os
import sys

import bpy


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 60
    bpy.context.scene.render.fps_base = 1.0


def options(op, wanted):
    """Only the keyword arguments this Blender's operator has (they change between versions)."""
    have = {p.identifier for p in op.get_rna_type().properties}
    return {k: v for k, v in wanted.items() if k in have}


def frame_range_from_actions():
    acts = list(bpy.data.actions)
    if not acts:
        return
    start = min(a.frame_range[0] for a in acts)
    end = max(a.frame_range[1] for a in acts)
    bpy.context.scene.frame_start = int(start)
    bpy.context.scene.frame_end = int(max(end, start + 1))


def rest_pose_everywhere():
    for ob in bpy.data.objects:
        if ob.type != "ARMATURE":
            continue
        if ob.animation_data:
            ob.animation_data.action = None
        for pb in ob.pose.bones:
            pb.location = (0, 0, 0)
            pb.rotation_quaternion = (1, 0, 0, 0)
            pb.rotation_euler = (0, 0, 0)
            pb.scale = (1, 1, 1)
    bpy.context.view_layer.update()


def export_glb(path):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    op = bpy.ops.export_scene.gltf
    op(**options(op, dict(filepath=path, export_format="GLB", export_animations=True, export_animation_mode="ACTIONS",
                          export_force_sampling=True, export_nla_strips=True, export_frame_range=False, export_skins=True,
                          export_morph=False, export_apply=False, export_yup=True)))


def main(argv):
    mode = argv[0]
    reset()
    if mode == "export":
        src, fbx, ref = argv[1:4]
        op = bpy.ops.import_scene.gltf
        op(**options(op, dict(filepath=src)))
        # Blender's glTF importer draws bones with a shape object ("Icosphere"); the FBX exporter would write it out
        # and the glTF exporter wouldn't: not part of the character.
        shapes = {pb.custom_shape for ob in bpy.data.objects if ob.type == "ARMATURE" for pb in ob.pose.bones if pb.custom_shape}
        for ob in shapes:
            bpy.data.objects.remove(ob, do_unlink=True)
        frame_range_from_actions()
        # Blender's FBX exporter keys every bone in every take ("Key All Bones", on by default for Unreal): a bone an
        # action doesn't move gets the pose it was last left in. Start from the rest pose, so that pose is the rest.
        rest_pose_everywhere()
        os.makedirs(os.path.dirname(os.path.abspath(fbx)), exist_ok=True)
        # Blender's defaults otherwise; "exact" turns off its curve simplification.
        # "clips" writes the armature alone (a clip file, as FBX packs ship them beside the character).
        extra = argv[4:]
        kw = {"bake_anim_simplify_factor": 0.0} if "exact" in extra else {}
        if "clips" in extra:
            kw["object_types"] = {"ARMATURE"}
        bpy.ops.export_scene.fbx(filepath=fbx, bake_anim=True, **kw)
        export_glb(ref)
    elif mode == "reference":
        src, ref = argv[1:3]
        op = bpy.ops.import_scene.fbx
        op(**options(op, dict(filepath=src, automatic_bone_orientation=False, ignore_leaf_bones=False, use_anim=True)))
        frame_range_from_actions()
        export_glb(ref)
    else:
        raise SystemExit("mode: export or reference")
    print("made", argv[1:], "with Blender", bpy.app.version_string)


main(sys.argv[sys.argv.index("--") + 1:])
