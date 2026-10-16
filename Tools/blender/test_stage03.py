"""Blender --background --factory-startup --python-exit-code 1 --python test_stage03.py."""
import json
import shutil
import sys
from pathlib import Path
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter
exporter.register()
root=Path(__file__).resolve().parents[2]
bpy.ops.wm.open_mainfile(filepath=str(root/'resources/levels/stage03/stage03.blend'))
scene=bpy.context.scene
baseline,geometry=exporter.build_level(scene,bpy.context.evaluated_depsgraph_get())
assert baseline['stage']['id']=='stage03' and baseline['stage']['playerGravity']
assert len(baseline['spawnPoints'])==6 and not baseline['spawnTriggers'] and not baseline['spawnGroups']
assert len([c for c in baseline['colliders'] if c.get('playerOnly')])==6
assert len([c for c in baseline['colliders'] if 'SmoothRamp' in c['id'] and c.get('walkable')])==4
assert {'F3_Start','F2_Middle','F1_Escape','Outdoor','Building_Roof'} <= set(bpy.data.collections.keys())
assert all(bpy.data.objects[f'F3_Room30{i}_Floor'].yan_level.role=='STATIC' for i in range(1,9))
assert bpy.data.objects['PlayerSpawn_F3_Room308'].yan_level.role=='PLAYER'
assert 'PlayerSpawn_F3_Room301' not in bpy.data.objects
assert 'F3_Edit' in scene.view_layers
edit_children=scene.view_layers['F3_Edit'].layer_collection.children['Stage03_Hotel'].children
assert all(c.exclude==(c.name!='F3_Start') for c in edit_children)
assert len([o for o in scene.objects if 'FixedWindow_' in o.name])==8
assert list(scene['hotel_footprint'])==[0,38,0,36] and list(scene['hotel_floor_heights'])==[0,6,12]
assert list(scene['hotel_guest_footprint'])==[0,38,0,22]
saved=json.loads((root/'resources/levels/stage03/stage03.json').read_text(encoding='utf-8'))
assert baseline==saved, 'Saved blend and exported JSON differ'
shutter=bpy.data.objects['Hotel_Shutter_Raised_Placeholder']
assert shutter.yan_level.role=='SHUTTER' and shutter not in geometry
assert shutter.yan_level.shutter_button==bpy.data.objects['Hotel_Shutter_Button_Placeholder']
assert len(saved['shutters'])==1 and saved['shutters'][0]['closedPosition']==[12,0,18]
assert not any(c['id']==shutter.name for c in saved['colliders'])
static=json.loads((root/'resources/levels/stage03/stage03.gltf').read_text())
assert not any(n.get('name','').startswith(shutter.name) for n in static['nodes'])
assert (root/'resources'/saved['shutters'][0]['model']).is_file()
button=shutter.yan_level.shutter_button
shutter.yan_level.shutter_button=None
try:
    exporter.build_level(scene,bpy.context.evaluated_depsgraph_get())
except ValueError:
    pass
else:
    raise AssertionError('Shutter without switch was accepted')
shutter.yan_level.shutter_button=button
# Edit a room and round-trip to an isolated project; retain the actual hotel assets.
target=root/'generated/hotel-f3-roundtrip'
data_directory=target/'resources/Data'
data_directory.mkdir(parents=True,exist_ok=True)
shutil.copyfile(root/'resources/Data/enemies.json',data_directory/'enemies.json')
scene.yan_level.project_root=str(target)
scene.yan_level.output_directory='resources/levels/stage03'
floor=bpy.data.objects['F3_Room301_Floor']
floor.scale.x *= 1.1
bpy.context.view_layer.update()
output=exporter.export_level(bpy.context)
after=json.loads((output/'stage03.json').read_text(encoding='utf-8'))
lookup=lambda data: next(c for c in data['colliders'] if c['id']=='F3_Room301_Floor')
assert lookup(after)['scale']!=lookup(saved)['scale']
assert after['stage']['playerGravity'] and sum(c.get('playerOnly',False) for c in after['colliders'])==6
assert after['shutters']==saved['shutters'], 'Re-export lost shutter settings'
assert (output/'shutter_000.gltf').is_file() and (output/'shutter_000.bin').is_file()
assert (output/'stage03.gltf').is_file() and (output/'stage03.bin').is_file()
print('PASS: saved hotel blend/JSON agreement, floor/roof collections, eight rooms / 308 spawn, six gates, four flights, edited room glTF/JSON re-export')
