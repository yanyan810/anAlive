"""Blender --background --python tools/blender/create_gameover_demo.py.

Editable small room, using the same exporter and StageStart spawn data as gameplay.
Enemy yaw follows EnemyAI's model-forward convention (yaw - pi/2).
"""
import math
import sys
from pathlib import Path
import bpy
from mathutils import Matrix, Vector, Euler

sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter

exporter.register()
root = Path(__file__).resolve().parents[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = 'GameOverDemo'
scene.yan_level.project_root = str(root)
scene.yan_level.stage_id = 'gameover_demo'
scene.yan_level.output_directory = 'resources/levels/gameover_demo'
scene.yan_level.fixed_seed = True
scene.yan_level.seed = 42


def make(name, role, position, size=None, yaw=0, color=None):
    if size:
        bpy.ops.mesh.primitive_cube_add(size=2)
        obj = bpy.context.object
    else:
        obj = bpy.data.objects.new(name, None)
        scene.collection.objects.link(obj)
    obj.name = name
    obj.yan_level.role = role
    matrix = Matrix.LocRotScale(Vector(position), Euler((0, yaw, 0), 'XYZ').to_quaternion(),
                               Vector(tuple(v / 2 for v in size) if size else (1, 1, 1)))
    obj.matrix_world = exporter.ENGINE_BASIS.inverted() @ matrix @ exporter.ENGINE_BASIS
    if size:
        obj.yan_level.collision = 'BOX'
        mat = bpy.data.materials.new(name)
        mat.diffuse_color = (*color, 1)
        mat.use_nodes = True
        mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value = (*color, 1)
        obj.data.materials.append(mat)
    return obj


make('Floor', 'STATIC', (0, -.25, 0), (12, .5, 12), color=(.35, .32, .29))
make('Wall_Back', 'STATIC', (0, 2, -6.2), (12.8, 4, .4), color=(.42, .43, .46))
make('Wall_Front', 'STATIC', (0, 2, 6.2), (12.8, 4, .4), color=(.42, .43, .46))
make('Wall_Left', 'STATIC', (-6.2, 2, 0), (.4, 4, 12), color=(.32, .34, .38))
make('Wall_Right', 'STATIC', (6.2, 2, 0), (.4, 4, 12), color=(.32, .34, .38))
make('PlayerSpawn', 'PLAYER', (0, 0, 0))
# Radius 1.3 < normal melee range 1.5, > player+enemy radii .4+.36.
# Pairwise distance 1.3 > enemy diameter .72; no obstacles block sight/body routes.
for i in range(6):
    angle = i * math.tau / 6
    x, z = 1.3 * math.sin(angle), 1.3 * math.cos(angle)
    yaw = math.atan2(-x, -z) + math.pi / 2
    obj = make(f'DemoEnemy_{i + 1:02}', 'ENEMY', (x, 0, z), yaw=yaw)
    cfg = obj.yan_level
    cfg.spawn_activation = 'StageStart'
    cfg.enemy_definition = 'normal'
    cfg.spawn_count = cfg.max_alive = 1
    cfg.spawn_mode = 'Simultaneous'
    cfg.initial_delay = cfg.spawn_interval = 0

bpy.context.view_layer.update()
exporter.export_level(bpy.context)
scene.yan_level.project_root = '//../../../'
bpy.ops.wm.save_as_mainfile(filepath=str(root / 'resources/levels/gameover_demo/gameover_demo.blend'))
print('GAMEOVER_DEMO_CREATED')
