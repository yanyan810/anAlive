"""Validate one combined GAME START Head asset and world-baked transforms."""
import sys
import json
import shutil
from pathlib import Path
import bpy
from mathutils import Vector, Matrix, Euler
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
e.register()
root = Path(__file__).resolve().parents[2]
bpy.ops.wm.open_mainfile(filepath=str(root/'resources/levels/title/title.blend'))
scene = bpy.context.scene
project = root/'generated/blender-title-tests/project'
(project/'resources/Data').mkdir(parents=True, exist_ok=True)
for name in ('enemies.json', 'weapons.json'):
    shutil.copyfile(root/'resources/Data'/name, project/'resources/Data'/name)
scene.yan_level.project_root = str(project)
bpy.context.view_layer.update()
data, geometry = e.build_level(scene,bpy.context.evaluated_depsgraph_get())
assert 'startLetters' not in data['title'] and data['title']['startObject']['id']=='GAME_START'
assert len(data['spawnPoints'])==1
assert not data['spawnTriggers'] and not data['weaponSpawnPoints'] and not data['goalTriggers']
assert all(not obj.name.startswith('GAME_START') for obj in geometry)
starts = [obj for obj in scene.objects if obj.yan_level.role=='START_LETTER']
assert len(starts)==1 and starts[0].type=='MESH'
start=starts[0]
saved=start.matrix_world.copy()
parent=bpy.data.objects.new('Title_TestParent',None)
scene.collection.objects.link(parent)
parent.matrix_world=Matrix.LocRotScale(Vector((1,2,-3)),Euler((.1,.2,-.3)).to_quaternion(),Vector((1.2,1.2,1.2)))
start.parent=parent
start.matrix_basis=saved
bpy.context.view_layer.update()
graph=bpy.context.evaluated_depsgraph_get()
evaluated=start.evaluated_get(graph)
points=[(e.ENGINE_BASIS @ start.matrix_world @ v.co.to_4d()).to_3d() for v in evaluated.data.vertices]
expected_min=[min(p[i] for p in points) for i in range(3)]
expected_max=[max(p[i] for p in points) for i in range(3)]
target=e.export_level(bpy.context)
data=json.loads((target/'title.json').read_text())
static=json.loads((target/'title.gltf').read_text())
assert len(static['meshes'])==len(geometry)
assert all('GAME_START' not in node.get('name','') for node in static['nodes'])
asset=json.loads((project/data['title']['startObject']['partAsset']).read_text())
assert asset['coordinateSystem']=='yanengine' and asset['hpGroups']==[] and len(asset['parts'])==1
head=asset['parts'][0]
assert head['name']=='Head' and head['role']=='Head' and head['localHp']==1
assert head['breakable'] and head['deathOnZero']
assert set(head['faces'])==set(tri['face'] for tri in head['triangles'])
vertices=[v['p'] for tri in head['triangles'] for v in tri['vertices']]
low=[min(p[i] for p in vertices) for i in range(3)]
high=[max(p[i] for p in vertices) for i in range(3)]
assert max(abs(a-b) for a,b in zip(low,expected_min))<1e-4
assert max(abs(a-b) for a,b in zip(high,expected_max))<1e-4
assert not any(path.name.startswith('start_') for path in target.glob('*.gltf'))
start.yan_level.role='IGNORE'
try: e.build_level(scene,bpy.context.evaluated_depsgraph_get())
except ValueError: pass
else: raise AssertionError('Empty GAME START accepted')
start.yan_level.role='START_LETTER'
extra=start.copy(); extra.name='Extra_START'; scene.collection.objects.link(extra)
extra.yan_level.identifier='Extra_START'
try: e.build_level(scene,bpy.context.evaluated_depsgraph_get())
except ValueError: pass
else: raise AssertionError('Multiple GAME START meshes accepted')
print('PASS: one combined Head model, all faces assigned, 1-HP death, static isolation, parent/rotation/scale bake, required single-object validation')
