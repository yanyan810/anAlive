"""Exact mixed enemy counts through real Blender editing, save/reload and export."""
import hashlib
import json
import shutil
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e

root = Path(__file__).resolve().parents[2]
test_root = root / 'generated/spawn-composition-tests'
project = test_root / 'project'


def fingerprints():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
            for stage in ('stage01', 'stage02', 'stage03')
            for p in (root / 'resources/levels' / stage).rglob('*') if p.is_file()}


before = fingerprints()
e.register()
bpy.ops.wm.open_mainfile(filepath=str(root / 'resources/levels/stage01/stage01.blend'))
scene = bpy.context.scene
scene.yan_level.project_root = str(root)
for obj in scene.objects:
    if obj.yan_level.role in {'ENEMY', 'TRIGGER', 'SPAWN_GROUP'}:
        obj.yan_level.role = 'IGNORE'
obj = bpy.data.objects.new('CompositionPoint', None)
scene.collection.objects.link(obj)
bpy.context.view_layer.objects.active = obj
cfg = obj.yan_level
cfg.role = 'ENEMY'
cfg.spawn_mode = 'Simultaneous'
cfg.spawn_count = cfg.max_alive = 5
single, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
assert single['spawnPoints'][0]['enemy'] == 'normal' and 'enemyComposition' not in single['spawnPoints'][0]
cfg.spawn_composition_mode = 'COUNTS'
assert len(cfg.enemy_composition) == 1 and cfg.enemy_composition[0].count == 5
cfg.enemy_composition[0].count = 3
assert bpy.ops.yanengine.enemy_composition() == {'FINISHED'}
assert bpy.ops.yanengine.choose_pool(index=1, identifier='fast') == {'FINISHED'}
cfg.enemy_composition[1].count = 2
expected = [{'enemy': 'normal', 'count': 3}, {'enemy': 'fast', 'count': 2}]
data, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
point = data['spawnPoints'][0]
assert point['enemyComposition'] == expected and point['spawnCount'] == 5 and 'enemy' not in point
assert point['enemyPool'] == [{'id': 'normal', 'weight': 3}, {'id': 'fast', 'weight': 2}]
assert bpy.ops.yanengine.validate() == {'FINISHED'}
assert bpy.ops.yanengine.enemy_composition(index=1, move=-1) == {'FINISHED'}
assert e.composition_data(cfg, e.definitions(scene, 'enemies'), obj.name) == expected[::-1]
assert bpy.ops.yanengine.enemy_composition(index=0, move=1) == {'FINISHED'}
# Toggling the format retains both the existing single-type settings and the list.
cfg.spawn_composition_mode = 'SINGLE'
again, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
assert again['spawnPoints'] == single['spawnPoints']
cfg.spawn_composition_mode = 'COUNTS'
assert e.spawn_total(cfg) == 5 and len(cfg.enemy_composition) == 2


def rejected(change, restore):
    try:
        change()
        try:
            e.build_level(scene, bpy.context.evaluated_depsgraph_get())
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid composition accepted')
    finally:
        restore()


row = cfg.enemy_composition[0]
rejected(lambda: setattr(row, 'identifier', 'missing'), lambda: setattr(row, 'identifier', 'normal'))
rejected(lambda: row.__setitem__('count', 0), lambda: setattr(row, 'count', 3))
rejected(lambda: setattr(row, 'count', 10000), lambda: setattr(row, 'count', 3))
cfg.enemy_composition.clear()
try:
    e.build_level(scene, bpy.context.evaluated_depsgraph_get())
except ValueError:
    pass
else:
    raise AssertionError('Empty composition accepted')
for entry in expected:
    row = cfg.enemy_composition.add()
    row.identifier, row.count = entry['enemy'], entry['count']

scene.yan_level.project_root = str(project)
(project / 'resources/Data').mkdir(parents=True, exist_ok=True)
for name in ('enemies.json', 'weapons.json'):
    shutil.copyfile(root / 'resources/Data' / name, project / 'resources/Data' / name)
output = e.export_level(bpy.context)
assert json.loads((output / 'stage01.json').read_text(encoding='utf-8')) == data
blend = test_root / 'composition.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend))
bpy.ops.wm.open_mainfile(filepath=str(blend))
roundtrip, _ = e.build_level(bpy.context.scene, bpy.context.evaluated_depsgraph_get())
assert roundtrip == data
assert fingerprints() == before, 'Original stages changed'
print('PASS: Normal 3 + Fast 2 exact counts, computed total, definition selection, reorder/add/remove, single-type compatibility, validation, save/reload/export, original stages untouched')
