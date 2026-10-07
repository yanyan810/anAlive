"""SpawnGroup authoring, persistence and export regression; writes generated/ only."""
import sys
import json
import shutil
from pathlib import Path
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e

root = Path(__file__).resolve().parents[2]
test_root = root / 'generated/spawn-group-tests'
e.register()
bpy.ops.wm.open_mainfile(filepath=str(root / 'resources/levels/stage01/stage01.blend'))
scene = bpy.context.scene
scene.yan_level.project_root = str(root)
baseline, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
assert baseline['spawnGroups'] == [] and len(baseline['spawnTriggers']) == 2
points = [scene.objects[f'SP_A_{i:02}'] for i in range(1, 4)]
catalog = e.definitions(scene, 'enemies')

def group(name, mode, time):
    obj = bpy.data.objects.new(name, None)
    scene.collection.objects.link(obj)
    cfg = obj.yan_level
    cfg.role = 'SPAWN_GROUP'
    cfg.spawn_mode, cfg.start_time, cfg.spawn_interval = mode, time, .5
    for identifier, point in zip(('normal', 'fast', 'tank'), points):
        entry = cfg.members.add()
        entry.identifier, entry.spawn_point = identifier, point
    return obj

simultaneous = group('Group_A', 'Simultaneous', 3)
sequential = group('Group_B', 'Sequential', 4)
data, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
assert data['spawnTriggers'] == baseline['spawnTriggers'] and data['spawnPoints'] == baseline['spawnPoints']
assert data['spawnGroups'][0] == {'id': 'Group_A', 'time': 3, 'mode': 'Simultaneous', 'interval': .5,
    'enemies': [{'enemy': key, 'spawnPoint': e.object_id(point)} for key, point in zip(('normal', 'fast', 'tank'), points)]}
assert data['spawnGroups'][1]['mode'] == 'Sequential'
# Adding a new role must retain serialized enum indices and Sequential defaults.
blank = bpy.data.objects.new('DefaultGroup', None)
scene.collection.objects.link(blank)
blank.yan_level.role = 'SPAWN_GROUP'
assert blank.yan_level.spawn_mode == 'Sequential' and blank.yan_level.start_time == 0
try:
    e.build_level(scene, bpy.context.evaluated_depsgraph_get())
except ValueError:
    pass
else:
    raise AssertionError('Empty group accepted')
bpy.data.objects.remove(blank, do_unlink=True)

def rejected(change, restore):
    change()
    try:
        e.build_level(scene, bpy.context.evaluated_depsgraph_get())
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid group accepted')
    finally:
        restore()

cfg = simultaneous.yan_level
member = cfg.members[0]
rejected(lambda: setattr(member, 'identifier', 'missing'), lambda: setattr(member, 'identifier', 'normal'))
rejected(lambda: setattr(member, 'spawn_point', None), lambda: setattr(member, 'spawn_point', points[0]))
rejected(lambda: setattr(points[0].yan_level, 'role', 'IGNORE'), lambda: setattr(points[0].yan_level, 'role', 'ENEMY'))
rejected(lambda: setattr(cfg, 'identifier', e.object_id(points[0])), lambda: setattr(cfg, 'identifier', ''))
other_scene = bpy.data.scenes.new('OtherScene')
other_point = bpy.data.objects.new('OtherPoint', None)
other_scene.collection.objects.link(other_point)
other_point.yan_level.role = 'ENEMY'
rejected(lambda: setattr(member, 'spawn_point', other_point), lambda: setattr(member, 'spawn_point', points[0]))
bpy.data.objects.remove(other_point, do_unlink=True)
bpy.data.scenes.remove(other_scene)
assert e.spawn_point_poll(member, points[0]) and not e.spawn_point_poll(member, simultaneous)

# Reuse the existing enemy definition picker and exercise member order controls.
bpy.context.view_layer.objects.active = simultaneous
assert bpy.ops.yanengine.choose_pool(index=0, identifier='ranged') == {'FINISHED'}
assert member.identifier == 'ranged' and not cfg.pool  # picker edits the member, not the weighted point pool
assert bpy.ops.yanengine.choose_pool(index=0, identifier='normal') == {'FINISHED'}
assert bpy.ops.yanengine.group_member(index=0, move=1) == {'FINISHED'}
assert [row.identifier for row in cfg.members] == ['fast', 'normal', 'tank']
assert bpy.ops.yanengine.group_member(index=1, move=-1) == {'FINISHED'}
assert bpy.ops.yanengine.group_member() == {'FINISHED'}
assert len(cfg.members) == 4 and cfg.members[-1].identifier in catalog and cfg.members[-1].spawn_point in scene.objects[:]
assert bpy.ops.yanengine.group_member(index=3) == {'FINISHED'}
assert len(cfg.members) == 3

project = test_root / 'project'
(project / 'resources/Data').mkdir(parents=True, exist_ok=True)
for name in ('enemies.json', 'weapons.json'):
    shutil.copyfile(root / 'resources/Data' / name, project / 'resources/Data' / name)
scene.yan_level.project_root = str(project)
scene.yan_level.output_directory = 'resources/levels/stage01'
out = e.export_level(bpy.context)
exported = json.loads((out / 'stage01.json').read_text(encoding='utf-8'))
assert exported == data
gltf = json.loads((out / 'stage01.gltf').read_text(encoding='utf-8'))
assert not any(node.get('name', '').startswith('Group_') for node in gltf['nodes'])
saved = test_root / 'spawn-groups.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(saved))
bpy.ops.wm.open_mainfile(filepath=str(saved))
scene = bpy.context.scene
assert scene.objects['Group_A'].yan_level.spawn_mode == 'Simultaneous'
assert scene.objects['Group_B'].yan_level.spawn_mode == 'Sequential'
assert scene.objects['Group_A'].yan_level.members[0].spawn_point == scene.objects['SP_A_01']
restored, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
assert restored == exported
e.unregister()
e.register()
restored, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
assert restored == exported
print('BLENDER_SPAWN_GROUP_TESTS_PASSED: legacy triggers, modes/timing, explicit definitions/points, catalog picker, member add/remove/order, validation, glTF exclusion, save/reopen, registration')
