"""Build the editable upper-floor hotel prototype. Re-running replaces stage04 only."""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter

bpy.ops.wm.read_factory_settings(use_empty=True)
exporter.register()
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'resources/levels/stage04'
BASIS = exporter.ENGINE_BASIS
TO_BLENDER = BASIS.inverted()
LEVELS = {1: .08, 2: 3.8, 3: 7.6, 4: 11.4, 5: 15.2}
HEIGHT = 3.45
scene = bpy.context.scene
scene.name = 'Hotel_Upper_Floors'
scene.yan_level.stage_id = 'stage04'
scene.yan_level.project_root = str(ROOT)
scene.yan_level.output_directory = 'resources/levels/stage04'
scene.yan_level.player_gravity = True
scene.yan_level.fall_death_distance = 3.0
scene.yan_level.fixed_seed = True


def collection(name, parent):
    result = bpy.data.collections.new(name)
    parent.children.link(result)
    return result


groups = {}
for floor in (5, 4):
    parent = collection(f'F{floor}_Hotel', scene.collection)
    for name in ('Rooms', 'Corridor', 'Doors', 'Facade_Windows', 'Stairs',
                 'Elevator', 'Exterior_Ledge', 'Fire', 'Gameplay'):
        groups[floor, name] = collection(f'F{floor}_{name}', parent)
groups[0, 'Context'] = collection('Outdoor_Context', scene.collection)
groups[0, 'Roof'] = collection('Building_Roof', scene.collection)
materials = {}
for name, rgb in {
    'room': (.37, .31, .25), 'hall': (.20, .27, .30), 'wall': (.64, .62, .55),
    'trim': (.25, .15, .09), 'facade': (.47, .42, .36), 'glass': (.12, .31, .43),
    'step': (.37, .41, .44), 'steel': (.16, .20, .23), 'concrete': (.42, .43, .43),
    'rubble': (.24, .22, .20), 'smoke': (.09, .085, .08), 'fire': (1, .22, .035),
    'ember': (1, .66, .09), 'guide': (.15, .78, .72), 'asphalt': (.19, .22, .25),
}.items():
    material = bpy.data.materials.new(name)
    material.diffuse_color = (*rgb, 1)
    material.use_nodes = True
    shader = material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*rgb, 1)
    shader.inputs['Roughness'].default_value = .85
    if name in {'fire', 'ember', 'guide'}:
        shader.inputs['Emission Color'].default_value = (*rgb, 1)
        shader.inputs['Emission Strength'].default_value = 1.6
    materials[name] = material

