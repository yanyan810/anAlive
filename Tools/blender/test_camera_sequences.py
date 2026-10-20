"""Blender integration + engine-space reference samples; source stages never saved."""
import sys
import json
import math
import hashlib
import shutil
from pathlib import Path
import bpy
from mathutils import Vector, Euler
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
e.register()
root = Path(__file__).resolve().parents[2]
out = root/'generated/camera-tests'
out.mkdir(parents=True, exist_ok=True)
for stage in ('stage01', 'stage02', 'stage03'):
    source = root/'resources/levels'/stage/f'{stage}.blend'
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    scene.yan_level.project_root = str(root)
    sequences = e.camera_sequences(scene)
    seq = next(s for s in sequences if s['id'] == 'start')
    assert len(seq['points']) == 7 and abs(sum(p['duration'] for p in seq['points'])-4) < 1e-6
    data, geometry = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
    assert data['cameraSequences'] == sequences and all(o.yan_level.role == 'STATIC' for o in geometry)
    player, rotation = e.camera_player(scene)
    samples = []
    for i in range(241):
        seconds = 4*i/240
        pos, quat, fov = e.camera_sample(seq, seconds, player, rotation)
        # Camera forward / up transformed back to engine equals runtime orientation.
        camera = e.camera_preview_matrix(pos, quat)
        forward = e.ENGINE_BASIS.to_3x3() @ (camera.to_3x3() @ Vector((0,0,-1)))
        assert (forward-quat@Vector((0,0,1))).length < 1e-5, (stage,i,tuple(forward),tuple(quat@Vector((0,0,1))),tuple(quat))
        assert camera.to_3x3().determinant() > .999
        samples.append(dict(time=seconds, position=list(pos), rotation=list(quat.to_euler('XYZ')), fov=fov))
    (out/f'{stage}-samples.json').write_text(json.dumps(samples), encoding='utf-8')
    assert (Vector(samples[-1]['position'])-(player+Vector((0,1.6,0)))).length < 1e-5
    # Export is atomic and isolated. Only generate a temporary test project.
    test_root = out/'export-project'
    (test_root/'resources/Data').mkdir(parents=True, exist_ok=True)
    for name in ('enemies.json','weapons.json'):
        shutil.copyfile(root/'resources/Data'/name, test_root/'resources/Data'/name)
    scene.yan_level.project_root = str(test_root)
    scene.yan_level.output_directory = f'resources/levels/{stage}'
    saved_camera = scene.camera
    if stage == 'stage01':
        export = e.export_level(bpy.context)
        exported = json.loads((export/f'{stage}.json').read_text())
        assert exported['cameraSequences'] == sequences and scene.camera == saved_camera
        view = next(a for a in bpy.context.screen.areas if a.type == 'VIEW_3D')
        space = view.spaces.active
        perspective = space.region_3d.view_perspective
        matrix = space.region_3d.view_matrix.copy()
        count = len(bpy.data.objects)
        with bpy.context.temp_override(area=view):
            assert bpy.ops.yanengine.camera_preview() == {'RUNNING_MODAL'}
            assert space.region_3d.view_perspective == 'CAMERA'
            assert scene.camera == saved_camera  # local viewport camera only
            for seconds in (0,1,2.5,3.5,4):
                scene.yan_level.camera_preview_time = seconds
                pos, q, fov = e.camera_sample(seq, seconds, player, rotation)
                assert (e._camera_preview['camera'].matrix_world.translation-e.camera_preview_matrix(pos,q).translation).length < 1e-5
            assert bpy.ops.yanengine.camera_stop() == {'FINISHED'}
            assert len(bpy.data.objects) == count and space.region_3d.view_perspective == perspective
            assert max(abs(matrix[i][j]-space.region_3d.view_matrix[i][j]) for i in range(4) for j in range(4)) < 1e-4
            assert bpy.ops.yanengine.camera_preview() == {'RUNNING_MODAL'}
            assert bpy.ops.yanengine.camera_preview() == {'RUNNING_MODAL'}
            assert len(bpy.data.objects) == count+1
            e.camera_stop()
        points = sorted((o for o in scene.objects if o.yan_level.role == 'CAMERA_POINT'), key=lambda o:o.yan_level.camera_order)
        c = points[2].yan_level
        def reject(change, restore):
            change()
            try:
                e.camera_sequences(scene)
            except ValueError:
                pass
            else:
                raise AssertionError('Invalid camera data accepted')
            finally:
                restore()
        reject(lambda:setattr(c,'camera_order',0), lambda:setattr(c,'camera_order',2))
        reject(lambda:setattr(c,'camera_duration',0), lambda:setattr(c,'camera_duration',.7))
        reject(lambda:setattr(c,'camera_player_eye',True), lambda:setattr(c,'camera_player_eye',False))
        reject(lambda:setattr(c,'camera_rotation_mode','LookAtTarget'), lambda:setattr(c,'camera_rotation_mode','LookAtPlayer'))
        last = points[-1].yan_level
        reject(lambda:setattr(last,'camera_player_eye',False), lambda:setattr(last,'camera_player_eye',True))
        # Explicit Manual roll and target tracking are sampled for C++ comparison too.
        c.camera_rotation_mode = 'Manual'; points[2].rotation_euler = Euler((.3,.4,.7))
        points[3].yan_level.camera_rotation_mode = 'LookAtTarget'
        points[3].yan_level.camera_target = next(o for o in scene.objects if o.yan_level.role == 'PLAYER')
        bpy.context.view_layer.update()
        seq2 = e.camera_sequences(scene)[0]
        mixed = []
        for i in range(161):
            seconds=4*i/160; pos,q,fov=e.camera_sample(seq2,seconds,player,rotation)
            mixed.append(dict(time=seconds,position=list(pos),rotation=list(q.to_euler('XYZ')),fov=fov))
        (out/'mixed-reference.json').write_text(json.dumps(dict(cameraSequences=[seq2],samples=mixed)),encoding='utf-8')
        # Save/reload a COPY, verifying RNA and target references persist.
        copy = out/'camera-roundtrip.blend'
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(copy), copy=True)
        bpy.ops.wm.open_mainfile(filepath=str(copy))
        assert e.camera_sequences(bpy.context.scene) == [seq2]
    assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
e.unregister()
assert e.camera_cleanup not in bpy.app.handlers.load_pre and e._camera_preview is None
print('PASS: camera points/settings/validation/export, engine parity reference, manual/target rotation, preview/seek/stop/restart and viewport restoration, save/reload copy, unregister cleanup; source blend untouched')
