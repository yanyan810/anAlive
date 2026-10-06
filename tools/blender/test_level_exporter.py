"""Headless integration checks; never operates the Blender or game UI."""
import sys
from pathlib import Path
import json
import math
import hashlib
import struct
import shutil
import bpy
from mathutils import Matrix, Vector, Euler
sys.path.insert(0,str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
root=Path(__file__).resolve().parents[2]
e.register()
bpy.ops.wm.open_mainfile(filepath=str(root/'resources/levels/stage01/stage01.blend'))
ctx=bpy.context
scene=ctx.scene
assert scene.yan_level.stage_id=='stage01' and scene.objects['SP_A_03'].yan_level.pool[0].identifier=='fast'
scene.yan_level.project_root=str(root)
ctx.view_layer.update()
valid, geometry=e.build_level(scene,ctx.evaluated_depsgraph_get())
assert len(geometry)==9 and len(valid['colliders'])==8
assert len(valid['spawnPoints'])==9 and len(valid['spawnTriggers'])==2
assert valid['spawnTriggers'][0]['spawnPointIds']==[f'SP_A_{i:02}' for i in range(1,6)]
assert valid['playerSpawn']['position']==[3,0,-6]


def rejected(change, restore):
    try:
        change(); ctx.view_layer.update()
        try:
            e.build_level(scene,ctx.evaluated_depsgraph_get())
        except (ValueError, TypeError):
            pass
        else:
            raise AssertionError('Invalid authoring data accepted')
    finally:
        restore(); ctx.view_layer.update()


player=scene.objects['PlayerSpawn']; enemy=scene.objects['SP_A_01']; trigger=scene.objects['Trigger_A']
rejected(lambda:setattr(player.yan_level,'role','IGNORE'),lambda:setattr(player.yan_level,'role','PLAYER'))
rejected(lambda:setattr(enemy.yan_level,'role','PLAYER'),lambda:setattr(enemy.yan_level,'role','ENEMY'))
rejected(lambda:setattr(enemy.yan_level,'identifier','PlayerSpawn'),lambda:setattr(enemy.yan_level,'identifier',''))
rejected(lambda:setattr(trigger.yan_level,'group','missing'),lambda:setattr(trigger.yan_level,'group','A'))
rejected(lambda:setattr(enemy.yan_level.pool[0],'weight',0),lambda:setattr(enemy.yan_level.pool[0],'weight',1))
rejected(lambda:setattr(enemy.yan_level.pool[0],'identifier','missing'),lambda:setattr(enemy.yan_level.pool[0],'identifier','normal'))
weapon=scene.objects['WeaponSpawn_01']; weights=[p.weight for p in weapon.yan_level.pool]
rejected(lambda:[setattr(p,'weight',0) for p in weapon.yan_level.pool],lambda:[setattr(p,'weight',w) for p,w in zip(weapon.yan_level.pool,weights)])
goal=scene.objects['Goal_Main']; oldscale=goal.scale.copy()
rejected(lambda:setattr(goal,'scale',(0,1,1)),lambda:setattr(goal,'scale',oldscale))
oldrotation=trigger.rotation_euler.copy()
rejected(lambda:setattr(trigger,'rotation_euler',(.3,.2,0)),lambda:setattr(trigger,'rotation_euler',oldrotation))
wall=scene.objects['Wall_Rotated']; oldscale=wall.scale.copy()
rejected(lambda:setattr(wall,'scale',(0,1,1)),lambda:setattr(wall,'scale',oldscale))
building=scene.objects['Building_Custom']; custom=building.yan_level.custom_collider
rejected(lambda:setattr(building.yan_level,'custom_collider',None),lambda:setattr(building.yan_level,'custom_collider',custom))
for value in [float('nan'),float('inf')]:
    try: e.finite([value],'test')
    except ValueError: pass
    else: raise AssertionError('Nonfinite accepted')
# General Euler rotation, parent transform and nonuniform scale: local/world paths agree.
parent=Matrix.LocRotScale(Vector((2,4,-7)),Euler((.2,.4,-.3)).to_quaternion(),Vector((2,2,2)))
child=Matrix.LocRotScale(Vector((3,-1,2)),Euler((-.3,.1,.5)).to_quaternion(),Vector((1,3,2)))
trs,m=e.engine_transform(parent@child)
rebuilt=Matrix.LocRotScale(Vector(trs['position']),Euler(trs['rotation'],'XYZ').to_quaternion(),Vector(trs['scale']))
for point in [Vector((1,2,3,1)),Vector((-2,1,-1,1))]:
    expected=e.ENGINE_BASIS@(parent@child)@point
    actual=rebuilt@(e.ENGINE_BASIS@point)
    assert (expected-actual).length<1e-4
# Shear is explicitly rejected rather than silently decomposed incorrectly.
shear=Matrix.Identity(4); shear[0][1]=.5
try: e.engine_transform(shear)
except ValueError: pass
else: raise AssertionError('Shear accepted')

test_root=root/'generated/blender-tests/project'
(test_root/'resources/Data').mkdir(parents=True,exist_ok=True)
for name in ('enemies.json','weapons.json'):
    shutil.copyfile(root/'resources/Data'/name,test_root/'resources/Data'/name)
scene.yan_level.project_root=str(test_root)
scene.yan_level.output_directory='resources/levels/stage01'
assert bpy.ops.yanengine.validate()=={'FINISHED'}
selected=list(ctx.selected_objects); source_scene=ctx.window.scene
out=e.export_level(ctx)
assert ctx.window.scene==source_scene and list(ctx.selected_objects)==selected
output=json.loads((out/'stage01.json').read_text())
gltf=json.loads((out/'stage01.gltf').read_text())
assert len(gltf['meshes'])==9
assert not any(n.get('name','').startswith(('COL_','SP_','Trigger_','Goal_','PlayerSpawn','WeaponSpawn')) for n in gltf['nodes'])
# Exported mesh bounds equal engine JSON collider bounds, including the rotated wall.
for collider in output['colliders']:
    name='Building_Custom' if collider['id']=='COL_Building' else collider['id']
    node=next(n for n in gltf['nodes'] if n.get('name','').startswith(name))
    accessor=gltf['accessors'][gltf['meshes'][node['mesh']]['primitives'][0]['attributes']['POSITION']]
    a,b=accessor['min'],accessor['max']; mesh_min=Vector((-b[0],a[1],a[2])); mesh_max=Vector((-a[0],b[1],b[2]))
    matrix=Matrix.LocRotScale(Vector(collider['position']),Euler(collider['rotation'],'XYZ').to_quaternion(),Vector(collider['scale']))
    low,high=collider['localBounds']['min'],collider['localBounds']['max']
    corners=[matrix@Vector((x,y,z,1)) for x in (low[0],high[0]) for y in (low[1],high[1]) for z in (low[2],high[2])]
    for i in range(3):
        assert abs(mesh_min[i]-min(p[i] for p in corners))<1e-4
        assert abs(mesh_max[i]-max(p[i] for p in corners))<1e-4

# Blender's file browser may leave its process working directory inside the output.
# Windows then forbids renaming that directory until the exporter releases its lock.
original_directory = Path.cwd()
authoring = out/'authoring'
authoring.mkdir(exist_ok=True)
author_file = authoring/'notes.txt'
author_file.write_text('Preserve unrelated author files', encoding='utf-8')
try:
    for working_directory in (out, authoring):
        e.os.chdir(working_directory)
        e.export_level(ctx)
        assert Path.cwd() == working_directory
        assert author_file.read_text(encoding='utf-8') == 'Preserve unrelated author files'
        assert ctx.window.scene == source_scene
finally:
    e.os.chdir(original_directory)


def fingerprints():
    return {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
before=fingerprints()
# Invalid export never touches the previous files.
trigger.yan_level.group='missing'
try: e.export_level(ctx)
except ValueError: pass
else: raise AssertionError('Invalid export wrote files')
trigger.yan_level.group='A'
assert before==fingerprints()
# Simulate failure in the publication rename, and verify complete restoration.
original=e.os.replace
count=0

def fail_second(source,destination):
    global count
    count+=1
    if count==2: raise OSError('injected publication failure')
    return original(source,destination)
e.os.replace=fail_second
try:
    e.os.chdir(authoring)
    try: e.export_level(ctx)
    except OSError: pass
    else: raise AssertionError('Expected injected failure')
    assert Path.cwd() == authoring
    assert author_file.read_text(encoding='utf-8') == 'Preserve unrelated author files'
finally:
    e.os.replace=original
    e.os.chdir(original_directory)
assert before==fingerprints() and ctx.window.scene==source_scene
# Verify .blend property persistence and registration lifecycle.
blend=root/'generated/blender-tests/properties.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend))
bpy.ops.wm.open_mainfile(filepath=str(blend))
assert bpy.context.scene.objects['SP_A_03'].yan_level.pool[0].identifier=='fast'
e.unregister(); e.register()

# Filter export and legacy pool IDs use real Blender RNA, not mock properties.
scene=bpy.context.scene
cfg=scene.objects['WeaponSpawn_01'].yan_level
original_pool=[(p.identifier,p.weight) for p in cfg.pool]
cfg.pool.clear()
cfg.weapon_slot='Main'; cfg.min_rarity='2'; cfg.max_rarity='3'
cfg.weapon_types.add().name='Shotgun'
weapons=e.definitions(scene,'weapons')
matches=e.matched_weapons(cfg,weapons)
expected=[key for key,w in weapons.items() if w['slot']=='Main' and 2<=w['rarity']<=3 and w['type']=='Shotgun']
assert [p['id'] for p in matches]==expected and expected
level,_=e.build_level(scene,bpy.context.evaluated_depsgraph_get())
point=next(p for p in level['weaponSpawnPoints'] if p['id']=='WeaponSpawn_01')
assert 'weaponPool' not in point
assert point['filter']=={'slot':'Main','minRarity':2,'maxRarity':3,'types':['Shotgun']}
for identifier,weight in [('pistol',100),(expected[0],7)]:
    row=cfg.pool.add(); row.identifier=identifier; row.weight=weight
assert e.matched_weapons(cfg,weapons)==[{'id':expected[0],'weight':7}]
rejected(lambda:setattr(cfg.pool[1],'weight',0),lambda:setattr(cfg.pool[1],'weight',7))
rejected(lambda:setattr(cfg,'min_rarity','5'),lambda:setattr(cfg,'min_rarity','2'))
rejected(lambda:setattr(cfg,'weapon_slot','Sub'),lambda:setattr(cfg,'weapon_slot','Main'))
rejected(lambda:setattr(cfg.pool[0],'identifier','deleted_weapon'),lambda:setattr(cfg.pool[0],'identifier','pistol'))

# JSON additions/reordering/deletions reload automatically and never rewrite saved IDs.
definition_path=test_root/'resources/Data/weapons.json'
original_text=definition_path.read_text(encoding='utf-8')
document=json.loads(original_text)
new_weapon=dict(document['weapons'][0],id='new_test_weapon',type='NewTestType',weight=9)
document['weapons'].insert(0,new_weapon)
definition_path.write_text(json.dumps(document),encoding='utf-8')
updated=e.definitions(scene,'weapons')
assert updated['new_test_weapon']['type']=='NewTestType'
assert cfg.pool[0].identifier=='pistol'
cfg.pool.clear(); cfg.weapon_slot='ALL'; cfg.min_rarity='1'; cfg.max_rarity='5'; cfg.weapon_types.clear()
cfg.weapon_types.add().name='NewTestType'
assert e.matched_weapons(cfg,updated)==[{'id':'new_test_weapon','weight':9}]
row=cfg.pool.add(); row.identifier='new_test_weapon'; row.weight=4
definition_path.write_text(original_text,encoding='utf-8')
assert 'new_test_weapon' not in e.definitions(scene,'weapons')
assert cfg.pool[0].identifier=='new_test_weapon'

# Unknown IDs/types survive save/load; existing IDs keep their strings too.
cfg.weapon_types.add().name='Shotgun'
bpy.ops.wm.save_as_mainfile(filepath=str(root/'generated/blender-tests/filter-properties.blend'))
bpy.ops.wm.open_mainfile(filepath=str(root/'generated/blender-tests/filter-properties.blend'))
cfg=bpy.context.scene.objects['WeaponSpawn_01'].yan_level
assert cfg.pool[0].identifier=='new_test_weapon' and cfg.pool[0].weight==4
assert [t.name for t in cfg.weapon_types]==['NewTestType','Shotgun']
assert e.weapon_filter(cfg)=={'minRarity':1,'maxRarity':5,'types':['NewTestType','Shotgun']}
assert bpy.context.scene.objects['SP_A_03'].yan_level.pool[0].identifier=='fast'

# An optional second existing stage is read-only throughout this regression test.
stage02=root/'resources/levels/stage02/stage02.blend'
if stage02.exists():
    bpy.ops.wm.open_mainfile(filepath=str(stage02))
    bpy.context.scene.yan_level.project_root=str(root)
    before={o.name:[p.identifier for p in o.yan_level.pool] for o in bpy.context.scene.objects}
    e.build_level(bpy.context.scene,bpy.context.evaluated_depsgraph_get())
    assert before=={o.name:[p.identifier for p in o.yan_level.pool] for o in bpy.context.scene.objects}
print('BLENDER_SPAWN_UI_TESTS_PASSED: filter export, manual/default weights, zero matches, dynamic definitions, missing ID persistence, stage02')

# Catalog edits are a draft until Apply and never change Dynamic Filter.
bpy.ops.wm.open_mainfile(filepath=str(root/'resources/levels/stage01/stage01.blend'))
scene=bpy.context.scene
scene.yan_level.project_root=str(test_root)
target=scene.objects['WeaponSpawn_01']
bpy.context.view_layer.objects.active=target
cfg=target.yan_level
cfg.pool.clear()
for identifier,weight in [('smg',7),('pistol',3),('smg',2)]:
    row=cfg.pool.add(); row.identifier=identifier; row.weight=weight
cfg.weapon_slot='Main'; cfg.min_rarity='2'; cfg.max_rarity='3'
cfg.weapon_types.clear()
state=bpy.context.window_manager.yan_catalog
state.target=target; state.scene=scene
e.reload_catalog(bpy.context,state,initial=True)
snapshot=[(p.identifier,p.weight) for p in cfg.pool]
filters=e.weapon_filter(cfg)
assert {r.identifier for r in state.entries if r.selected}=={'smg','pistol'}
state.slot='Main'; state.rarity='2'; state.weapon_type='Shotgun'
visible={r.identifier for r in state.entries if e.catalog_visible(state,r)}
assert visible=={key for key,w in weapons.items() if w['slot']=='Main' and w['rarity']==2 and w['type']=='Shotgun'}
assert visible
assert bpy.ops.yanengine.catalog_action(action='SELECT')=={'FINISHED'}
assert {r.identifier for r in state.entries if r.selected}==visible|{'smg','pistol'}
assert [(p.identifier,p.weight) for p in cfg.pool]==snapshot
assert bpy.ops.yanengine.catalog_action(action='CLEAR_VISIBLE')=={'FINISHED'}
assert {r.identifier for r in state.entries if r.selected}=={'smg','pistol'}
bpy.ops.yanengine.catalog_action(action='SELECT')
assert bpy.ops.yanengine.catalog_apply()=={'FINISHED'}
assert [(p.identifier,p.weight) for p in cfg.pool][:3]==snapshot
assert all(p.weight==1 for p in list(cfg.pool)[3:])
assert e.weapon_filter(cfg)==filters

# Matched view follows the saved pool AND filter, not draft checks.
state.slot='ALL'; state.rarity='ALL'; state.weapon_type=''; state.matched_only=True
assert {r.identifier for r in state.entries if e.catalog_visible(state,r)}=={p['id'] for p in e.matched_weapons(cfg,weapons)}
bpy.ops.yanengine.catalog_action(action='CLEAR')
assert not any(r.selected for r in state.entries)
assert len(cfg.pool)>0
bpy.ops.yanengine.catalog_apply()
assert len(cfg.pool)==0 and e.weapon_filter(cfg)==filters
assert {r.identifier for r in state.entries if e.catalog_visible(state,r)}=={p['id'] for p in e.matched_weapons(cfg,weapons)}

# Reload adds new JSON definitions/types, but never checks new weapons automatically.
state.matched_only=False
document=json.loads(original_text)
document['weapons'].append(new_weapon)
definition_path.write_text(json.dumps(document),encoding='utf-8')
bpy.ops.yanengine.catalog_action(action='RELOAD')
draft=next(r for r in state.entries if r.identifier=='new_test_weapon')
assert draft.weapon_type=='NewTestType' and not draft.selected
draft.selected=True
bpy.ops.yanengine.catalog_apply()
assert [(p.identifier,p.weight) for p in cfg.pool]==[('new_test_weapon',1)]
definition_path.write_text(original_text,encoding='utf-8')
bpy.ops.yanengine.catalog_action(action='RELOAD')
draft=next(r for r in state.entries if r.identifier=='new_test_weapon')
assert draft.missing and draft.selected
bpy.ops.yanengine.catalog_apply()
assert cfg.pool[0].identifier=='new_test_weapon'  # no silent removal
draft=next(r for r in state.entries if r.identifier=='new_test_weapon')
draft.selected=False
bpy.ops.yanengine.catalog_apply()
assert not cfg.pool

# A different active object cannot receive a stale catalog draft.
bpy.context.view_layer.objects.active=scene.objects['PlayerSpawn']
try: e.apply_catalog(bpy.context,state)
except ValueError: pass
else: raise AssertionError('Catalog wrote to a different selection')
bpy.context.view_layer.objects.active=target
assert e.weapon_filter(cfg)==filters
e.unregister(); e.register()
print('BLENDER_CATALOG_TESTS_PASSED: display filters, visible/hidden checks, draft isolation, weights/order, matches, reload, missing IDs, target guard')
print('BLENDER_LEVEL_TESTS_PASSED: roles, validation, groups/pools, transforms, glTF filtering, atomic rollback, saved properties, registration')
