"""Exercise the real Install from Disk / enable operators, not direct register().

Use isolated BLENDER_USER_RESOURCES so installation never touches user settings.
"""
import hashlib
from pathlib import Path
import sys

import addon_utils
import bpy

root = Path(__file__).resolve().parents[2]
source = root / 'tools/blender/yanengine_level_exporter.py'
blend = root / 'resources/levels/stage01/stage01.blend'
before = hashlib.sha256(blend.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(blend))

assert bpy.ops.preferences.addon_install(filepath=str(source), overwrite=True, enable_on_install=True) == {'FINISHED'}
if not addon_utils.check('yanengine_level_exporter')[1]:
    # Reinstalling an already present file is not considered a new module by Blender.
    assert bpy.ops.preferences.addon_enable(module='yanengine_level_exporter') == {'FINISHED'}
e = sys.modules['yanengine_level_exporter']
assert addon_utils.check(e.__name__) == (True, True)
assert e.bl_info['version'] == (1, 12, 0)
assert bpy.app.timers.is_registered(e.initialize_legacy_spawns)
# Export/validation must be safe even before the deferred callback has run.
data, _ = e.build_level(bpy.context.scene, bpy.context.evaluated_depsgraph_get())
assert not bpy.app.timers.is_registered(e.initialize_legacy_spawns)
assert len(data['spawnPoints']) == 9 and all('activation' not in p for p in data['spawnPoints'])
assert len(data['spawnTriggers']) == 2 and all('groupId' not in t for t in data['spawnTriggers'])

# Disabling before the callback runs cancels it; re-enabling must not duplicate it.
for _ in range(2):
    assert bpy.ops.preferences.addon_disable(module=e.__name__) == {'FINISHED'}
    assert not bpy.app.timers.is_registered(e.initialize_legacy_spawns)
    assert e.preserve_legacy_spawns not in bpy.app.handlers.load_post
    assert bpy.ops.preferences.addon_enable(module=e.__name__) == {'FINISHED'}
    assert bpy.app.timers.is_registered(e.initialize_legacy_spawns)
    assert bpy.app.handlers.load_post.count(e.preserve_legacy_spawns) == 1

# The deferred callback runs with ordinary data access once registration exits.
assert e.initialize_legacy_spawns() is None
bpy.ops.wm.open_mainfile(filepath=str(blend))
assert not bpy.app.timers.is_registered(e.initialize_legacy_spawns)
assert bpy.context.scene.objects['SP_A_01'].yan_level.spawn_activation == 'LEGACY'
assert bpy.context.scene.objects['Trigger_A'].yan_level.trigger_mode == 'LEGACY'

obj = bpy.data.objects.new('Registration_NewEnemy', None)
bpy.context.scene.collection.objects.link(obj)
obj.yan_level.role = 'ENEMY'
assert obj.yan_level.spawn_activation == 'StageStart'
bpy.ops.preferences.addon_disable(module=e.__name__)
bpy.ops.preferences.addon_enable(module=e.__name__)
data, _ = e.build_level(bpy.context.scene, bpy.context.evaluated_depsgraph_get())
assert next(p for p in data['spawnPoints'] if p['id'] == obj.name)['activation'] == 'StageStart'
assert hashlib.sha256(blend.read_bytes()).hexdigest() == before
print('PASS: real addon_install/enable under restricted data, deferred initialization, immediate validation, disable/re-enable cleanup, load_post legacy preservation, new defaults, original blend untouched')
