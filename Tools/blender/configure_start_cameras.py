"""One-time migration of shipped stages. Only adds camera helpers and JSON field."""
import sys
import json
from pathlib import Path
import bpy
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
e.register()
root = Path(__file__).resolve().parents[2]
for stage in ('stage01', 'stage02', 'stage03'):
    folder = root/'resources/levels'/stage
    bpy.ops.wm.open_mainfile(filepath=str(folder/f'{stage}.blend'))
    scene = bpy.context.scene
    points = [o for o in scene.objects if o.yan_level.role == 'CAMERA_POINT' and o.yan_level.camera_sequence == 'start']
    if not points:
        assert bpy.ops.yanengine.camera_default() == {'FINISHED'}
        points = [o for o in scene.objects if o.yan_level.role == 'CAMERA_POINT']
    if stage == 'stage03':
        # Room 308 is smaller than the outdoor prototype. Keep the whole arc
        # south of the bathroom wall, with margin for the camera near plane.
        from mathutils import Vector
        player, q = e.camera_player(scene)
        offsets = [(2.1,1.8,1.75),(2.1,1.8,1.75),(2.25,1.9,.5),(1.65,1.9,-.9),(.3,1.75,-1.1),(0,1.6,-.95),(0,1.6,0)]
        for obj in points:
            p = player+q@Vector(offsets[obj.yan_level.camera_order])
            obj.location = (e.ENGINE_BASIS.inverted()@p.to_4d()).to_3d()
        bpy.context.view_layer.update()
    sequences = e.camera_sequences(bpy.context.scene)
    data = json.loads((folder/f'{stage}.json').read_text(encoding='utf-8'))
    data['cameraSequences'] = sequences
    (folder/f'{stage}.json').write_text(json.dumps(data, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(folder/f'{stage}.blend'))
    print(f'Configured {stage}: {len(sequences[0]["points"])} points / 4s')
