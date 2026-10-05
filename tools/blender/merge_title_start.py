"""Migrate the existing title without replacing its authored geometry/placements."""
import sys
from pathlib import Path
import bpy
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
e.register()
root = Path(__file__).resolve().parents[2]
path = root/'resources/levels/title/title.blend'
bpy.ops.wm.open_mainfile(filepath=str(path))
scene = bpy.context.scene
scene.yan_level.project_root = str(root)
objects = sorted((obj for obj in scene.objects if obj.yan_level.role=='START_LETTER'), key=lambda obj: obj.name)
if not objects or any(obj.type!='MESH' for obj in objects):
    raise ValueError('Existing title needs Game Start Mesh objects')
bpy.ops.object.select_all(action='DESELECT')
for obj in objects: obj.select_set(True)
bpy.context.view_layer.objects.active = objects[0]
if len(objects)>1: bpy.ops.object.join()
bpy.context.object.name = 'GAME_START'
bpy.context.object.yan_level.identifier = 'GAME_START'
bpy.context.view_layer.update()
e.export_level(bpy.context)
scene.yan_level.project_root = '//../../../'
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(path))
print('TITLE_MERGED: original positions retained, one GAME_START mesh, entire Head asset')
