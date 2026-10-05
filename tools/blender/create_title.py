"""Create the editable title prototype: Blender --background --python this_file.py."""
import sys
from pathlib import Path
import math
import bpy
from mathutils import Matrix, Vector, Euler
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter
exporter.register()
root = Path(__file__).resolve().parents[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = 'Title'
scene.yan_level.project_root = str(root)
scene.yan_level.stage_id = 'title'
scene.yan_level.output_directory = 'resources/levels/title'
scene.yan_level.title_weapon = 'pistol'
scene.yan_level.title_start_delay = .75


def material(name, color):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, 1)
    shader = next(node for node in mat.node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Roughness'].default_value = .7
    return mat


def pose(obj, position, scale=(1,1,1), rotation=(0,0,0)):
    engine = Matrix.LocRotScale(Vector(position), Euler(rotation, 'XYZ').to_quaternion(), Vector(scale))
    obj.matrix_world = exporter.ENGINE_BASIS.inverted() @ engine @ exporter.ENGINE_BASIS


def box(name, position, size, mat):
    bpy.ops.mesh.primitive_cube_add(size=2)
    obj = bpy.context.object
    obj.name = name
    pose(obj, position, tuple(v*.5 for v in size))
    obj.yan_level.role = 'STATIC'
    obj.yan_level.collision = 'BOX'
    obj.data.materials.append(mat)
    return obj


def text(name, body, position, size, mat, role='STATIC'):
    curve = bpy.data.curves.new(name, 'FONT')
    curve.body = body
    curve.size = size
    curve.extrude = .06
    curve.bevel_depth = .008
    curve.resolution_u = 8
    obj = bpy.data.objects.new(name, curve)
    scene.collection.objects.link(obj)
    # Text right/up/front -> engine +X/+Y/-Z, facing the fixed player.
    obj.matrix_world = Matrix(((-1,0,0,0),(0,0,1,0),(0,1,0,0),(0,0,0,1)))
    obj.location = (exporter.ENGINE_BASIS.inverted() @ Vector((*position,1))).to_3d()
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.convert(target='MESH')
    obj = bpy.context.object
    obj.yan_level.role = role
    obj.yan_level.collision = 'NONE'
    obj.data.materials.append(mat)
    return obj


floor = material('Title Floor', (.09,.12,.16))
wall = material('Title Backdrop', (.035,.055,.08))
base = material('Start Plinth', (.16,.23,.3))
accent = material('Start Letter', (.15,.85,1))
white = material('Title Text', (.92,.95,1))
box('Floor', (0,-.25,8), (28,.5,28), floor)
box('Backdrop', (0,4,14), (28,8,.5), wall)
box('Start_Platform', (4.2,.7,10.8), (7,1.4,2), base)
text('Game_Title', 'UNALIVE', (-4.8,5,12), 1.7, white)
text('Start_Instruction', 'SHOOT TO START', (1.3,.8,9.78), .48, white)
start_letters = []
for i, (char, x) in enumerate(zip('GAMESTART', (1,1.7,2.4,3.2,4.4,5.1,5.8,6.5,7.2))):
    start_letters.append(text(f'GAME_START_{i:02}_{char}', char, (x,1.6,10.5), .9, accent, 'START_LETTER'))
bpy.ops.object.select_all(action='DESELECT')
for obj in start_letters: obj.select_set(True)
bpy.context.view_layer.objects.active = start_letters[0]
bpy.ops.object.join()
bpy.context.object.name = 'GAME_START'
player = bpy.data.objects.new('PlayerSpawn', None)
scene.collection.objects.link(player)
player.yan_level.role = 'PLAYER'
pose(player, (0,0,-5), rotation=(-.06,0,0))
enemy = bpy.data.objects.new('EnemySpawn_Title', None)
scene.collection.objects.link(enemy)
enemy.yan_level.role = 'ENEMY'
pose(enemy, (-4,0,10.5), rotation=(0,-math.pi/2,0))
enemy.yan_level.group = 'Title'
entry = enemy.yan_level.pool.add()
entry.identifier = 'normal'
entry.weight = 1
bpy.context.view_layer.update()
exporter.export_level(bpy.context)
scene.yan_level.project_root = '//../../../'
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(root/'resources/levels/title/title.blend'))
print('TITLE_CREATED')
