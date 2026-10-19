"""Real Blender RNA / saved legacy stages / independent-wave export, generated only."""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e

root = Path(__file__).resolve().parents[2]
test_root = root / 'generated/spawn-activation-tests'
e.register()


def fingerprints():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
            for stage in ('stage01', 'stage02', 'stage03')
            for p in (root / 'resources/levels' / stage).rglob('*') if p.is_file()}


before = fingerprints()
for stage in ('stage01', 'stage02', 'stage03'):
    # Use committed legacy fixtures: the user's working hotel may deliberately
    # be mid-edit (e.g. an unlinked trigger) and must never be rewritten for tests.
    fixture = test_root / 'legacy-fixtures' / f'{stage}.blend'
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_bytes(subprocess.run(['git', 'show', f'HEAD:resources/levels/{stage}/{stage}.blend'],
        cwd=root, check=True, stdout=subprocess.PIPE).stdout)
    bpy.ops.wm.open_mainfile(filepath=str(fixture))
    scene = bpy.context.scene
    scene.yan_level.project_root = str(root)
    baseline, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
    assert all('activation' not in p for p in baseline['spawnPoints'])
    assert all('groupId' not in t for t in baseline['spawnTriggers'])
    project = test_root / 'compatibility' / stage
    (project / 'resources/Data').mkdir(parents=True, exist_ok=True)
    for name in ('enemies.json', 'weapons.json'):
        shutil.copyfile(root / 'resources/Data' / name, project / 'resources/Data' / name)
    scene.yan_level.project_root = str(project)
    scene.yan_level.output_directory = f'resources/levels/{stage}'
    output = e.export_level(bpy.context)
    assert json.loads((output / f'{stage}.json').read_text(encoding='utf-8')) == baseline

bpy.ops.wm.open_mainfile(filepath=str(root / 'resources/levels/stage01/stage01.blend'))
scene = bpy.context.scene
for obj in scene.objects:
    if obj.yan_level.role in {'ENEMY', 'TRIGGER', 'SPAWN_GROUP'}:
        obj.yan_level.role = 'IGNORE'
project = test_root / 'project'
(project / 'resources/Data').mkdir(parents=True, exist_ok=True)
for name in ('enemies.json', 'weapons.json'):
    shutil.copyfile(root / 'resources/Data' / name, project / 'resources/Data' / name)
scene.yan_level.project_root = str(project)


def point(name, activation, count, mode, enemy='normal'):
    obj = bpy.data.objects.new(name, None)
    scene.collection.objects.link(obj)
    cfg = obj.yan_level
    cfg.role = 'ENEMY'
    assert cfg.spawn_activation == 'StageStart' and cfg.is_property_set('spawn_activation')
    cfg.spawn_activation, cfg.enemy_definition = activation, enemy
    cfg.spawn_count, cfg.max_alive, cfg.spawn_mode = count, count, mode
    cfg.spawn_interval = .5
    return obj


a = point('Independent_A', 'OnTrigger', 3, 'Simultaneous')
b = point('Independent_B', 'OnTrigger', 5, 'Sequential', 'fast')
c = point('Independent_C', 'StageStart', 2, 'Simultaneous', 'tank')
d = point('Independent_D', 'Manual', 3, 'Sequential')
d.yan_level.initial_delay, d.yan_level.max_alive = 1, 1
trigger = bpy.data.objects.new('Independent_Trigger', None)
scene.collection.objects.link(trigger)
trigger.empty_display_type = 'CUBE'
trigger.yan_level.role = 'TRIGGER'
assert trigger.yan_level.trigger_mode == 'GROUP' and trigger.yan_level.is_property_set('trigger_mode')
trigger.location = (50, 0, 0)
bpy.context.view_layer.update()
data, _ = e.build_level(scene, bpy.context.evaluated_depsgraph_get())
assert [p['spawnCount'] for p in data['spawnPoints']] == [3, 5, 2, 3]
assert data['spawnTriggers'][0] == dict(id=trigger.name, position=[-50, 0, 0], size=[2, 2, 2],
    groupId='A', condition='PlayerEnter', initialDelay=0, oneShot=True)
assert 'groupId' not in data['spawnPoints'][2] and 'groupId' not in data['spawnPoints'][3]
assert bpy.ops.yanengine.validate() == {'FINISHED'}


def rejected(change, restore):
    try:
        change()
        bpy.context.view_layer.update()
        try:
            e.build_level(scene, bpy.context.evaluated_depsgraph_get())
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid independent spawn accepted')
    finally:
        restore()


cfg = a.yan_level
rejected(lambda: setattr(cfg, 'identifier', b.name), lambda: setattr(cfg, 'identifier', ''))
rejected(lambda: setattr(cfg, 'enemy_definition', 'missing'), lambda: setattr(cfg, 'enemy_definition', 'normal'))
rejected(lambda: setattr(cfg, 'group', ''), lambda: setattr(cfg, 'group', 'A'))
for field, good, invalid in [('spawn_count', 3, 0), ('max_alive', 3, 0),
                              ('spawn_interval', .5, -1.0), ('initial_delay', 0.0, -1.0)]:
    # Create the typed RNA storage before injecting a corrupt saved value.
    setattr(cfg, field, good)
    rejected(lambda f=field, v=invalid: cfg.__setitem__(f, v),
             lambda f=field, v=good: setattr(cfg, f, v))
rejected(lambda: setattr(trigger.yan_level, 'group', 'missing'), lambda: setattr(trigger.yan_level, 'group', 'A'))
rejected(lambda: setattr(trigger.yan_level, 'trigger_mode', 'LEGACY'), lambda: setattr(trigger.yan_level, 'trigger_mode', 'GROUP'))

# Real definition menu operator writes a persistent ID string, rather than an enum index.
bpy.context.view_layer.objects.active = a
assert bpy.ops.yanengine.choose_pool(index=-1, identifier='ranged') == {'FINISHED'}
assert cfg.enemy_definition == 'ranged'
cfg.enemy_definition = 'normal'
output = e.export_level(bpy.context)
notes = output / 'authoring.txt'
notes.write_text('keep author files', encoding='utf-8')
previous = Path.cwd()
try:
    e.os.chdir(output)
    assert bpy.ops.yanengine.export() == {'FINISHED'}
    assert Path.cwd() == output and notes.read_text(encoding='utf-8') == 'keep author files'
finally:
    e.os.chdir(previous)
blend = test_root / 'independent.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend))
bpy.ops.wm.open_mainfile(filepath=str(blend))
roundtrip, _ = e.build_level(bpy.context.scene, bpy.context.evaluated_depsgraph_get())
assert roundtrip == data
assert json.loads((output / 'stage01.json').read_text(encoding='utf-8')) == data
assert fingerprints() == before, 'Original stage assets were modified'
assert e.YAN_ObjectSettings.bl_rna.properties['spawn_count'].name == '総出現数'
assert e.YAN_ObjectSettings.bl_rna.properties['spawn_activation'].name == '起動方法'
print('PASS: Stage01-03 legacy export unchanged, Japanese RNA, independent activation, validation, menu, save/reload, export rollback-safe directory, original files untouched')
