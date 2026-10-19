"""Run with Blender --background --python this_file.py to rebuild the editable example."""
import sys
from pathlib import Path
import json
import bpy
from mathutils import Matrix, Vector, Euler
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter
exporter.register()
root = Path(__file__).resolve().parents[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = 'Stage01'
scene.yan_level.project_root = str(root)
scene.yan_level.stage_id = 'stage01'
scene.yan_level.output_directory = 'resources/levels/stage01'
parent = bpy.data.collections.new('Stage01')
scene.collection.children.link(parent)
collections = {}
for name in ['Geometry','Colliders','EnemySpawns','Triggers','WeaponSpawns','PlayerSpawn','Goal']:
    collection = bpy.data.collections.new(name)
    parent.children.link(collection)
    collections[name] = collection


def pose(obj, position, rotation=(0,0,0), scale=(1,1,1)):
    engine = Matrix.LocRotScale(Vector(position), Euler(rotation,'XYZ').to_quaternion(), Vector(scale))
    obj.matrix_world = exporter.ENGINE_BASIS.inverted() @ engine @ exporter.ENGINE_BASIS


def make(name, role, collection, position, size=None, rotation=(0,0,0), mesh=False):
    if mesh:
        bpy.ops.mesh.primitive_cube_add(size=2)
        obj = bpy.context.object
        for c in list(obj.users_collection): c.objects.unlink(obj)
    else:
        obj = bpy.data.objects.new(name, None)
    obj.name = name
    collections[collection].objects.link(obj)
    obj.yan_level.role = role
    if role == 'ENEMY': obj.yan_level.spawn_activation = 'LEGACY'
    if role == 'TRIGGER': obj.yan_level.trigger_mode = 'LEGACY'
    pose(obj,position,rotation,tuple(v*.5 for v in size) if size else (1,1,1))
    return obj


floor=make('Floor','STATIC','Geometry',(3,-.3,24),(34,.6,76),mesh=True)
floor.yan_level.collision='BOX'
for name, pos, size, rotation in [
    ('Wall_Left',(-14.3,3,24),(.6,6,76),(0,0,0)),
    ('Wall_Right',(20.3,3,24),(.6,6,76),(0,0,0)),
    ('Wall_Start',(3,3,-14.3),(34,6,.6),(0,0,0)),
    ('Wall_End',(3,3,62.3),(34,6,.6),(0,0,0)),
    ('Wall_Rotated',(-7,3,25),(9,6,.6),(0,.4,0)),
    ('Wall_Inner',(12,3,32),(8,6,.6),(0,0,0))]:
    obj=make(name,'STATIC','Geometry',pos,size,rotation,True)
    obj.yan_level.collision='BOX'
rock=make('Decoration_NoCollision','STATIC','Geometry',(16,.7,22),(1.4,1.4,1.4),mesh=True)
rock.yan_level.collision='NONE'
building=make('Building_Custom','STATIC','Geometry',(-10,2,44),(4,4,4),mesh=True)
collider=make('COL_Building','COLLIDER','Colliders',(-10,2,44),(4,4,4),mesh=True)
building.yan_level.collision='CUSTOM'; building.yan_level.custom_collider=collider
make('PlayerSpawn','PLAYER','PlayerSpawn',(3,0,-6))
# Import the earlier layout once as authoring objects; future exports read only this .blend.
legacy=json.loads((root/'resources/levels/fps_spawns.json').read_text(encoding='utf-8'))
for item in legacy['spawnPoints']:
    obj=make(item['id'],'ENEMY','EnemySpawns',item['position'],rotation=item['rotation'])
    obj.yan_level.group='A' if item['id'].startswith('SP_A') else 'B'
    for entry in item['enemyPool']:
        row=obj.yan_level.pool.add(); row.identifier=entry['id']; row.weight=entry['weight']
for item in legacy['spawnTriggers']:
    obj=make(item['id'],'TRIGGER','Triggers',item['position'],item['size'])
    cfg=obj.yan_level; cfg.group='A' if item['id']=='Trigger_A' else 'B'
    cfg.spawn_count=item['spawnCount']; cfg.spawn_interval=item['spawnInterval']; cfg.initial_delay=item['initialDelay']
    cfg.max_alive=item['maxAlive']; cfg.selection=item['selection']; cfg.one_shot=item['oneShot']
for item in legacy['weaponSpawnPoints']:
    obj=make(item['id'],'WEAPON','WeaponSpawns',item['position'],rotation=item['rotation'])
    for entry in item['weaponPool']:
        row=obj.yan_level.pool.add(); row.identifier=entry if isinstance(entry,str) else entry['id']
        row.weight=1 if isinstance(entry,str) else entry['weight']
for item in legacy['goalTriggers']:
    make(item['id'],'GOAL','Goal',item['position'],item['size'])
bpy.context.view_layer.update()
exporter.export_level(bpy.context)
scene.yan_level.project_root='//../../../'
# Saved custom properties and portable root make this the editable starting point.
bpy.ops.wm.save_as_mainfile(filepath=str(root/'resources/levels/stage01/stage01.blend'))
print('STAGE01_CREATED')