# Local coordinates are converted too, so the same authoring convention drives
# the mesh, its collision box, its pivot and its rotation.
UNIT_VERTICES = [(x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
BOX_FACES = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
             (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]


def obj(floor, group, name, position, size=None, *, material='wall', collision=True,
        walkable=False, role='STATIC', rotation=(0, 0, 0), offset=(0, 0, 0), wedge=False):
    name = f'F{floor}_{name}' if floor else name
    mesh = None
    if size:
        mesh = bpy.data.meshes.new(name)
        vertices = UNIT_VERTICES
        if wedge:
            vertices = [(x * (.18 if y > 0 else 1), y, z * (.18 if y > 0 else 1))
                        for x, y, z in vertices]
        points = [TO_BLENDER.to_3x3() @ Vector(tuple(o + v*d/2 for v, d, o in zip(p, size, offset)))
                  for p in vertices]
        # The coordinate basis reflects X.
        mesh.from_pydata(points, [], [tuple(reversed(face)) for face in BOX_FACES])
        mesh.materials.append(materials[material])
    result = bpy.data.objects.new(name, mesh)
    groups[floor, group].objects.link(result)
    transform = Matrix.LocRotScale(Vector(position), Euler(rotation, 'XYZ').to_quaternion(), Vector((1, 1, 1)))
    result.matrix_world = TO_BLENDER @ transform @ BASIS
    settings = result.yan_level
    settings.role = role
    settings.collision = 'BOX' if collision else 'NONE'
    settings.walkable = walkable
    return result


def slab(floor, group, name, bounds, top, material='concrete', thickness=.3):
    x0, x1, z0, z1 = bounds
    return obj(floor, group, name, ((x0+x1)/2, top-thickness/2, (z0+z1)/2),
               (x1-x0, thickness, z1-z0), material=material, walkable=True)


def wall(floor, group, name, axis, at, lo, hi, y, opening=None, material='wall', height=HEIGHT):
    def span(suffix, a, b, bottom, h, depth=.22):
        if b-a <= .001 or h <= .001:
            return
        pos = ((a+b)/2, bottom+h/2, at) if axis == 'X' else (at, bottom+h/2, (a+b)/2)
        size = (b-a, h, depth) if axis == 'X' else (depth, h, b-a)
        obj(floor, group, name+suffix, pos, size, material=material)

    if opening is None:
        span('', lo, hi, y, height)
    else:
        center, width, sill, head = opening
        a, b = center-width/2, center+width/2
        span('_Left', lo, a, y, height)
        span('_Right', b, hi, y, height)
        span('_Sill', a, b, y, sill)
        span('_Lintel', a, b, y+head, height-head)


def door(floor, room, axis, at, center, y, swing):
    width, height = 1.54, 2.40
    pivot = center-width/2
    if axis == 'X':
        pos, size, offset = (pivot, y, at), (width, height, .085), (width/2, height/2+.025, 0)
    else:
        pos, size, offset = (at, y, pivot), (.085, height, width), (0, height/2+.025, width/2)
    leaf = obj(floor, 'Doors', room+'_Door', pos, size, material='trim', role='DOOR', offset=offset)
    leaf.yan_level.door_angle = swing*math.pi/2
    leaf.yan_level.door_initially_open = room in {'Room403', 'Room404', 'Room503', 'Room504'}
    # Trim projects away from the opening and never narrows the walking gap.
    for side, coordinate in (('A', center-.85), ('B', center+.85)):
        pos = (coordinate, y+1.22, at) if axis == 'X' else (at, y+1.22, coordinate)
        size = (.11, 2.44, .28) if axis == 'X' else (.28, 2.44, .11)
        obj(floor, 'Doors', room+'_Frame'+side, pos, size, material='trim', collision=False)


# Shared 16-room shell. A door is (wall axis, fixed coordinate, door center, swing).
rooms = []
for i in range(6):
    a = 7.5+7*i
    rooms.append(((a, a+7, 0, 8), ('X', 8, a+3.5, 1), ('X', 0, a+3.5)))
for i in range(4):
    a = 18+7.75*i
    rooms.append(((a, a+7.75, 11, 19), ('X', 11, a+2.6, -1), ('X', 19, a+3.875)))
for i in range(3):
    a = 19+7*i
    rooms.append(((0, 7.5, a, a+7), ('Z', 7.5, a+3.5, -1), ('Z', 0, a+3.5)))
    rooms.append(((10.5, 18, a, a+7), ('Z', 10.5, a+3.5, 1), ('Z', 18, a+3.5)))


def build_floor(floor):
    y = LEVELS[floor]
    slab(floor, 'Corridor', 'MainCorridor_Floor', (7.5, 49, 8, 11), y, 'hall')
    slab(floor, 'Corridor', 'StartWing_Floor', (7.5, 10.5, 11, 40), y, 'hall')
    wall(floor, 'Corridor', 'StartWingEnd_Wall', 'X', 39.89, 0, 18, y, material='facade')
    wall(floor, 'Corridor', 'CoreWest_Wall', 'Z', 7.5, 11, 19, y)
    wall(floor, 'Corridor', 'CoreEast_Wall', 'Z', 10.5, 11, 19, y)
    wall(floor, 'Facade_Windows', 'CoreEastFacade', 'Z', 18, 11, 19, y, material='facade')
    wall(floor, 'Facade_Windows', 'CoreWestFacade', 'Z', .11, 0, 19, y, material='facade')
    wall(floor, 'Facade_Windows', 'CoreNorthFacade', 'X', .11, 0, 7.5, y, material='facade')
    wall(floor, 'Facade_Windows', 'CoreWestPartition', 'Z', 7.5, 0, 8, y)
    for index, (bounds, access, facade) in enumerate(rooms, 1):
        room = f'Room{floor*100+index}'
        x0, x1, z0, z1 = bounds
        slab(floor, 'Rooms', room+'_Floor', bounds, y, 'room')
        axis, at, center, swing = access
        lo, hi = (x0, x1) if axis == 'X' else (z0, z1)
        if floor != 5 or index not in (14, 16):
            wall(floor, 'Rooms', room+'_CorridorWall', axis, at, lo, hi, y, (center, 1.66, 0, 2.48))
            door(floor, room, axis, at, center, y, -swing if room == 'Room515' else swing)
        # One owner per shared room boundary.
        if index <= 10:
            opening = (4, 2.4, 0, 2.55) if floor == 5 and index == 4 else None
            wall(floor, 'Rooms', room+'_PartitionWall', 'Z', x0, z0, z1, y, opening)
        elif floor != 5 or index != 16:
            wall(floor, 'Rooms', room+'_PartitionWall', 'X', z0, x0, x1, y)
        if index == 10:
            wall(floor, 'Rooms', room+'_EastWall', 'Z', x1, z0, z1, y)
        if index == 6:
            wall(floor, 'Rooms', room+'_StairWall', 'Z', x1, z0, z1, y)
        facade_axis, edge, window_x = facade
        broken = floor == 4 and index in (4, 6)
        on_ledge = floor == 4 and index <= 6
        sill = 0 if on_ledge else .9
        width = 1.8 if on_ledge else 2.35
        wall(floor, 'Facade_Windows', room+'_WindowWall', facade_axis, edge, lo, hi, y,
             (window_x, width, sill, 2.45), 'facade')
        if not broken:
            height = 2.45-sill
            center = y+sill+height/2
            pos = (window_x, center, edge) if facade_axis == 'X' else (edge, center, window_x)
            size = (width, height, .10) if facade_axis == 'X' else (.10, height, width)
            glass = obj(floor, 'Facade_Windows', room+'_WindowGlass', pos, size, material='glass')
            glass.yan_level.break_on_bullet = True
        else:
            for dx in (-.82, .82):
                obj(floor, 'Facade_Windows', room+f'_BrokenFrame_{dx}', (window_x+dx, y+2.05, edge),
                    (.095, .72, .11), material='steel', collision=False, rotation=(0, 0, dx*.15))
    if floor == 5:
        wall(floor, 'Rooms', 'Lounge_CorridorWall', 'Z', 10.5, 26, 40, y, (33, 12, 0, 2.55))
        wall(floor, 'Corridor', 'CorridorDetour_Wall', 'Z', 30, 7.88, 11.12, y)
    for edge, name in ((8, 'North'), (11, 'South')):
        wall(floor, 'Corridor', 'WestCorridor_Wall_'+name, 'X', edge, 0, 7.5, y)
    slab(floor, 'Corridor', 'WestCorridor_Floor', (0, 7.5, 8, 11), y, 'hall')
    slab(floor, 'Elevator', 'ElevatorCore_Floor', (10.5, 18, 11, 19), y, 'steel')
    wall(floor, 'Elevator', 'Elevator_Entry', 'X', 11, 10.5, 18, y, (14.25, 1.8, 0, 2.55), 'steel')
    wall(floor, 'Elevator', 'Elevator_Back', 'X', 14.6, 12.8, 15.7, y, material='steel')
    for x, name in ((12.8, 'West'), (15.7, 'East')):
        wall(floor, 'Elevator', 'Elevator_Wall_'+name, 'Z', x, 11, 14.6, y, material='steel')
    if floor == 5:
        obj(floor, 'Elevator', 'Elevator_LockedPanel', (14.25, y+1.28, 11), (1.8, 2.56, .18), material='steel')
    else:
        obj(floor, 'Elevator', 'Elevator_Ready', (14.25, y+2.68, 10.85), (.75, .18, .1), material='guide', collision=False)
    wall(floor, 'Stairs', 'StairNorth_Wall', 'X', .11, 49, 55, y, material='facade')
    wall(floor, 'Stairs', 'StairEast_Wall', 'Z', 54.89, 0, 12, y, material='facade')
    wall(floor, 'Stairs', 'StairSouth_Wall', 'X', 11.89, 49, 55, y)
    wall(floor, 'Stairs', 'StairLobbyWall', 'Z', 49, 11, 12, y)
    if floor == 4:
        slab(floor, 'Stairs', 'StairLowerLanding_Floor', (49, 52, 0, 1.7), y, 'step')
        slab(floor, 'Stairs', 'StairReturn_Floor', (52, 55, 0, 12), y, 'step')
        slab(floor, 'Stairs', 'StairSouthLanding_Floor', (49, 52, 8.5, 12), y, 'step')
    else:
        slab(floor, 'Stairs', 'StairUpperLanding_Floor', (49, 55, 8.5, 12), y, 'step')
        slab(floor, 'Stairs', 'StairShaftSide_Floor', (52, 55, 0, 8.5), y, 'step')
        slab(floor, 'Stairs', 'StairShaftEnd_Floor', (49, 52, 0, 1.7), y, 'step')
        obj(floor, 'Stairs', 'StairUpperRailSide', (52, y+.53, 5.05), (.13, 1.06, 6.9), material='steel')
        obj(floor, 'Stairs', 'StairUpperRailEnd', (50.5, y+.53, 1.68), (3, 1.06, .13), material='steel')


for floor in (5, 4):
    build_floor(floor)

# The tread meshes are visual only. A continuous proxy, aligned to both landings,
# uses the same floor-following collision and navigation as the original hotel.
low, high = LEVELS[4], LEVELS[5]
z0, z1 = 1.7, 8.5
angle = -math.atan2(high-low, z1-z0)
length = math.hypot(high-low, z1-z0)
obj(5, 'Stairs', 'COL_StairRamp', (50.5, (high+low)/2-.13*math.cos(angle),
    (z0+z1)/2-.13*math.sin(angle)), (2.75, .26, length), role='COLLIDER',
    walkable=True, rotation=(angle, 0, 0))
for i in range(20):
    t = (i+.5)/20
    obj(5, 'Stairs', f'StairTread_{i+1:02}', (50.5, low+t*(high-low)-.13, z0+t*(z1-z0)),
        (2.75, .22, (z1-z0)/20+.01), material='step', collision=False)
obj(4, 'Stairs', 'StairReturnGuide', (52.02, low+.48, 5.1), (.12, .96, 6.8), material='steel')

slab(4, 'Exterior_Ledge', 'ExteriorWalkway_Floor', (9.35, 47.15, -1.68, .14), low, 'step', .22)
for x in (10.4, 14.9, 19.7, 24.4, 28.9, 33.1, 37.5, 41.8, 46.2):
    obj(4, 'Exterior_Ledge', f'ExteriorSupport_{x}', (x, low-.90, -.63), (.18, 1.1, 1.22),
        material='steel', collision=False, rotation=(.32, 0, 0))
for x in (32, 46):
    obj(4, 'Exterior_Ledge', f'WindowRouteMarker_{x}', (x, low+.012, .70), (1.25, .02, .32),
        material='guide', collision=False)

# The original corridor slab is present under a continuous full-height blocker.
obj(4, 'Fire', 'COL_FireBlockedCorridor', (36.5, low+HEIGHT/2, 9.5), (4.4, HEIGHT, 3.24), role='COLLIDER')
for i, (x, z, h) in enumerate(((34.5, 8.6, .65), (35.4, 10.2, .90), (36.3, 9.4, 1.2),
                              (37.4, 8.6, .72), (38.2, 10.25, .82))):
    obj(4, 'Fire', f'Rubble_{i}', (x, low+h/2, z), (1.2, h, 1.25), material='rubble',
        collision=False, rotation=(.12*i, .58*i, -.12))
    obj(4, 'Fire', f'Flame_{i}', (x, low+h+.34, z), (.62, 1.28, .57), material='fire',
        collision=False, wedge=True, rotation=(0, i*.4, .15))
    obj(4, 'Fire', f'Ember_{i}', (x+.15, low+h+.16, z+.12), (.38, .55, .38), material='ember',
        collision=False, wedge=True)
    obj(4, 'Fire', f'Smoke_{i}', (x+.15, low+2.95, z), (1.18, .68, .95), material='smoke',
        collision=False, wedge=True, rotation=(0, .65*i, .2))
obj(4, 'Fire', 'CollapsedCeilingBeam', (36.65, low+1.42, 9.55), (3.92, .41, .51),
    material='rubble', collision=False, rotation=(.17, .34, -.62))
obj(4, 'Fire', 'DetourDoorMarker', (46, low+2.68, 8.19), (.74, .19, .08), material='guide', collision=False)

# Exterior massing only. No playable first floor is authored here.
for i, bounds in enumerate(((0, 55, 0, 12), (0, 49, 12, 19), (0, 18, 19, 40))):
    slab(0, 'Roof', f'Roof_{i}', bounds, high+3.8, 'steel', .35)
    x0, x1, z0_, z1_ = bounds
    if i == 0:
        x1 = 49  # The enclosed east stairwell fills the rest.
    obj(0, 'Context', f'LowerFacadeBlock_{i}', ((x0+x1)/2, 8.36, (z0_+z1_)/2),
        (x1-x0, 5.28, z1_-z0_), material='facade')
slab(0, 'Context', 'Street_Floor', (-18, 73, -22, 56), 0, 'asphalt')
slab(0, 'Context', 'Sidewalk_Floor', (-7, 63, -5, -.05), .15, 'concrete', .15)
for i, x in enumerate((-10, 6, 24, 44, 62)):
    obj(0, 'Context', f'CityBackdrop_{i}', (x, 4+i%3*2, -19), (12, 8+i%3*4, 4), material='steel')


def marker(floor, name, pos, role, size=None, yaw=0):
    return obj(floor, 'Gameplay', name, pos, size, role=role, collision=False, rotation=(0, yaw, 0))


def build_lower_stairs():
    """Continue the 5F stairwell on the same footprint down to the lobby."""
    for floor in (2, 3):
        parent = collection(f'F{floor}_Hotel', scene.collection)
        groups[floor, 'Stairs'] = collection(f'F{floor}_Stairs', parent)

    for floor in (1, 2, 3):
        y = LEVELS[floor]
        rise = LEVELS[floor+1]-y
        if floor == 1:
            slab(1, 'Stairs', 'StairBottom_Floor', (49, 55, 0, 12), y, 'step')
        else:
            slab(floor, 'Stairs', 'StairNorthLanding_Floor', (49, 52, 0, 1.7), y, 'step')
            slab(floor, 'Stairs', 'StairReturn_Floor', (52, 55, 0, 12), y, 'step')
            slab(floor, 'Stairs', 'StairSouthLanding_Floor', (49, 52, 8.5, 12), y, 'step')
            obj(floor, 'Stairs', 'StairReturnRail', (52.02, y+.48, 5.1), (.12, .96, 6.8),
                material='steel')
        wall(floor, 'Stairs', 'StairNorth_Wall', 'X', .11, 49, 55, y, material='facade', height=rise)
        wall(floor, 'Stairs', 'StairEast_Wall', 'Z', 54.89, 0, 12, y, material='facade', height=rise)
        wall(floor, 'Stairs', 'StairSouth_Wall', 'X', 11.89, 49, 55, y, height=rise)
        opening = (.95, 1.5, 0, 2.7) if floor == 1 else None
        wall(floor, 'Stairs', 'StairWest_Wall', 'Z', 49, 0, 12, y, opening, height=rise)

        angle = -math.atan2(rise, 6.8)
        obj(floor, 'Stairs', 'COL_StairRamp', (50.5, y+rise/2-.13*math.cos(angle),
            5.1-.13*math.sin(angle)), (2.75, .26, math.hypot(rise, 6.8)),
            role='COLLIDER', walkable=True, rotation=(angle, 0, 0))
        for i in range(20):
            t = (i+.5)/20
            obj(floor, 'Stairs', f'StairTread_{i+1:02}', (50.5, y+t*rise-.13, 1.7+t*6.8),
                (2.75, .22, 6.8/20+.01), material='step', collision=False)

    y = LEVELS[1]
    slab(1, 'Stairs', 'StairConnector_Floor', (45, 49, 0, 5.8), y, 'step')
    slab(1, 'Stairs', 'StairConnector_Ceiling', (45, 49, 0, 5.8), y+3.34, 'concrete', .24)
    wall(1, 'Stairs', 'StairConnectorNorth_Wall', 'X', .11, 45, 49, y, material='facade', height=3.1)
    wall(1, 'Stairs', 'StairConnectorSouth_Wall', 'X', 5.69, 45, 49, y, height=3.1)

    y = LEVELS[4]
    obj(4, 'Stairs', 'COL_StairCollapse', (50.55, y+1.4, 8.46), (3.15, 2.8, .46), role='COLLIDER')
    for i, (x, z, w, h, angle) in enumerate((
        (49.6, 8.51, 1.15, 1.35, -.24), (50.6, 8.64, 1.45, 1.05, .17),
        (51.53, 8.47, 1.00, 1.52, -.35), (50.34, 8.30, 1.48, 1.55, .42),
        (51.3, 8.31, .85, 1.60, -.22),
    )):
        obj(4, 'Stairs', f'StairCollapse_Rubble_{i}', (x, y+h*.56, z), (w, h, .63),
            material='rubble', collision=False, rotation=(.17, angle, .23*(-1)**i))
    obj(4, 'Stairs', 'StairCollapse_Beam', (50.56, y+1.37, 8.49), (3.04, .32, .35),
        material='steel', collision=False, rotation=(0, .04, -.47))


def build_lobby_elevator(y, height):
    west, east = 11.22, 14.82
    north, south = 6.22, 9.12
    center = (north+south)/2
    wall(1, 'Elevator', 'Arrival_Back_Wall', 'Z', west, north, south, y, material='steel', height=height)
    for z, name in ((north, 'North'), (south, 'South')):
        wall(1, 'Elevator', 'Arrival_'+name+'_Wall', 'X', z, west, east, y, material='steel', height=height)
    wall(1, 'Elevator', 'Arrival_Entry_Wall', 'Z', east, north, south, y,
         (center, 1.8, 0, 2.55), 'steel', 3.2)
    obj(1, 'Elevator', 'Arrival_Light', (east+.15, y+2.69, center),
        (.12, .14, .8), material='guide', collision=False)
    x = 13.55
    yaw = math.atan2(45-x, 16.5-center)
    return marker(1, 'Elevator_Arrival', (x, y, center), 'IGNORE', yaw=yaw)


def build_lobby():
    """Ground-floor architecture and final defense, authored as ordinary level objects."""
    y = LEVELS[1]
    height = 5.4
    parent = collection('F1_Hotel', scene.collection)
    for group in ('Lobby', 'FrontDesk', 'Lounge', 'Service', 'Stairs',
                  'Elevator', 'Shutter', 'Breaches', 'Gameplay'):
        groups[1, group] = collection(f'F1_{group}', parent)

    def box(group, name, position, size, **kwargs):
        return obj(1, group, name, (position[0], y+position[1], position[2]), size, **kwargs)

    def partition(group, name, axis, at, lo, hi, opening=None, material='wall'):
        wall(1, group, name, axis, at, lo, hi, y, opening, material, height)

    slab(1, 'Lobby', 'Lobby_Floor', (5, 45, 2, 32), y, 'room')
    slab(1, 'Lobby', 'Lobby_Ceiling', (5, 45, 2, 32), y+height+.24, 'concrete', .24)
    partition('Lobby', 'NorthExterior_Wall', 'X', 2.11, 5, 45, material='facade')
    partition('Lobby', 'SouthExterior_Wall', 'X', 31.89, 5, 45, material='facade')
    partition('Lobby', 'WestExterior_Wall', 'Z', 5.11, 2, 32, material='facade')
    partition('Stairs', 'StairConnectorEntry_Wall', 'Z', 45, 2, 6, (4.1, 2.8, 0, 2.85), 'facade')
    partition('Shutter', 'ExitExterior_Wall', 'Z', 45, 6, 32, (16.5, 4.2, 0, 3.1), 'facade')
    partition('Stairs', 'NorthStair_Wall', 'X', 6, 27, 39, (33, 3.15, 0, 2.85))
    partition('Stairs', 'NorthStair_Wall_West', 'X', 6, 5, 18)
    partition('Service', 'Staff_Wall', 'Z', 11, 6, 27, (21.5, 2.6, 0, 2.7))
    partition('Service', 'SidePassage_Wall', 'X', 27, 25.5, 39, (32, 2.8, 0, 2.7))
    partition('Service', 'SouthServiceDivider_Wall', 'Z', 25.5, 27, 32)
    partition('Service', 'EastUpperDivider_Wall', 'X', 13.7, 39, 45)
    partition('Service', 'EastLowerDivider_Wall', 'X', 20.3, 39, 45)

    # Three separate room-to-lobby breaches, all traversable at floor level.
    partition('Breaches', 'BreachA_Wall', 'X', 6, 18, 27, (23, 2.8, 0, 2.9))
    partition('Breaches', 'BreachB_Wall', 'Z', 39, 6, 13.7, (9.5, 2.8, 0, 2.9))
    partition('Shutter', 'ExitInner_Wall', 'Z', 39, 13.7, 27, (16.5, 4.4, 0, 3.45))
    partition('Breaches', 'BreachC_Wall', 'X', 27, 5, 25.5, (19.7, 2.8, 0, 2.9))
    panels = {}
    for name, x, z, along_x in (('A', 23, 6, True), ('B', 39, 9.5, False), ('C', 19.7, 27, True)):
        size = (2.8, 2.9, .24) if along_x else (.24, 2.9, 2.8)
        panels[name] = box('Breaches', f'Breach{name}_Panel', (x, 1.45, z), size, material='facade').name
        # Jagged fragments at the jambs are visible when the middle panel drops.
        for side in (-1, 1):
            rx, rz = (x+side*1.4, z) if along_x else (x, z+side*1.4)
            box('Breaches', f'Breach{name}_Rubble_{side}', (rx, .23, rz), (.48, .46, .52),
                material='rubble', collision=False, wedge=True, rotation=(0, side*.4, 0))
            box('Breaches', f'Breach{name}_Rebar_{side}', (rx, 2.66, rz), (.07, .46, .09),
                material='steel', collision=False, rotation=(.25*side, 0, .35))

    arrival = build_lobby_elevator(y, height)
    transfer = marker(4, 'Elevator_Transfer', (14.25, LEVELS[4]+.8, 13.3), 'TRANSFER', (1.75, 2.4, 1.65))
    transfer.yan_level.transfer_arrival = arrival
    # The final floor is below the old absolute fall limit. The existing 3 m
    # unsupported-drop rule still applies before and after the transfer.
    scene.yan_level.player_fall_death = False

    box('FrontDesk', 'ReceptionCounter', (15.6, .57, 19.1), (5.2, 1.14, 1.05), material='trim')
    box('FrontDesk', 'ReceptionReturn', (13.4, .57, 20.15), (.8, 1.14, 1.8), material='trim')
    box('FrontDesk', 'CounterTop', (15.6, 1.17, 19.1), (5.35, .1, 1.14), material='concrete', collision=False)
    for x in (22.5, 34):
        for z in (11.4, 21.5):
            box('Lobby', f'Column_{x}_{z}', (x, height/2, z), (.9, height, .9), material='facade')
    for x in (25.4, 29.2):
        box('Lounge', f'Sofa_{x}', (x, .31, 24), (2.55, .62, 1.0), material='trim')
        box('Lounge', f'SofaBack_{x}', (x, .8, 24.42), (2.55, .52, .22), material='trim')
        box('Lounge', f'Table_{x}', (x, .26, 22.65), (1.28, .52, .72), material='wall')
    for i, (x, z) in enumerate(((18.2, 8.6), (36.5, 24.6))):
        box('Lounge', f'Planter_{i}', (x, .3, z), (.68, .6, .68), material='concrete')
        box('Lounge', f'Plant_{i}', (x, 1.05, z), (1.0, 1.1, .85), material='guide', collision=False, wedge=True)
    for i, (x, z) in enumerate(((19, 12), (19.6, 12.3), (35.7, 18.6))):
        box('FrontDesk', f'Luggage_{i}', (x, .33, z), (.52, .66, .41),
            material='trim', rotation=(0, .28*i, 0))

    slab(1, 'Shutter', 'ExitWalkway_Floor', (45, 51, 14.2, 18.8), y, 'hall')
    for edge, name in ((14.2, 'North'), (18.8, 'South')):
        wall(1, 'Shutter', 'ExitWalkway_Wall_'+name, 'X', edge, 45, 51, y, material='facade', height=1.2)
    switch = box('Shutter', 'ExitSwitch', (44.55, 1.25, 14.02), (.18, .29, .33), material='guide', collision=False)
    shutter = box('Shutter', 'ExitShutter', (45, 1.55, 16.5), (.28, 3.1, 4.2),
        material='steel', role='SHUTTER')
    cfg = shutter.yan_level
    cfg.shutter_button = switch
    cfg.shutter_duration = 30
    cfg.shutter_distance = 2.35
    cfg.shutter_defend_before_opening = True
    marker(1, 'HotelExit_Goal', (48.3, y+.8, 16.5), 'GOAL', (2, 2.4, 3.5))

    def manual(name, position, yaw, group, kind='normal', cap=1, pursue=False):
        x, height, z = position
        settings = marker(1, name, (x, y+height, z), 'ENEMY', yaw=yaw).yan_level
        settings.spawn_activation = 'Manual'
        settings.manual_group = group
        settings.enemy_definition = kind
        settings.spawn_count = 1
        settings.max_alive = cap
        settings.pursue_on_spawn = pursue

    for name, x, z, yaw in (('Stairs', 33, 8, -.6), ('Center', 26, 15.8, -math.pi/2),
                            ('Reception', 17.2, 20.7, math.pi), ('Lounge', 32, 24.5, math.pi)):
        enemy(1, 'ArrivalEnemy_'+name, (x, y, z), yaw)
    stair_height = (LEVELS[2]-y)*(3.0-1.7)/6.8
    manual('Defense_Stairs', (50.5, stair_height, 3), math.pi, 'LobbyRegular', cap=4, pursue=True)
    manual('Defense_Staff', (8, 0, 21.5), math.pi/2, 'LobbyRegular', cap=4, pursue=True)
    manual('Defense_Side', (32, 0, 30), math.pi, 'LobbyRegular', 'fast', 4, True)
    manual('Defense_BreachA', (23, 0, 3.9), 0, 'LobbyA', cap=3, pursue=True)
    manual('Defense_BreachB', (42, 0, 9.5), -math.pi/2, 'LobbyB', 'ranged', 2, True)
    manual('Defense_BreachC', (19.7, 0, 30), math.pi, 'LobbyC', 'fast', 2, True)

    events = [{'progress': t/30, 'event': f'regular_{t}', 'spawnGroup': 'LobbyRegular'} for t in (2, 10, 16, 28)]
    for hole, times in (('A', (12, 22, 28)), ('B', (20, 28)), ('C', (26, 28))):
        for i, t in enumerate(times):
            event = {'progress': t/30, 'event': f'breach{hole}_{t}', 'spawnGroup': 'Lobby'+hole}
            if i == 0:
                event['openCollider'] = panels[hole]
            events.append(event)
    cfg.shutter_progress_events = json.dumps(sorted(events, key=lambda event: event['progress']))


def enemy(floor, name, pos, yaw):
    cfg = marker(floor, name, pos, 'ENEMY', yaw=yaw).yan_level
    cfg.spawn_activation = 'StageStart'
    cfg.spawn_count = cfg.max_alive = 1


marker(5, 'Room515_PlayerStart', (3.1, high, 36.5), 'PLAYER', yaw=math.pi/2)
weapon = marker(5, 'Room516_StarterWeapon', (13.5, high+.28, 36.5), 'WEAPON')
weapon.yan_level.weapon_slot = 'Sub'
weapon.yan_level.max_rarity = '2'
obj(5, 'Doors', 'ItemDoorMarker', (10.3, high+2.68, 36.5), (.09, .18, .78), material='guide', collision=False)
enemy(5, 'Room513_Enemy', (3.9, high, 29.5), math.pi/2)
enemy(5, 'CornerEnemyNear', (17.3, high, 9.6), -math.pi/2)
enemy(5, 'CornerEnemyFar', (22.1, high, 9.2), -math.pi/2)
enemy(4, 'Room406_Enemy', (44.4, low, 3.7), 0)
build_lobby()
build_lower_stairs()

bpy.context.view_layer.update()
for floor in (5, 4, 3, 2, 1):
    layer = scene.view_layers.new(f'F{floor}_Edit')
    for child in layer.layer_collection.children:
        child.exclude = child.name != f'F{floor}_Hotel'

for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type != 'VIEW_3D':
            continue
        space = area.spaces.active
        space.clip_start = .005
        space.overlay.grid_scale = .1
        space.shading.color_type = 'MATERIAL'
        view = space.region_3d
        view.view_perspective = 'ORTHO'
        view.view_rotation = (1, 0, 0, 0)
        view.view_location = TO_BLENDER.to_3x3() @ Vector((24, high, 19))
        view.view_distance = 68

OUTPUT.mkdir(parents=True, exist_ok=True)
scene.yan_level.project_root = '//../../../'
bpy.context.window.view_layer = scene.view_layers['F5_Edit']
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / 'stage04.blend'))
bpy.context.window.view_layer = scene.view_layers['ViewLayer']
exporter.export_level(bpy.context)
metadata = {'floors': [0, 3.8, 7.6, low, high],
            'rooms': [{'number': floor*100+i, 'bounds': (10.5, 18, 26, 40) if floor == 5 and i == 16 else bounds}
                      for floor in (4, 5) for i, (bounds, _, _) in enumerate(rooms, 1)
                      if floor != 5 or i != 14]}
(OUTPUT / 'stage04-layout.json').write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8')
