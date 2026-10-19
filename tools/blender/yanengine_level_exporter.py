"""YanEngine level authoring. Blender 4.4+; install this file as a legacy add-on."""
bl_info = {"name": "YanEngine Level", "author": "YanEngine", "version": (1, 12, 0),
           "blender": (4, 4, 0), "location": "View3D > Sidebar > YanEngine Level", "category": "Import-Export"}
import bpy
from bpy.app.handlers import persistent
from bpy.props import BoolProperty, StringProperty, EnumProperty, FloatProperty, IntProperty, PointerProperty, CollectionProperty
from mathutils import Matrix, Vector
import json
import math
import os
from pathlib import Path
import re
import shutil
import uuid

# Blender -> glTF: (x,z,-y). Model.cpp mirrors glTF X for vertices AND node transforms.
# Thus engine point=(-x,z,-y). Matrix conjugation handles parent transforms and rotations.
ENGINE_BASIS = Matrix(((-1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))


def finite(values, label):
    if any(not math.isfinite(float(v)) or abs(float(v)) > 1e6 for v in values):
        raise ValueError(f"{label}: invalid number (NaN/Infinity/out of range)")


def engine_transform(matrix):
    """The sole coordinate conversion helper. Returns engine TRS and column-vector matrix."""
    converted = ENGINE_BASIS @ matrix @ ENGINE_BASIS.inverted()
    position, quaternion, scale = converted.decompose()
    finite([v for row in converted for v in row], "Transform")
    if min(scale) <= 1e-5:
        raise ValueError("Gameplay transforms require positive nonzero scale; apply mirrored scale first")
    rebuilt = Matrix.LocRotScale(position, quaternion, scale)
    if max(abs(converted[i][j] - rebuilt[i][j]) for i in range(4) for j in range(4)) > 1e-4:
        raise ValueError("Sheared gameplay transform; apply transforms or remove nonuniform scaled parent")
    return {"position": list(position), "rotation": list(quaternion.to_euler('XYZ')), "scale": list(scale)}, converted


def bounds(obj, depsgraph):
    if obj.type == 'EMPTY':
        if obj.empty_display_type != 'CUBE':
            raise ValueError(f"{obj.name}: use a Cube Empty for volume roles")
        s = obj.empty_display_size
        points = [Vector((x*s, y*s, z*s)) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
    elif obj.type == 'MESH':
        evaluated = obj.evaluated_get(depsgraph)
        points = [Vector(p) for p in evaluated.bound_box]
    else:
        raise ValueError(f"{obj.name}: volume requires a Mesh or Cube Empty")
    points = [(ENGINE_BASIS @ p.to_4d()).to_3d() for p in points]
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    finite(low+high, obj.name)
    if min(high[i]-low[i] for i in range(3)) <= 1e-5:
        raise ValueError(f"{obj.name}: collider/volume size is zero")
    return {"min": low, "max": high}


def box_data(obj, depsgraph):
    trs, matrix = engine_transform(obj.matrix_world)
    return dict(trs, localBounds=bounds(obj, depsgraph)), matrix


def trigger_volume(obj, depsgraph):
    data, matrix = box_data(obj, depsgraph)
    # Existing trigger systems are AABBs. Reject tilted volumes rather than silently inflate them.
    rotation = matrix.to_3x3().normalized()
    for row in rotation:
        if sum(abs(v) > 1e-4 for v in row) != 1:
            raise ValueError(f"{obj.name}: Trigger/Goal must be axis-aligned (90 degree rotations allowed)")
    b = data['localBounds']
    corners = [matrix @ Vector((x, y, z, 1)) for x in (b['min'][0], b['max'][0])
               for y in (b['min'][1], b['max'][1]) for z in (b['min'][2], b['max'][2])]
    low = [min(p[i] for p in corners) for i in range(3)]
    high = [max(p[i] for p in corners) for i in range(3)]
    return {"position": [(a+b)*.5 for a, b in zip(low, high)], "size": [b-a for a, b in zip(low, high)]}


def object_id(obj):
    return obj.yan_level.identifier.strip() or obj.name


@persistent
def preserve_legacy_spawns(_=None):
    """Mark old authoring data in memory; never save or distribute shared totals.

    RNA defaults apply to newly placed objects. Files without the new properties
    remain location/pool + shared trigger waves, including weighted random order.
    """
    for obj in bpy.data.objects:
        cfg = obj.yan_level
        if cfg.role == 'ENEMY' and not cfg.is_property_set('spawn_activation'):
            cfg.spawn_activation = 'LEGACY'
        if cfg.role == 'TRIGGER' and not cfg.is_property_set('trigger_mode'):
            cfg.trigger_mode = 'LEGACY'


def initialize_legacy_spawns():
    # Blender runs register() with _RestrictData, including during installation.
    # Access loaded objects only after that registration context has ended.
    preserve_legacy_spawns()
    return None


def spawn_total(cfg):
    if cfg.spawn_activation != 'LEGACY' and cfg.spawn_composition_mode == 'COUNTS':
        return sum(row.count for row in cfg.enemy_composition)
    return cfg.spawn_count


def composition_data(cfg, enemies, label):
    if not 1 <= len(cfg.enemy_composition) <= 10000:
        raise ValueError(f'{label}: 敵の種類と体数を1行以上追加してください')
    entries = []
    for row in cfg.enemy_composition:
        identifier = row.identifier.strip()
        if identifier not in enemies:
            raise ValueError(f'{label}: 存在しない敵定義: {identifier}')
        count = row.get('count', row.count)
        if type(count) is not int or not 1 <= count <= 10000:
            raise ValueError(f'{label}: 種類ごとの体数は1～10000の整数で指定してください')
        entries.append({'enemy': identifier, 'count': count})
    if sum(row['count'] for row in entries) > 10000:
        raise ValueError(f'{label}: 総出現数は10000体以下にしてください')
    return entries


def composition_changed(self, context):
    if self.spawn_composition_mode == 'COUNTS' and not self.enemy_composition:
        row = self.enemy_composition.add()
        row.identifier, row.count = self.enemy_definition, self.spawn_count


def pool_data(pool, label, known=None):
    entries = []
    for row in pool:
        key = row.identifier.strip()
        finite([row.weight], label)
        if not key or row.weight < 0:
            raise ValueError(f"{label}: empty ID or negative weight")
        if known is not None and key not in known:
            raise ValueError(f"{label}: unknown ID {key}")
        entries.append({"id": key, "weight": row.weight})
    if not entries or sum(e['weight'] for e in entries) <= 0:
        raise ValueError(f"{label}: add a pool entry with positive weight")
    return entries


_definition_cache = {}


def definitions(scene, key):
    root = scene.yan_level.project_root
    if not root:
        raise ValueError('Set Project Root to load Enemy / Weapon definitions')
    path = Path(bpy.path.abspath(root)) / 'resources' / 'Data' / f'{key}.json'
    try:
        stat = path.stat()
        stamp = (stat.st_mtime_ns, stat.st_size)
        cached = _definition_cache.get(str(path))
        if cached and cached[0] == stamp:
            return cached[1]
        rows = json.loads(path.read_text(encoding='utf-8-sig'))[key]
        if not isinstance(rows, list):
            raise ValueError(f'{key} must be an array')
        result = {}
        for entry in rows:
            identifier = entry['id']
            if not isinstance(identifier, str) or not identifier.strip() or identifier in result:
                raise ValueError(f'Empty or duplicate ID: {identifier}')
            if key == 'weapons':
                for field, default in (('slot', 'Main'), ('rarity', 1), ('type', 'Unknown'), ('weight', 1)):
                    entry.setdefault(field, default)
                if entry['slot'] not in {'Main', 'Sub'} or type(entry['rarity']) is not int or not 1 <= entry['rarity'] <= 5:
                    raise ValueError(f'{identifier}: invalid slot or rarity')
                if not isinstance(entry['type'], str) or not entry['type']:
                    raise ValueError(f'{identifier}: invalid type')
                if not isinstance(entry['weight'], (int, float)) or not math.isfinite(entry['weight']) or not 0 <= entry['weight'] <= 1e9:
                    raise ValueError(f'{identifier}: invalid weight')
            result[identifier] = entry
        _definition_cache[str(path)] = (stamp, result)
        return result
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ValueError(f'Cannot load {path}: {error}') from error


def definition_label(entry, weapon=False):
    identifier = entry['id']
    name = entry.get('displayName') or identifier
    label = f'{name} ({identifier})' if name != identifier else identifier
    if weapon:
        label += f" / {entry.get('slot', '?')} / ★{entry.get('rarity', '?')} / {entry.get('type', '?')}"
    return label


def weapon_filter(cfg):
    low, high = int(cfg.min_rarity), int(cfg.max_rarity)
    if low > high:
        raise ValueError('Min Rarity must be <= Max Rarity')
    result = {'minRarity': low, 'maxRarity': high}
    if cfg.weapon_slot != 'ALL':
        result['slot'] = cfg.weapon_slot
    if cfg.weapon_types:
        result['types'] = [entry.name for entry in cfg.weapon_types]
    return result


def matched_weapons(cfg, weapons):
    """Return effective pool (including zero weights), in runtime candidate order."""
    filters = weapon_filter(cfg)
    source = pool_data(cfg.pool, 'Weapon Pool', weapons) if cfg.pool else [
        {'id': key, 'weight': entry.get('weight', 1)} for key, entry in weapons.items()]
    matches = []
    for candidate in source:
        weapon = weapons[candidate['id']]
        if ('slot' not in filters or weapon['slot'] == filters['slot']) and \
                filters['minRarity'] <= weapon['rarity'] <= filters['maxRarity'] and \
                ('types' not in filters or weapon['type'] in filters['types']):
            finite([candidate['weight']], 'Weapon weight')
            if candidate['weight'] < 0:
                raise ValueError('Negative weapon weight')
            matches.append(candidate)
    return matches


def build_level(scene, depsgraph):
    # Scripts may validate/export immediately after enabling, before timers run.
    if bpy.app.timers.is_registered(initialize_legacy_spawns):
        initialize_legacy_spawns()
        bpy.app.timers.unregister(initialize_legacy_spawns)
    settings = scene.yan_level
    stage_id = settings.stage_id.strip()
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]*', stage_id):
        raise ValueError("Stage ID must start with a letter; use letters, digits, '-' or '_'")
    objects = sorted((o for o in scene.objects if o.yan_level.role != 'IGNORE'), key=lambda o: o.name)
    ids = set()
    for obj in objects:
        key = object_id(obj)
        if key in ids:
            raise ValueError(f'IDが重複しています: {key}')
        ids.add(key)
        finite([v for row in obj.matrix_world for v in row], obj.name)
    players = [o for o in objects if o.yan_level.role == 'PLAYER']
    if len(players) != 1:
        raise ValueError(f"Exactly one Player Spawn required (found {len(players)})")
    enemies = definitions(scene, 'enemies') if any(o.yan_level.role == 'ENEMY' for o in objects) else {}
    weapons = definitions(scene, 'weapons') if any(o.yan_level.role == 'WEAPON' for o in objects) else {}
    data = {"version": 1, "stage": {"id": stage_id, "model": f"levels/{stage_id}/{stage_id}.gltf"},
            "playerSpawn": {}, "colliders": [], "enemyRandom": {"useFixedSeed": settings.fixed_seed, "seed": settings.seed},
            "spawnPoints": [], "spawnTriggers": [], "spawnGroups": [], "weaponRandom": {"useFixedSeed": settings.fixed_seed, "seed": settings.seed},
            "weaponSpawnPoints": [], "goalTriggers": []}
    groups = {}
    if settings.player_gravity:
        data['stage']['playerGravity'] = True
    geometry = []
    for obj in objects:
        cfg = obj.yan_level
        key = object_id(obj)
        if cfg.role == 'SHUTTER':
            if obj.type != 'MESH' or cfg.shutter_button is None or cfg.shutter_button.name not in scene.objects:
                raise ValueError(f'{key}: Shutter requires a mesh and a switch object in this scene')
            box, matrix = box_data(obj, depsgraph)
            b = box['localBounds']
            corners = [matrix @ Vector((x,y,z,1)) for x in (b['min'][0],b['max'][0])
                       for y in (b['min'][1],b['max'][1]) for z in (b['min'][2],b['max'][2])]
            origin = [(min(p[i] for p in corners)+max(p[i] for p in corners))*.5 for i in range(3)]
            origin[1] = min(p[1] for p in corners)
            box['position'] = [a-b for a,b in zip(box['position'],origin)]
            button, _ = engine_transform(cfg.shutter_button.matrix_world)
            finite([cfg.shutter_duration,cfg.shutter_distance,cfg.shutter_reach],key)
            if min(cfg.shutter_duration,cfg.shutter_distance,cfg.shutter_reach)<=0:
                raise ValueError(f'{key}: Shutter timing and distances must be positive')
            markers = json.loads(cfg.shutter_progress_events)
            if not isinstance(markers,list):
                raise ValueError(f'{key}: Progress Events must be a JSON array')
            for marker in markers:
                if not 0<float(marker['progress'])<1 or not isinstance(marker['event'],str) or not marker['event']:
                    raise ValueError(f'{key}: Invalid progress event')
            shutters = data.setdefault('shutters',[])
            shutters.append(dict(id=key,model=f'levels/{stage_id}/shutter_{len(shutters):03}.gltf',
                closedPosition=origin,buttonPosition=button['position'],collider=box,
                openDuration=cfg.shutter_duration,openDistance=cfg.shutter_distance,interactionDistance=cfg.shutter_reach,
                sounds={'switch':cfg.shutter_switch_sound,'start':cfg.shutter_start_sound,
                        'motor':cfg.shutter_motor_sound,'complete':cfg.shutter_complete_sound},progressEvents=markers))
            continue
        if cfg.role == 'STATIC':
            if obj.type != 'MESH':
                raise ValueError(f"{key}: Static Mesh requires a Mesh object")
            geometry.append(obj)
            if cfg.collision == 'CUSTOM':
                target = cfg.custom_collider
                if target is None or target.name not in scene.objects or target.yan_level.role != 'COLLIDER':
                    raise ValueError(f"{key}: Custom collision requires a Collider object in this scene")
            if cfg.collision != 'BOX':
                continue
        if cfg.role == 'COLLIDER' or (cfg.role == 'STATIC' and cfg.collision == 'BOX'):
            box, _ = box_data(obj, depsgraph)
            collider = dict(id=key, **box)
            if cfg.walkable:
                collider['walkable'] = True
            if cfg.player_only:
                collider['playerOnly'] = True
            data['colliders'].append(collider)
        elif cfg.role in {'PLAYER', 'ENEMY', 'WEAPON'}:
            trs, _ = engine_transform(obj.matrix_world)
            pose = {"position": trs['position'], "rotation": trs['rotation']}
            if cfg.role == 'PLAYER':
                data['playerSpawn'] = pose
            elif cfg.role == 'ENEMY':
                point = dict(id=key, **pose)
                if cfg.spawn_activation == 'LEGACY':
                    if not cfg.group.strip():
                        raise ValueError(f'{key}: 旧形式の敵出現地点にはグループIDが必要です')
                    groups.setdefault(cfg.group.strip(), []).append(key)
                    point['enemyPool'] = pool_data(cfg.pool, key, enemies)
                else:
                    composition = composition_data(cfg, enemies, key) if cfg.spawn_composition_mode == 'COUNTS' else None
                    enemy_id = cfg.enemy_definition.strip()
                    if composition is None and enemy_id not in enemies:
                        raise ValueError(f'{key}: 存在しない敵定義: {enemy_id}')
                    # Inspect raw saved values too: RNA may clamp malformed stored
                    # values on access, hiding corruption from Validate otherwise.
                    for field in (('max_alive',) if composition is not None else ('spawn_count', 'max_alive')):
                        value = cfg.get(field, getattr(cfg, field))
                        if type(value) is not int or not 1 <= value <= 10000:
                            raise ValueError(f'{key}: 総出現数・同時生存上限は1～10000の整数で指定してください')
                    for field in ('spawn_interval', 'initial_delay'):
                        value = cfg.get(field, getattr(cfg, field))
                        if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1e6:
                            raise ValueError(f'{key}: 出現間隔・初回待機時間は0～1000000秒で指定してください')
                    if cfg.spawn_activation == 'OnTrigger' and not cfg.group.strip():
                        raise ValueError(f'{key}: トリガー起動にはグループIDが必要です')
                    point.update(activation=cfg.spawn_activation,
                                 spawnCount=spawn_total(cfg), maxAlive=cfg.max_alive, mode=cfg.spawn_mode,
                                 spawnInterval=cfg.spawn_interval, initialDelay=cfg.initial_delay)
                    if composition is not None:
                        point['enemyComposition'] = composition
                        point['enemyPool'] = [{'id': row['enemy'], 'weight': row['count']} for row in composition]
                    else:
                        point.update(enemy=enemy_id, enemyPool=[{'id': enemy_id, 'weight': 1}])
                    if cfg.spawn_activation == 'OnTrigger':
                        point['groupId'] = cfg.group.strip()
                data['spawnPoints'].append(point)
            else:
                candidates = matched_weapons(cfg, weapons)
                if not candidates or sum(p['weight'] for p in candidates) <= 0:
                    raise ValueError(f'{key}: no positive-weight weapons match Filter')
                point = dict(id=key, **pose, filter=weapon_filter(cfg))
                if cfg.pool:
                    point['weaponPool'] = pool_data(cfg.pool, key, weapons)
                data['weaponSpawnPoints'].append(point)
        elif cfg.role == 'GOAL':
            data['goalTriggers'].append(dict(id=key, **trigger_volume(obj, depsgraph)))
    for obj in objects:
        cfg = obj.yan_level
        if cfg.role == 'SPAWN_GROUP':
            finite([cfg.start_time, cfg.spawn_interval], obj.name)
            if cfg.start_time < 0 or cfg.spawn_interval < 0 or not 1 <= len(cfg.members) <= 10000:
                raise ValueError(f'{obj.name}: Spawn Group requires valid timing and 1 to 10000 enemies')
            members = []
            for entry in cfg.members:
                point = entry.spawn_point
                if point is None or point.name not in scene.objects or point.yan_level.role != 'ENEMY':
                    raise ValueError(f'{obj.name}: choose an Enemy Spawn in this scene for every member')
                identifier = entry.identifier.strip()
                if identifier not in enemies:
                    raise ValueError(f'{obj.name}: unknown enemy ID {identifier}')
                members.append({'enemy': identifier, 'spawnPoint': object_id(point)})
            data['spawnGroups'].append(dict(id=object_id(obj), time=cfg.start_time, mode=cfg.spawn_mode,
                interval=cfg.spawn_interval, enemies=members))
            continue
        if cfg.role != 'TRIGGER':
            continue
        if cfg.trigger_mode == 'GROUP':
            group = cfg.group.strip()
            if not group or not any(p.get('activation') == 'OnTrigger' and p.get('groupId') == group for p in data['spawnPoints']):
                raise ValueError(f'{obj.name}: 同じグループIDの「トリガー起動」敵出現地点が必要です')
            finite([cfg.initial_delay], obj.name)
            if not 0 <= cfg.initial_delay <= 1e6:
                raise ValueError(f'{obj.name}: 起動遅延は0～1000000秒で指定してください')
            data['spawnTriggers'].append(dict(id=object_id(obj), **trigger_volume(obj, depsgraph),
                groupId=group, condition=cfg.trigger_condition, initialDelay=cfg.initial_delay, oneShot=cfg.one_shot))
            continue
        points = groups.get(cfg.group.strip(), [])
        if any(p.get('activation') == 'OnTrigger' and p.get('groupId') == cfg.group.strip() for p in data['spawnPoints']):
            raise ValueError(f'{obj.name}: 旧形式と独立設定の地点が同じグループに混在しています。トリガー方式と各地点を確認してください')
        if not points:
            raise ValueError(f"{obj.name}: Spawn Group '{cfg.group}' has no Enemy Spawn")
        finite([cfg.spawn_interval, cfg.initial_delay], obj.name)
        if cfg.spawn_count < 1 or cfg.max_alive < 1 or cfg.spawn_interval < 0 or cfg.initial_delay < 0:
            raise ValueError(f"{obj.name}: invalid count/timing")
        data['spawnTriggers'].append(dict(id=object_id(obj), **trigger_volume(obj, depsgraph), spawnPointIds=points,
            spawnCount=cfg.spawn_count, spawnInterval=cfg.spawn_interval, initialDelay=cfg.initial_delay,
            maxAlive=cfg.max_alive, selection=cfg.selection, oneShot=cfg.one_shot))
    if not geometry:
        raise ValueError("At least one Static Mesh is required")
    letters = [obj for obj in objects if obj.yan_level.role == 'START_LETTER']
    if stage_id == 'title':
        if len(data['spawnPoints']) != 1:
            raise ValueError('Title requires exactly one Enemy Spawn (no Spawn Trigger needed)')
        if len(letters) != 1 or letters[0].type != 'MESH':
            raise ValueError('Title requires one Game Start Mesh: convert Text to Mesh, then join all letters (Ctrl+J)')
        weapon_id = settings.title_weapon.strip()
        if weapon_id not in definitions(scene, 'weapons'):
            raise ValueError('Unknown Title Weapon ID')
        data['title'] = {'weaponId': weapon_id, 'explosionDelay': settings.title_start_delay,
                         'startObject': dict(id=object_id(letters[0]), partAsset='resources/levels/title/game_start.enemy.json')}
    elif letters:
        raise ValueError('Game Start objects require Stage ID title')
    json.dumps(data, allow_nan=False)
    return data, geometry


def output_path(scene):
    cfg = scene.yan_level
    root = Path(bpy.path.abspath(cfg.project_root)).resolve() if cfg.project_root else None
    output = Path(bpy.path.abspath(cfg.output_directory)) if cfg.output_directory.startswith('//') else Path(cfg.output_directory)
    if not output.is_absolute():
        if root is None:
            raise ValueError("Set Project Root, or an absolute Output Directory")
        output = root / output
    output = output.resolve()
    if output.name != cfg.stage_id:
        raise ValueError("Output Directory's last folder must match Stage ID")
    if root and output != root / 'resources' / 'levels' / cfg.stage_id:
        raise ValueError("Use Project Root/resources/levels/StageID as output so game model paths resolve")
    return output


def export_start_head(obj, filepath):
    """World-baked single mesh -> existing EnemyAsset format, entirely Head.

    The level exporter assigns all faces automatically; no Enemy Parts add-on
    or per-letter objects are required for the title start target.
    """
    mesh = obj.data
    mesh.calc_loop_triangles()
    basis = ENGINE_BASIS.to_3x3()
    uv = mesh.uv_layers.active
    triangles = []
    for triangle in mesh.loop_triangles:
        vertices = []
        for loop_index in reversed(triangle.loops):
            loop = mesh.loops[loop_index]
            p = basis @ mesh.vertices[loop.vertex_index].co
            n = basis @ mesh.corner_normals[loop_index].vector
            texcoord = uv.data[loop_index].uv if uv else (0, 0)
            finite(list(p)+list(n)+list(texcoord), obj.name)
            vertices.append(dict(p=list(p), n=list(n), uv=[float(texcoord[0]), 1-float(texcoord[1])]))
        triangles.append(dict(face=triangle.polygon_index, vertices=vertices))
    if not triangles or len(triangles)>1000000:
        raise ValueError('GAME START requires 1..1000000 triangles')
    data = dict(version=1, coordinateSystem='yanengine', texture='resources/white1x1.png', hpGroups=[],
        parts=[dict(name='Head', role='Head', localHp=1, sharedHpGroup='', sharedDamageRate=1,
                    breakable=True, deathOnZero=True, faces=[p.index for p in mesh.polygons], triangles=triangles)])
    filepath.write_text(json.dumps(data, separators=(',', ':'), allow_nan=False), encoding='utf-8')


def publish_level(scratch, target):
    """Release Blender's own Windows directory lock while swapping the output."""
    backup = target.with_name(f'.{target.name}-backup-{uuid.uuid4().hex}')
    previous_directory = Path.cwd()
    relocate = previous_directory == target or target in previous_directory.parents
    try:
        if relocate:
            os.chdir(target.parent)
        # Publish the entire dependency set together, with rollback on rename failure.
        if target.exists():
            os.replace(target, backup)
        try:
            os.replace(scratch, target)
        except Exception:
            if backup.exists():
                os.replace(backup, target)
            raise
        if backup.exists():
            shutil.rmtree(backup)
    finally:
        # A failed rollback must retain its original error and recovery backup.
        if relocate and previous_directory.is_dir():
            os.chdir(previous_directory)


def export_level(context):
    if context.mode != 'OBJECT':
        raise ValueError("Switch to Object Mode before export")
    context.view_layer.update()
    depsgraph = context.evaluated_depsgraph_get()
    data, geometry = build_level(context.scene, depsgraph)
    target = output_path(context.scene)
    target.parent.mkdir(parents=True, exist_ok=True)
    scratch = target.with_name(f'.{target.name}-export-{uuid.uuid4().hex}')
    scratch.mkdir()
    temporary_scene = None
    source_scene = context.window.scene
    try:
        # Preserve unrelated author files; only generated stage assets are replaced.
        if target.exists():
            shutil.copytree(target, scratch, dirs_exist_ok=True)
        temporary_scene = bpy.data.scenes.new('__YanEngineExport')
        context.window.scene = temporary_scene
        # Bake evaluated geometry to world space, isolated from parent/helper visibility.
        # glTF conversion then Model.cpp's X reflection equals ENGINE_BASIS exactly.
        letters = sorted((obj for obj in source_scene.objects if obj.yan_level.role == 'START_LETTER'), key=lambda obj: obj.name)
        exported = {}
        shutters = sorted((obj for obj in source_scene.objects if obj.yan_level.role == 'SHUTTER'), key=lambda obj: obj.name)
        for source in geometry + letters + shutters:
            mesh = bpy.data.meshes.new_from_object(source.evaluated_get(depsgraph), depsgraph=depsgraph)
            finite([v for vertex in mesh.vertices for v in vertex.co], source.name)
            mesh.transform(source.matrix_world)
            if source in shutters:
                definition = next(s for s in data['shutters'] if s['id']==object_id(source))
                origin = (ENGINE_BASIS.inverted() @ Vector((*definition['closedPosition'],0))).to_3d()
                mesh.transform(Matrix.Translation(-origin))
            if source.matrix_world.determinant() < 0:
                mesh.flip_normals()
            obj = bpy.data.objects.new(source.name, mesh)
            temporary_scene.collection.objects.link(obj)
            exported[source.name] = obj
        context.view_layer.update()
        static_objects = {exported[source.name] for source in geometry}
        for obj in temporary_scene.objects:
            obj.select_set(obj in static_objects)
        path = scratch / f"{data['stage']['id']}.gltf"
        result = bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLTF_SEPARATE',
            use_active_scene=True, use_selection=True, export_yup=True, export_animations=False, export_skins=False,
            export_cameras=False, export_lights=False, export_extras=False)
        if 'FINISHED' not in result:
            raise RuntimeError("glTF export did not finish")
        gltf = json.loads(path.read_text(encoding='utf-8'))
        if not gltf.get('meshes'):
            raise ValueError("Exported glTF contains no meshes")
        for entry in gltf.get('buffers', []) + gltf.get('images', []):
            uri = entry.get('uri', '')
            if uri and not uri.startswith('data:') and not (scratch / uri).is_file():
                raise ValueError(f"Missing exported dependency: {uri}")
        for source in shutters:
            for obj in temporary_scene.objects:
                obj.select_set(obj == exported[source.name])
            definition = next(s for s in data['shutters'] if s['id']==object_id(source))
            dynamic_path = scratch / Path(definition['model']).name
            result = bpy.ops.export_scene.gltf(filepath=str(dynamic_path), export_format='GLTF_SEPARATE',
                use_active_scene=True, use_selection=True, export_yup=True, export_animations=False, export_skins=False,
                export_cameras=False, export_lights=False, export_extras=False)
            if 'FINISHED' not in result:
                raise RuntimeError('Shutter glTF export did not finish')
        # GAME START is one Head asset, rendered and shattered by the ordinary Enemy.
        for source in letters:
            export_start_head(exported[source.name], scratch/'game_start.enemy.json')
        if letters:
            # Remove only the previous exporter's per-letter generated files.
            for old_file in scratch.iterdir():
                if re.fullmatch(r'start_\d{2,3}\.(gltf|bin)', old_file.name):
                    old_file.unlink()
        (scratch / f"{data['stage']['id']}.json").write_text(json.dumps(data, indent=2, allow_nan=False)+'\n', encoding='utf-8')
        publish_level(scratch, target)
        return target
    finally:
        context.window.scene = source_scene
        if temporary_scene:
            for obj in list(temporary_scene.objects):
                mesh = obj.data
                bpy.data.objects.remove(obj, do_unlink=True)
                if mesh.users == 0:
                    bpy.data.meshes.remove(mesh)
            bpy.data.scenes.remove(temporary_scene)
        if scratch.exists():
            shutil.rmtree(scratch)


def role_changed(self, context):
    obj = self.id_data
    if not isinstance(obj, bpy.types.Object):
        return
    # Persist defaults for new placements so a saved new point is distinguishable
    # from an old .blend which never had these properties.
    if self.role == 'ENEMY' and not self.is_property_set('spawn_activation'):
        self.spawn_activation = 'StageStart'
    if self.role == 'TRIGGER' and not self.is_property_set('trigger_mode'):
        self.trigger_mode = 'GROUP'
    if obj.type == 'EMPTY':
        obj.empty_display_type = 'CUBE' if self.role in {'COLLIDER', 'TRIGGER', 'GOAL'} else 'ARROWS'
        obj.empty_display_size = 1.0
    if self.role == 'COLLIDER':
        obj.display_type = 'WIRE'
    colors = {'PLAYER': (.1, 1, .2, 1), 'ENEMY': (1, .2, .2, 1), 'WEAPON': (1, .8, .1, 1),
              'TRIGGER': (.1, .7, 1, 1), 'GOAL': (1, .2, 1, 1)}
    obj.color = colors.get(self.role, (1, 1, 1, 1))


class YAN_PoolEntry(bpy.types.PropertyGroup):
    identifier: StringProperty(name="ID", default="normal")
    weight: FloatProperty(name="Weight", default=1, min=0)


class YAN_WeaponType(bpy.types.PropertyGroup):
    # Store actual strings, never enum indices/bit masks tied to definition order.
    name: StringProperty()


def spawn_point_poll(self, obj):
    return obj.yan_level.role == 'ENEMY'


class YAN_SpawnGroupEnemy(bpy.types.PropertyGroup):
    identifier: StringProperty(name='Enemy ID', default='normal')
    spawn_point: PointerProperty(name='Spawn Point', type=bpy.types.Object, poll=spawn_point_poll)


class YAN_EnemySpawnCount(bpy.types.PropertyGroup):
    identifier: StringProperty(name='敵定義', default='normal')
    count: IntProperty(name='体数', default=1, min=1, max=10000)


class YAN_ObjectSettings(bpy.types.PropertyGroup):
    role: EnumProperty(name="オブジェクト種別", default='IGNORE', update=role_changed, items=[
        ('STATIC', '固定メッシュ', ''), ('COLLIDER', '当たり判定', ''), ('PLAYER', 'プレイヤー出現地点', ''),
        ('ENEMY', '敵出現地点', ''), ('TRIGGER', '出現起動トリガー', ''), ('WEAPON', '武器出現地点', ''),
        ('GOAL', 'ゴール', ''), ('IGNORE', '対象外', ''),
        ('START_LETTER', 'Game Start (Head)', 'Title only; join the whole text into one Mesh'),
        ('SPAWN_GROUP', '時刻指定の出現グループ', 'ステージ開始からの時刻で敵を出現させます'),
        ('SHUTTER', 'Shutter', 'Independent upward-moving mesh and box collision')])
    identifier: StringProperty(name="ID", description="空欄ならオブジェクト名を使用します")
    group: StringProperty(name="出現グループID", default="A")
    spawn_activation: EnumProperty(name='起動方法', default='StageStart', items=[
        ('LEGACY', '旧形式（互換）', '旧トリガーの共有出現設定または時刻指定グループを使用します'),
        ('StageStart', 'ステージ開始時', 'トリガー不要。初回待機時間の後に出現します'),
        ('OnTrigger', 'トリガー起動', '同じグループIDのトリガーから起動します'),
        ('Manual', '手動・外部イベント', '出現IDを指定した共通APIから起動します')])
    enemy_definition: StringProperty(name='敵定義', default='normal')
    spawn_composition_mode: EnumProperty(name='敵の設定', default='SINGLE', update=composition_changed, items=[
        ('SINGLE', '1種類', '選択した敵を総出現数だけ出現させます'),
        ('COUNTS', '種類ごとの体数', '例：通常3体＋高速2体。順次出現ではリストの上から出現します')])
    enemy_composition: CollectionProperty(type=YAN_EnemySpawnCount)
    trigger_mode: EnumProperty(name='トリガー方式', default='GROUP', items=[
        ('LEGACY', '旧形式（共有出現設定）', '既存ステージの総数・抽選順・タイミングを維持します'),
        ('GROUP', 'グループを起動', '対応する敵出現地点が各自の設定に従って出現します')])
    trigger_condition: EnumProperty(name='起動条件', default='PlayerEnter', items=[
        ('PlayerEnter', 'プレイヤー進入', '範囲外から範囲内へ入ると起動します')])
    collision: EnumProperty(name="Collision", items=[('NONE', 'None', ''), ('BOX', 'Box', ''), ('CUSTOM', 'Custom', '')])
    custom_collider: PointerProperty(name="Collider Object", type=bpy.types.Object)
    walkable: BoolProperty(name='Walkable Ramp', description='Smooth floor support proxy; never enable on walls or ceilings', default=False)
    player_only: BoolProperty(name='Player Only', description='Block the player but allow enemies and shots through', default=False)
    pool: CollectionProperty(type=YAN_PoolEntry)
    weapon_slot: EnumProperty(name='Slot', items=[('ALL', 'All', ''), ('Main', 'Main', ''), ('Sub', 'Sub', '')], default='ALL')
    min_rarity: EnumProperty(name='Min Rarity', items=[(str(i), f'★{i}', '') for i in range(1, 6)], default='1')
    max_rarity: EnumProperty(name='Max Rarity', items=[(str(i), f'★{i}', '') for i in range(1, 6)], default='5')
    weapon_types: CollectionProperty(type=YAN_WeaponType)
    spawn_count: IntProperty(name="総出現数", default=5, min=1, max=10000)
    spawn_interval: FloatProperty(name="出現間隔（秒）", default=.5, min=0, max=1e6)
    initial_delay: FloatProperty(name="初回待機時間（秒）", default=0, min=0, max=1e6)
    max_alive: IntProperty(name="同時生存上限", default=5, min=1, max=10000)
    selection: EnumProperty(name="地点の選択", items=[('Random', 'ランダム', ''), ('RoundRobin', '順番', '')])
    one_shot: BoolProperty(name="一度だけ起動", default=True)
    spawn_mode: EnumProperty(name='出現方式', default='Sequential', items=[
        ('Sequential', '順次出現', '設定した出現間隔で順番に出現します'),
        ('Simultaneous', '同時出現', '同じフレームに出現。生存上限を超える分は空きを待ちます')])
    start_time: FloatProperty(name='開始時刻（秒）', description='ステージ開始からのゲーム内秒数', default=0, min=0)
    members: CollectionProperty(type=YAN_SpawnGroupEnemy)
    shutter_button: PointerProperty(name='Switch Object', type=bpy.types.Object)
    shutter_duration: FloatProperty(name='Open Duration (s)',default=30,min=.001)
    shutter_distance: FloatProperty(name='Open Distance (m)',default=3.2,min=.001)
    shutter_reach: FloatProperty(name='Interaction Distance (m)',default=2,min=.001)
    shutter_progress_events: StringProperty(name='Progress Events (JSON)',default='[]')
    shutter_switch_sound: StringProperty(name='Switch WAV',default='')
    shutter_start_sound: StringProperty(name='Start WAV',default='')
    shutter_motor_sound: StringProperty(name='Motor WAV (loop)',default='')
    shutter_complete_sound: StringProperty(name='Complete WAV',default='')
    auto_collider_count: IntProperty(name="Max Box Count", description="Upper limit; simple shapes use fewer boxes", default=4, min=1, max=32)
    auto_collider_padding: FloatProperty(name="Padding", description="Extra local-space margin added to generated boxes", default=0.02, min=0.0, max=10.0)


class YAN_SceneSettings(bpy.types.PropertyGroup):
    player_gravity: BoolProperty(name='プレイヤー重力', description='床の支持・落下・坂の歩行を使用します', default=False)
    stage_id: StringProperty(name="ステージID", default="stage01")
    project_root: StringProperty(name="プロジェクトの場所", description="エンジンのプロジェクトフォルダー。//はblendファイルからの相対パスです")
    output_directory: StringProperty(name="出力フォルダー", default="resources/levels/stage01", subtype='DIR_PATH')
    fixed_seed: BoolProperty(name="乱数シードを固定", default=False)
    seed: IntProperty(name="乱数シード", default=12345, min=0)
    title_weapon: StringProperty(name='Title Weapon ID', default='pistol')
    title_start_delay: FloatProperty(name='Explosion Hold Before Fade', default=.45, min=.4, max=.5)


class YAN_CatalogEntry(bpy.types.PropertyGroup):
    identifier: StringProperty()
    selected: BoolProperty(name='Select', default=False)
    slot: StringProperty()
    rarity: IntProperty()
    weapon_type: StringProperty()
    missing: BoolProperty()
    matched: BoolProperty()


class YAN_CatalogState(bpy.types.PropertyGroup):
    # WindowManager-only draft: no authoring properties change until Apply.
    target: PointerProperty(type=bpy.types.Object)
    scene: PointerProperty(type=bpy.types.Scene)
    entries: CollectionProperty(type=YAN_CatalogEntry)
    active_index: IntProperty()
    slot: EnumProperty(name='Slot', items=[('ALL', 'All', ''), ('Main', 'Main', ''), ('Sub', 'Sub', '')])
    rarity: EnumProperty(name='Rarity', items=[('ALL', 'All', '')] + [(str(i), f'★{i}', '') for i in range(1, 6)])
    weapon_type: StringProperty(default='')
    matched_only: BoolProperty(name='Only Spawn Matches', description='Use the current saved Manual Pool and Dynamic Filter; draft checks do not affect this view')
    match_error: StringProperty()


def catalog_target(context, state):
    target = state.target
    if state.scene != context.scene or target is None or context.object != target or target.yan_level.role != 'WEAPON':
        raise ValueError('Select the original Weapon Spawn, or reopen Catalog for the new selection')
    return target


def reload_catalog(context, state, initial=False):
    target = catalog_target(context, state)
    weapons = definitions(context.scene, 'weapons')
    selected = {p.identifier.strip() for p in target.yan_level.pool} if initial else {
        row.identifier for row in state.entries if row.selected}
    # Retain missing legacy IDs (and deleted draft selections) as explicit checkboxes.
    identifiers = set(weapons) | selected | {p.identifier.strip() for p in target.yan_level.pool}
    try:
        matched = {p['id'] for p in matched_weapons(target.yan_level, weapons)}
        state.match_error = ''
    except ValueError as error:
        matched = set()
        state.match_error = str(error)
    state.entries.clear()
    for identifier in sorted(identifiers, key=lambda key: (weapons.get(key, {}).get('rarity', 99), key)):
        definition = weapons.get(identifier)
        row = state.entries.add()
        row.identifier = identifier
        row.selected = identifier in selected
        row.missing = definition is None
        row.matched = identifier in matched
        if definition:
            row.slot, row.rarity, row.weapon_type = definition['slot'], definition['rarity'], definition['type']
    state.active_index = 0


def catalog_visible(state, row):
    return ((not state.matched_only or row.matched) and
            (state.slot == 'ALL' or row.slot == state.slot) and
            (state.rarity == 'ALL' or row.rarity == int(state.rarity)) and
            (not state.weapon_type or row.weapon_type == state.weapon_type))


def apply_catalog(context, state):
    target = catalog_target(context, state)
    weapons = definitions(context.scene, 'weapons')
    selected = {row.identifier for row in state.entries if row.selected}
    existing = {p.identifier.strip() for p in target.yan_level.pool}
    deleted = selected - set(weapons) - existing
    if deleted:
        raise ValueError('Definitions removed; Reload and clear missing selections: ' + ', '.join(sorted(deleted)))
    # Keep original order, strings and even duplicate rows/weights for retained IDs.
    entries = [(p.identifier, p.weight) for p in target.yan_level.pool if p.identifier.strip() in selected]
    entries.extend((row.identifier, 1.0) for row in state.entries if row.selected and row.identifier not in existing)
    target.yan_level.pool.clear()
    for identifier, weight in entries:
        row = target.yan_level.pool.add()
        row.identifier, row.weight = identifier, weight


class YAN_OT_catalog_open(bpy.types.Operator):
    bl_idname = 'yanengine.catalog_open'
    bl_label = 'Open Weapon Catalog'
    matched_only: BoolProperty(default=False)

    @classmethod
    def poll(cls, context):
        return context.object is not None and context.object.yan_level.role == 'WEAPON'

    def execute(self, context):
        state = context.window_manager.yan_catalog
        state.target, state.scene = context.object, context.scene
        state.slot, state.rarity, state.weapon_type = 'ALL', 'ALL', ''
        state.matched_only = self.matched_only
        try:
            reload_catalog(context, state, initial=True)
        except ValueError as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}
        bpy.ops.wm.call_panel('INVOKE_DEFAULT', name='YAN_PT_weapon_catalog', keep_open=True)
        return {'FINISHED'}


class YAN_OT_catalog_action(bpy.types.Operator):
    bl_idname = 'yanengine.catalog_action'
    bl_label = 'Catalog Selection'
    action: EnumProperty(items=[('SELECT', 'Select All Visible', ''), ('CLEAR_VISIBLE', 'Clear Visible', ''),
                               ('CLEAR', 'Clear All', ''), ('RELOAD', 'Reload', '')])

    def execute(self, context):
        state = context.window_manager.yan_catalog
        try:
            catalog_target(context, state)
            if self.action == 'RELOAD':
                _definition_cache.clear()
                reload_catalog(context, state)
            else:
                for row in state.entries:
                    if self.action == 'CLEAR' or catalog_visible(state, row):
                        row.selected = self.action == 'SELECT'
        except ValueError as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}
        return {'FINISHED'}


class YAN_OT_catalog_type(bpy.types.Operator):
    bl_idname = 'yanengine.catalog_type'
    bl_label = 'Catalog Type'
    identifier: StringProperty()

    def execute(self, context):
        context.window_manager.yan_catalog.weapon_type = self.identifier
        return {'FINISHED'}


class YAN_MT_catalog_types(bpy.types.Menu):
    bl_label = 'Catalog Type'

    def draw(self, context):
        self.layout.operator('yanengine.catalog_type', text='All').identifier = ''
        state = context.window_manager.yan_catalog
        for name in sorted({row.weapon_type for row in state.entries if not row.missing}):
            self.layout.operator('yanengine.catalog_type', text=name).identifier = name


class YAN_OT_catalog_apply(bpy.types.Operator):
    bl_idname = 'yanengine.catalog_apply'
    bl_label = 'Apply to Weapon Spawn'
    bl_description = 'Replace Manual Pool with all checked weapons (including hidden checks); keep existing weights and Dynamic Filter'
    bl_options = {'UNDO'}

    def execute(self, context):
        state = context.window_manager.yan_catalog
        try:
            apply_catalog(context, state)
            reload_catalog(context, state)
        except ValueError as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}
        self.report({'INFO'}, f'Applied Manual Pool to {state.target.name}')
        if context.area:
            context.area.tag_redraw()
        return {'FINISHED'}


class YAN_UL_catalog(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        row = layout.row(align=True)
        row.alert = item.missing
        row.prop(item, 'selected', text='')
        columns = row.split(factor=0.48)
        columns.label(text=f'Missing / Unknown: {item.identifier}' if item.missing else f'★{item.rarity}  {item.identifier}')
        details = columns.split(factor=0.3)
        details.label(text=item.slot)
        details.label(text=item.weapon_type)

    def filter_items(self, context, data, propname):
        return ([self.bitflag_filter_item if catalog_visible(data, row) else 0
                 for row in getattr(data, propname)], [])


class YAN_PT_weapon_catalog(bpy.types.Panel):
    bl_label = 'Weapon Catalog'
    bl_idname = 'YAN_PT_weapon_catalog'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'WINDOW'
    bl_ui_units_x = 34

    def draw(self, context):
        layout = self.layout
        state = context.window_manager.yan_catalog
        try:
            target = catalog_target(context, state)
        except ValueError as error:
            layout.label(text=str(error), icon='ERROR')
            return
        layout.label(text=f'Weapon Spawn: {target.name}')
        layout.label(text='Catalog display filters (do not change Dynamic Filter)')
        row = layout.row(align=True)
        row.prop(state, 'slot')
        row.prop(state, 'rarity')
        row.menu('YAN_MT_catalog_types', text='Type: ' + (state.weapon_type or 'All'))
        layout.prop(state, 'matched_only')
        if state.matched_only and state.match_error:
            layout.label(text=state.match_error, icon='ERROR')
        row = layout.row(align=True)
        for action, label in [('SELECT', 'Select All Visible'), ('CLEAR_VISIBLE', 'Clear Visible'), ('CLEAR', 'Clear All')]:
            row.operator('yanengine.catalog_action', text=label).action = action
        layout.template_list('YAN_UL_catalog', '', state, 'entries', state, 'active_index', rows=10)
        visible = [row for row in state.entries if catalog_visible(state, row)]
        selected = [row for row in state.entries if row.selected]
        hidden = sum(not catalog_visible(state, row) for row in selected)
        layout.label(text=f'Visible: {len(visible)}   Selected: {len(selected)} ({hidden} hidden)')
        if not visible:
            layout.label(text='No weapons match the catalog display filters', icon='INFO')
        if any(row.missing for row in selected):
            layout.label(text='Missing IDs are retained until unchecked', icon='ERROR')
        if not selected:
            layout.label(text='Empty Manual Pool: all weapons use Dynamic Filter', icon='INFO')
        layout.label(text='Apply replaces Manual Pool with all checks; Dynamic Filter is kept.')
        row = layout.row()
        row.operator('yanengine.catalog_action', text='Reload', icon='FILE_REFRESH').action = 'RELOAD'
        row.operator('yanengine.catalog_apply', icon='CHECKMARK')


class YAN_OT_pool(bpy.types.Operator):
    bl_idname = 'yanengine.pool'
    bl_label = 'Edit Pool'
    bl_options = {'UNDO'}
    index: IntProperty(default=-1)
    def execute(self, context):
        cfg = context.object.yan_level
        if self.index < 0:
            try:
                choices = definitions(context.scene, 'weapons' if cfg.role == 'WEAPON' else 'enemies')
                if not choices:
                    raise ValueError('No definitions available')
            except ValueError as error:
                self.report({'ERROR'}, str(error))
                return {'CANCELLED'}
            row = cfg.pool.add()
            row.identifier = next(iter(choices))
        elif self.index < len(cfg.pool):
            cfg.pool.remove(self.index)
        return {'FINISHED'}


class YAN_OT_choose_pool(bpy.types.Operator):
    bl_idname = 'yanengine.choose_pool'
    bl_label = '敵・武器定義を選択'
    bl_options = {'UNDO'}
    index: IntProperty()
    identifier: StringProperty()

    def execute(self, context):
        cfg = context.object.yan_level
        if self.index == -1 and cfg.role == 'ENEMY':
            cfg.enemy_definition = self.identifier
            return {'FINISHED'}
        entries = cfg.enemy_composition if cfg.role == 'ENEMY' and cfg.spawn_activation != 'LEGACY' and cfg.spawn_composition_mode == 'COUNTS' else cfg.members if cfg.role == 'SPAWN_GROUP' else cfg.pool
        if 0 <= self.index < len(entries):
            entries[self.index].identifier = self.identifier
            return {'FINISHED'}
        return {'CANCELLED'}


class YAN_MT_pool_choices(bpy.types.Menu):
    bl_label = '定義を選択'

    def draw(self, context):
        cfg = context.object.yan_level
        independent = cfg.role == 'ENEMY' and cfg.spawn_activation != 'LEGACY'
        if independent and cfg.spawn_composition_mode == 'SINGLE':
            identifier, index = cfg.enemy_definition.strip(), -1
        else:
            entry = context.yan_pool_entry
            entries = cfg.enemy_composition if independent else cfg.members if cfg.role == 'SPAWN_GROUP' else cfg.pool
            index = next(i for i, row in enumerate(entries) if row.as_pointer() == entry.as_pointer())
            identifier = entry.identifier.strip()
        try:
            choices = definitions(context.scene, 'weapons' if cfg.role == 'WEAPON' else 'enemies')
            if identifier not in choices:
                self.layout.label(text=f'不明な定義: {identifier}', icon='ERROR')
            for identifier, definition in choices.items():
                op = self.layout.operator('yanengine.choose_pool', text=definition_label(definition, cfg.role == 'WEAPON'))
                op.index, op.identifier = index, identifier
        except ValueError as error:
            self.layout.label(text=str(error), icon='ERROR')


class YAN_OT_enemy_composition(bpy.types.Operator):
    bl_idname = 'yanengine.enemy_composition'
    bl_label = '敵の種類別体数を編集'
    bl_options = {'UNDO'}
    index: IntProperty(default=-1)
    move: IntProperty(default=0, min=-1, max=1)

    @classmethod
    def poll(cls, context):
        return context.object is not None and context.object.yan_level.role == 'ENEMY' and context.object.yan_level.spawn_activation != 'LEGACY'

    def execute(self, context):
        cfg = context.object.yan_level
        entries = cfg.enemy_composition
        if self.index < 0:
            try:
                choices = definitions(context.scene, 'enemies')
                if not choices or len(entries) >= 10000:
                    raise ValueError('敵定義を確認してください。追加できるのは10000行までです')
            except ValueError as error:
                self.report({'ERROR'}, str(error))
                return {'CANCELLED'}
            row = entries.add()
            row.identifier = cfg.enemy_definition if cfg.enemy_definition in choices else next(iter(choices))
        elif 0 <= self.index < len(entries):
            if self.move:
                destination = self.index + self.move
                if 0 <= destination < len(entries):
                    entries.move(self.index, destination)
            else:
                entries.remove(self.index)
        else:
            return {'CANCELLED'}
        return {'FINISHED'}


class YAN_OT_group_member(bpy.types.Operator):
    bl_idname = 'yanengine.group_member'
    bl_label = 'Edit Spawn Group Members'
    bl_options = {'UNDO'}
    index: IntProperty(default=-1)
    move: IntProperty(default=0, min=-1, max=1)

    @classmethod
    def poll(cls, context):
        return context.object is not None and context.object.yan_level.role == 'SPAWN_GROUP'

    def execute(self, context):
        members = context.object.yan_level.members
        if self.index < 0:
            try:
                choices = definitions(context.scene, 'enemies')
                if not choices:
                    raise ValueError('Enemy catalog is empty')
            except ValueError as error:
                self.report({'ERROR'}, str(error))
                return {'CANCELLED'}
            points = sorted((o for o in context.scene.objects if o.yan_level.role == 'ENEMY'), key=lambda o: o.name)
            point = points[len(members) % len(points)] if points else None
            entry = members.add()
            entry.identifier = next(iter(choices))
            entry.spawn_point = point
        elif 0 <= self.index < len(members):
            if self.move:
                destination = self.index + self.move
                if 0 <= destination < len(members):
                    members.move(self.index, destination)
            else:
                members.remove(self.index)
        else:
            return {'CANCELLED'}
        return {'FINISHED'}


class YAN_OT_weapon_type(bpy.types.Operator):
    bl_idname = 'yanengine.weapon_type'
    bl_label = 'Toggle Weapon Type'
    bl_options = {'UNDO'}
    identifier: StringProperty()

    def execute(self, context):
        selected = context.object.yan_level.weapon_types
        if not self.identifier:
            selected.clear()
        else:
            index = selected.find(self.identifier)
            if index >= 0:
                selected.remove(index)
            else:
                selected.add().name = self.identifier
        return {'FINISHED'}


class YAN_OT_refresh_definitions(bpy.types.Operator):
    bl_idname = 'yanengine.refresh_definitions'
    bl_label = '定義を再読み込み'

    def execute(self, context):
        _definition_cache.clear()
        if context.area:
            context.area.tag_redraw()
        return {'FINISHED'}


def draw_weapon_filter(layout, cfg, weapons):
    box = layout.box()
    box.label(text='Weapon Filter')
    for prop in ('weapon_slot', 'min_rarity', 'max_rarity'):
        box.prop(cfg, prop)
    box.label(text='Type (multiple; none selected = All)')
    selected = {entry.name for entry in cfg.weapon_types}
    box.operator('yanengine.weapon_type', text='All', icon='CHECKBOX_HLT' if not selected else 'CHECKBOX_DEHLT').identifier = ''
    types = {entry['type'] for entry in weapons.values()}
    for name in sorted(types | selected):
        row = box.row()
        row.alert = name not in types
        row.operator('yanengine.weapon_type', text=name if name in types else f'Missing / Unknown: {name}',
                     icon='CHECKBOX_HLT' if name in selected else 'CHECKBOX_DEHLT').identifier = name
    box.label(text='Source: Manual Pool' if cfg.pool else 'Source: All Weapons (definition weights)')
    try:
        matches = matched_weapons(cfg, weapons)
        box.label(text=f'Matched Weapons: {len({p["id"] for p in matches})}')
        box.operator('yanengine.catalog_open', text='View Matched in Catalog').matched_only = True
        for key in dict.fromkeys(p['id'] for p in matches):
            box.label(text=definition_label(weapons[key], True))
        if not matches or sum(p['weight'] for p in matches) <= 0:
            row = box.row()
            row.alert = True
            row.label(text='No positive-weight weapons match Filter', icon='ERROR')
    except (ValueError, KeyError, TypeError) as error:
        row = box.row()
        row.alert = True
        row.label(text=str(error), icon='ERROR')


def _remove_generated_colliders(source):
    """Remove only colliders previously generated for source by this add-on."""
    owner = source.get('yan_auto_collider_owner') or source.name
    for obj in list(bpy.data.objects):
        if obj.get('yan_auto_collider_generated') and obj.get('yan_auto_collider_owner') == owner:
            bpy.data.objects.remove(obj, do_unlink=True)


def _compound_group(faces, thickness):
    if not faces:
        return None
    low = tuple(min(p[a] for face in faces for p in face) for a in range(3))
    high = tuple(max(p[a] for face in faces for p in face) for a in range(3))
    volume = math.prod(max(high[a] - low[a], thickness) for a in range(3))
    return faces, low, high, volume


def _compound_candidates(group, epsilon):
    """Bounded candidates on ALL axes: centroid gaps and repeated face boundaries.

    Boundaries matter: the best cut through a lintel/pillar joint need not be at
    the midpoint of a centroid gap. Keep both, rather than picking a longest axis.
    """
    faces, low, high, _ = group
    result = []
    for axis in range(3):
        span = high[axis] - low[axis]
        if span <= epsilon:
            continue
        centers = sorted(sum(p[axis] for p in f) / len(f) for f in faces)
        gaps = sorted(((b-a, (a+b)*.5) for a, b in zip(centers, centers[1:])
                       if b-a > epsilon), reverse=True)
        boundaries = {}
        for face in faces:
            for value in (min(p[axis] for p in face), max(p[axis] for p in face)):
                if low[axis]+epsilon < value < high[axis]-epsilon:
                    boundaries[value] = boundaries.get(value, 0)+1
        # Cap search cost on dense terrain, while retaining large empty intervals.
        values = [(position, gap/span) for gap, position in gaps[:6]]
        # Small door jambs may occur less often than triangulated wall edges.
        # Keep every boundary on ordinary architectural meshes; cap dense meshes.
        values += [(value, 0.0) for value in sorted(boundaries, key=lambda v: (-boundaries[v], v))[:24]]
        used = []
        for value, gap in values:
            if low[axis]+epsilon < value < high[axis]-epsilon and not any(abs(value-v) <= epsilon for v in used):
                used.append(value)
                result.append((axis, value, gap))
    return result


def _compound_cut(group, axis, plane, epsilon, thickness):
    """Clip face polygons at a candidate plane; never change the author's mesh.

    Centroid-only assignment keeps long crossing triangles intact and can fill a
    doorway even after splitting. Clipping retains their full surface coverage.
    Coplanar cut faces are assigned conservatively without creating slabs across holes.
    """
    sides = [[], []]
    coplanar = []
    for face in group[0]:
        distances = [p[axis]-plane for p in face]
        if all(abs(d) <= epsilon for d in distances):
            coplanar.append(face)
            continue
        if max(distances) <= epsilon:
            sides[0].append(face)
            continue
        if min(distances) >= -epsilon:
            sides[1].append(face)
            continue
        for side, sign in enumerate((1, -1)):
            clipped = []
            previous = face[-1]
            prev_d = previous[axis]-plane
            for point, distance in zip(face, distances):
                inside, prev_inside = sign*distance <= 0, sign*prev_d <= 0
                if inside != prev_inside:
                    t = prev_d/(prev_d-distance)
                    intersection = tuple(previous[a]+t*(point[a]-previous[a]) for a in range(3))
                    clipped.append(intersection)
                if inside:
                    clipped.append(point)
                previous, prev_d = point, distance
            if len(clipped) >= 3:
                sides[side].append(tuple(clipped))
    left = _compound_group(sides[0], thickness)
    right = _compound_group(sides[1], thickness)
    if not left or not right:
        return None
    children = [left, right]
    for face in coplanar:
        # A cut-plane face is harmless only if another child's Box covers it.
        # Keep isolated shelves/floors instead of deleting their collision surface.
        covered = [i for i, child in enumerate(children)
                   if all(child[1][a]-epsilon <= p[a] <= child[2][a]+epsilon
                          for p in face for a in range(3))]
        if covered:
            # Retain the surface for later recursive cuts. Dropping it because
            # today's box covers it can create false holes in tomorrow's boxes.
            side = covered[0]
            child = children[side]
            children[side] = (child[0]+[face], child[1], child[2], child[3])
            continue
        enlarged = [_compound_group(child[0]+[face], thickness) for child in children]
        side = min(range(2), key=lambda i: enlarged[i][3]-children[i][3])
        children[side] = enlarged[side]
    return tuple(children)


def _approximate_compound_bounds(mesh, maximum):
    mesh.calc_loop_triangles()
    faces = [tuple(tuple(mesh.vertices[i].co) for i in triangle.vertices) for triangle in mesh.loop_triangles]
    if not faces:
        raise ValueError('Mesh has no surface triangles')
    if any(not math.isfinite(c) for f in faces for p in f for c in p):
        raise ValueError('Mesh contains non-finite coordinates')
    span = max(max(p[a] for f in faces for p in f)-min(p[a] for f in faces for p in f) for a in range(3))
    if span <= 1e-6:
        raise ValueError('Mesh bounds are degenerate')
    epsilon = max(span*1e-7, 1e-8)
    thickness = max(span*1e-6, 1e-4)
    groups = [_compound_group(faces, thickness)]

    # For a consistently oriented closed mesh, boxes must also preserve solid
    # volume, not merely its visible surfaces after clipping.
    edges = {}
    signed_volume = 0.0
    origin = faces[0][0]
    for triangle, face in zip(mesh.loop_triangles, faces):
        ids = tuple(triangle.vertices)
        for a, b in zip(ids, ids[1:]+ids[:1]):
            key = (min(a,b), max(a,b))
            count, balance = edges.get(key, (0,0))
            edges[key] = count+1, balance+(1 if a < b else -1)
        a,b,c = [tuple(p[i]-origin[i] for i in range(3)) for p in face]
        signed_volume += (a[0]*(b[1]*c[2]-b[2]*c[1])+
                          a[1]*(b[2]*c[0]-b[0]*c[2])+
                          a[2]*(b[0]*c[1]-b[1]*c[0]))/6.0
    solid_volume = abs(signed_volume) if all(v == (2,0) for v in edges.values()) else 0.0

    # Fix structural planes before clipping. Intersections with triangulation
    # diagonals otherwise invent many new boundaries and crowd doorway planes
    # out of the bounded lookahead search.
    structural = []
    for axis in range(3):
        counts = {}
        for face in faces:
            for point in face:
                value = point[axis]
                if groups[0][1][axis]+epsilon < value < groups[0][2][axis]-epsilon:
                    counts[value] = counts.get(value, 0)+1
        structural.extend((axis, value, 2.0) for value in
                          sorted(counts, key=lambda v: (-counts[v], v))[:24])

    def splits(group):
        options = []
        candidates = list(structural)
        # Centroid gaps remain useful for disconnected/non-axis-aligned meshes.
        candidates.extend(candidate for candidate in _compound_candidates(group, epsilon)
                          if candidate[2] > 0 and not any(
                              candidate[0] == axis and abs(candidate[1]-plane) <= epsilon
                              for axis, plane, _ in structural))
        for axis, plane, gap in candidates:
            if not group[1][axis]+epsilon < plane < group[2][axis]-epsilon:
                continue
            children = _compound_cut(group, axis, plane, epsilon, thickness)
            if children:
                options.append((sum(child[3] for child in children), gap, children))
        return options

    def plans(group, budget):
        # A recessed door needs cuts at both jambs, its top AND its back.
        # Intermediate cuts may save no volume at all. Keep these candidates
        # temporarily, but commit only a complete plan with positive savings.
        # Bound the search rather than recursively exploring every partition.
        frontier = [(None, (group,))]
        cache = {}
        for added in range(1, min(budget, 4)+1):
            candidates = {}
            for origin, leaves in frontier:
                for side, leaf in enumerate(leaves):
                    key = id(leaf)
                    if key not in cache:
                        # Hold a reference too, preventing Python id reuse.
                        cache[key] = (leaf, splits(leaf))
                    for _, gap, children in cache[key][1]:
                        replacement = leaves[:side]+children+leaves[side+1:]
                        volume = sum(child[3] for child in replacement)
                        # Keep separate search lanes for each first cut. Otherwise
                        # zero-saving doorway cuts lose to unrelated large gaps.
                        lane = origin if origin is not None else tuple(
                            (child[1], child[2]) for child in children)
                        # Equal bounds do not imply equal remaining surfaces.
                        signature = (lane, tuple(sorted(
                            tuple(sorted(child[0])) for child in replacement)))
                        previous = candidates.get(signature)
                        if previous is None or gap > previous[1]:
                            candidates[signature] = (volume, gap, replacement, lane)
            # Compare near-equal volumes as ties; floating-point noise
            # must not select a random path before a real void is exposed.
            tolerance = max(group[3]*1e-7, thickness**3)
            ordered = sorted(candidates.values(),
                             key=lambda item: (round(item[0]/tolerance), -item[1]))
            if not ordered:
                break
            for volume, gap, leaves, _ in ordered:
                yield volume, gap, leaves
            # Retain multiple paths PER first cut, not 24 paths for the entire
            # mesh. Small entrances must compete within their own search lane.
            frontier = []
            retained = {}
            for _, _, leaves, lane in ordered:
                if retained.get(lane, 0) < 8:
                    frontier.append((lane, leaves))
                    retained[lane] = retained.get(lane, 0)+1

    while len(groups) < maximum:
        if solid_volume > 0 and sum(g[3] for g in groups) <= solid_volume*(1+1e-7):
            break
        best = None
        budget = maximum-len(groups)
        for index, group in enumerate(groups):
            # Reject numerical noise only. A percentage-per-box cost suppresses
            # real narrow entrances merely because the surrounding building is large.
            tolerance = max(group[3]*1e-7, thickness**3)
            for volume, gap, leaves in plans(group, budget):
                saving = group[3]-volume
                if sum(g[3] for g in groups)-saving < solid_volume-max(solid_volume*1e-6, tolerance):
                    continue
                if saving <= tolerance:
                    continue
                added = len(leaves)-1
                # Quantize within numerical tolerance so equivalent partitions
                # favor fewer boxes, rather than floating-point roundoff.
                rank = (round(saving/tolerance), -added, gap)
                if best is None or saving > best[3]+max(tolerance, best[4]) or (
                        abs(saving-best[3]) <= max(tolerance, best[4])
                        and rank[1:] > best[0][1:]):
                    best = (rank, index, leaves, saving, tolerance)
        if best is None:
            break
        _, index, leaves, _, _ = best
        groups[index:index+1] = leaves
    return [(group[1], group[2]) for group in groups], thickness


def _pad_compound_bounds(boxes, padding, thickness):
    """Apply final margins, capping facing margins to preserve >= half a gap."""
    result = []
    # A lintel may sit between full-height side boxes: none is strictly below
    # it, so pairwise facing tests alone cannot protect the opening underneath.
    # Limit margins by the smallest structural interval on each axis as well.
    margins = []
    for axis in range(3):
        coordinates = sorted(set(bound[axis] for box in boxes for bound in box))
        intervals = [b-a for a,b in zip(coordinates, coordinates[1:]) if b-a > thickness]
        margins.append(min(padding, min(intervals)*.25) if intervals and len(boxes) > 1 else padding)
    for index, (low, high) in enumerate(boxes):
        before, after = list(margins), list(margins)
        for other_index, (other_low, other_high) in enumerate(boxes):
            if index == other_index:
                continue
            for axis in range(3):
                if other_low[axis] >= high[axis]:
                    after[axis] = min(after[axis], (other_low[axis]-high[axis])*.25)
                if other_high[axis] <= low[axis]:
                    before[axis] = min(before[axis], (low[axis]-other_high[axis])*.25)
        bmin = Vector(tuple(low[a]-before[a] for a in range(3)))
        bmax = Vector(tuple(high[a]+after[a] for a in range(3)))
        # Open/planar meshes still need a valid, nonzero Box for StageWorld.
        for axis in range(3):
            if bmax[axis]-bmin[axis] < thickness:
                center = (bmax[axis]+bmin[axis])*.5
                bmin[axis], bmax[axis] = center-thickness*.5, center+thickness*.5
        result.append((bmin, bmax))
    return result


def _generate_compound_boxes(source, count, padding):
    if source.type != 'MESH':
        raise ValueError('Auto Compound requires a Mesh object')
    if not isinstance(count, int) or not 1 <= count <= 32 or not math.isfinite(padding) or padding < 0:
        raise ValueError('Invalid Max Box Count or Padding')
    # Match Static Mesh export, including Boolean/other evaluated modifiers.
    if source.mode == 'EDIT':
        source.update_from_editmode()
    bpy.context.view_layer.update()
    evaluated = source.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    try:
        boxes, thickness = _approximate_compound_bounds(mesh, count)
    finally:
        evaluated.to_mesh_clear()
    boxes = _pad_compound_bounds(boxes, padding, thickness)
    # Validate first: a failed regeneration must not remove the previous colliders.
    _remove_generated_colliders(source)
    source['yan_auto_collider_owner'] = source.name
    created = []
    for index, (bmin, bmax) in enumerate(boxes):
        center, half = (bmin+bmax)*.5, (bmax-bmin)*.5
        empty = bpy.data.objects.new(f'COL_{source.name}_{index:02d}', None)
        empty.empty_display_type = 'CUBE'
        empty.empty_display_size = 1.0
        empty.display_type = 'WIRE'
        empty.matrix_world = source.matrix_world @ Matrix.Translation(center) @ Matrix.Diagonal((half.x, half.y, half.z, 1.0))
        context_collection = source.users_collection[0] if source.users_collection else bpy.context.scene.collection
        context_collection.objects.link(empty)
        empty.yan_level.role = 'COLLIDER'
        empty.yan_level.identifier = empty.name
        empty['yan_auto_collider_generated'] = True
        empty['yan_auto_collider_owner'] = source.name
        created.append(empty)
    return created


class YAN_OT_generate_auto_colliders(bpy.types.Operator):
    bl_idname = 'yanengine.generate_auto_colliders'
    bl_label = 'Generate Approximate Colliders'
    bl_description = 'Generate editable Box colliders that approximately cover the selected Static Mesh'
    bl_options = {'UNDO'}

    @classmethod
    def poll(cls, context):
        return context.object is not None and context.object.type == 'MESH' and context.object.yan_level.role == 'STATIC'

    def execute(self, context):
        source = context.object
        cfg = source.yan_level
        try:
            created = _generate_compound_boxes(source, cfg.auto_collider_count, cfg.auto_collider_padding)
            # The generated Collider objects now own collision. Avoid also exporting one
            # large Static Box for the source mesh.
            cfg.collision = 'NONE'
            for obj in context.selected_objects:
                obj.select_set(False)
            for obj in created:
                obj.select_set(True)
            context.view_layer.objects.active = created[0]
            self.report({'INFO'}, f'Generated {len(created)} Box colliders for {source.name}')
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_OT_clear_auto_colliders(bpy.types.Operator):
    bl_idname = 'yanengine.clear_auto_colliders'
    bl_label = 'Clear Generated Colliders'
    bl_options = {'UNDO'}

    @classmethod
    def poll(cls, context):
        return context.object is not None and context.object.type == 'MESH' and context.object.yan_level.role == 'STATIC'

    def execute(self, context):
        source = context.object
        _remove_generated_colliders(source)
        self.report({'INFO'}, f'Cleared generated colliders for {source.name}')
        return {'FINISHED'}


class YAN_OT_validate(bpy.types.Operator):
    bl_idname = 'yanengine.validate'
    bl_label = 'レベルを検証'
    def execute(self, context):
        try:
            context.view_layer.update()
            build_level(context.scene, context.evaluated_depsgraph_get())
            output_path(context.scene)
            self.report({'INFO'}, 'レベルの検証に成功しました')
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_OT_export(bpy.types.Operator):
    bl_idname = 'yanengine.export'
    bl_label = 'レベルを書き出す'
    def execute(self, context):
        try:
            target = export_level(context)
            self.report({'INFO'}, f'書き出しました: {target}')
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_PT_level(bpy.types.Panel):
    bl_label = 'YanEngine Level'
    bl_idname = 'YAN_PT_level'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'YanEngine Level'
    def draw(self, context):
        layout = self.layout
        cfg = context.scene.yan_level
        for key in ('stage_id', 'project_root', 'output_directory', 'fixed_seed', 'player_gravity'):
            layout.prop(cfg, key)
        layout.operator('yanengine.refresh_definitions', icon='FILE_REFRESH')
        if cfg.fixed_seed:
            layout.prop(cfg, 'seed')
        if cfg.stage_id == 'title':
            layout.prop(cfg, 'title_weapon')
            layout.prop(cfg, 'title_start_delay')
        layout.separator()
        obj = context.object
        if obj:
            settings = obj.yan_level
            layout.prop(settings, 'role')
            if settings.role != 'IGNORE':
                layout.prop(settings, 'identifier', text='出現ID' if settings.role == 'ENEMY' else 'トリガーID' if settings.role == 'TRIGGER' else 'ID')
            if settings.role == 'START_LETTER':
                layout.label(text='One combined Mesh; all faces are Head, 1 HP')
            if settings.role == 'SHUTTER':
                for field in ('shutter_button','shutter_duration','shutter_distance','shutter_reach',
                              'shutter_progress_events','shutter_switch_sound','shutter_start_sound',
                              'shutter_motor_sound','shutter_complete_sound'):
                    layout.prop(settings,field)
            if settings.role == 'COLLIDER' or (settings.role == 'STATIC' and settings.collision == 'BOX'):
                layout.prop(settings, 'walkable')
                layout.prop(settings, 'player_only')
            if settings.role == 'STATIC':
                layout.prop(settings, 'collision')
                if settings.collision == 'CUSTOM':
                    layout.prop(settings, 'custom_collider')
                auto = layout.box()
                auto.label(text='Approximate Collision (Box Compound)')
                auto.prop(settings, 'auto_collider_count')
                auto.prop(settings, 'auto_collider_padding')
                row = auto.row(align=True)
                row.operator('yanengine.generate_auto_colliders', icon='MOD_BUILD')
                row.operator('yanengine.clear_auto_colliders', text='Clear', icon='TRASH')
                auto.label(text='Generated boxes are editable Collider objects.')
            if settings.role == 'ENEMY':
                layout.prop(settings, 'spawn_activation')
                if settings.spawn_activation == 'LEGACY':
                    layout.label(text='旧設定を維持（共有総数の自動コピーなし）', icon='INFO')
                    layout.prop(settings, 'group')
                else:
                    if settings.spawn_activation == 'OnTrigger':
                        layout.prop(settings, 'group')
                    if settings.spawn_activation == 'Manual':
                        layout.label(text='出現IDを指定して外部APIから起動します')
                    choices = {}
                    try:
                        choices = definitions(context.scene, 'enemies')
                    except ValueError as error:
                        layout.label(text=str(error), icon='ERROR')
                    layout.prop(settings, 'spawn_composition_mode')
                    if settings.spawn_composition_mode == 'COUNTS':
                        box = layout.box()
                        box.label(text='出現する敵と体数（リスト順）')
                        for index, entry in enumerate(settings.enemy_composition):
                            row = box.row(align=True)
                            row.context_pointer_set('yan_pool_entry', entry)
                            definition = choices.get(entry.identifier.strip())
                            picker = row.row(align=True)
                            picker.alert = definition is None
                            picker.menu('YAN_MT_pool_choices', text=definition_label(definition) if definition else f'不明: {entry.identifier}')
                            row.prop(entry, 'count')
                            for direction, icon in ((-1, 'TRIA_UP'), (1, 'TRIA_DOWN')):
                                button = row.row(align=True)
                                button.enabled = 0 <= index + direction < len(settings.enemy_composition)
                                op = button.operator('yanengine.enemy_composition', text='', icon=icon)
                                op.index, op.move = index, direction
                            box_op = row.operator('yanengine.enemy_composition', text='', icon='REMOVE')
                            box_op.index = index
                        box.operator('yanengine.enemy_composition', text='敵の種類を追加', icon='ADD')
                        box.label(text=f'総出現数: {spawn_total(settings)} 体')
                    else:
                        definition = choices.get(settings.enemy_definition.strip())
                        picker = layout.row()
                        picker.alert = definition is None
                        picker.menu('YAN_MT_pool_choices', text='敵定義: ' + (definition_label(definition) if definition else f'不明: {settings.enemy_definition}'))
                        layout.prop(settings, 'spawn_count')
                    for field in ('spawn_mode', 'initial_delay', 'max_alive'):
                        layout.prop(settings, field)
                    interval = layout.row()
                    interval.enabled = settings.spawn_mode == 'Sequential'
                    interval.prop(settings, 'spawn_interval')
                    if settings.spawn_mode == 'Simultaneous':
                        layout.label(text='出現間隔は使いません。上限超過分は空きを待ちます')
                    layout.label(text=f'実効出現ID: {object_id(obj)}')
            if settings.role == 'TRIGGER':
                layout.prop(settings, 'trigger_mode')
                layout.prop(settings, 'group')
                layout.prop(settings, 'trigger_condition')
                if settings.trigger_mode == 'GROUP':
                    matches = [o for o in context.scene.objects if o.yan_level.role == 'ENEMY'
                               and o.yan_level.spawn_activation == 'OnTrigger'
                               and o.yan_level.group.strip() == settings.group.strip()]
                    layout.label(text=f'起動対象: {len(matches)} 地点')
                    for point in sorted(matches, key=lambda o: o.name):
                        layout.label(text=f'{object_id(point)}: 総数 {spawn_total(point.yan_level)} / 上限 {point.yan_level.max_alive}')
                else:
                    layout.label(text='旧形式: 総数・上限を全地点で共有します', icon='INFO')
            if settings.role == 'WEAPON' or (settings.role == 'ENEMY' and settings.spawn_activation == 'LEGACY'):
                layout.label(text='敵の抽選候補（旧形式）' if settings.role == 'ENEMY' else '武器の抽選候補（手動）')
                choices = {}
                try:
                    choices = definitions(context.scene, 'weapons' if settings.role == 'WEAPON' else 'enemies')
                except ValueError as error:
                    layout.label(text=str(error), icon='ERROR')
                for index, entry in enumerate(settings.pool):
                    row = layout.row(align=True)
                    row.context_pointer_set('yan_pool_entry', entry)
                    definition = choices.get(entry.identifier.strip())
                    label = definition_label(definition, settings.role == 'WEAPON') if definition else f'Missing / Unknown: {entry.identifier}'
                    picker = row.row(align=True)
                    picker.alert = definition is None
                    picker.menu('YAN_MT_pool_choices', text=label)
                    row.prop(entry, 'weight')
                    row.operator('yanengine.pool', text='', icon='REMOVE').index = index
                layout.operator('yanengine.pool', text='候補を追加', icon='ADD').index = -1
                if settings.role == 'WEAPON':
                    layout.operator('yanengine.catalog_open', text='Open Weapon Catalog').matched_only = False
                    draw_weapon_filter(layout, settings, choices)
            if settings.role == 'TRIGGER':
                fields = ('initial_delay', 'one_shot') if settings.trigger_mode == 'GROUP' else ('spawn_count', 'spawn_interval', 'initial_delay', 'max_alive', 'selection', 'one_shot')
                for key in fields:
                    if key == 'initial_delay':
                        layout.prop(settings, key, text='起動遅延（秒）')
                        continue
                    layout.prop(settings, key)
            if settings.role == 'SPAWN_GROUP':
                box = layout.box()
                box.label(text='時刻指定の出現グループ')
                box.prop(settings, 'spawn_mode')
                box.prop(settings, 'start_time')
                interval = box.row()
                interval.enabled = settings.spawn_mode == 'Sequential'
                interval.prop(settings, 'spawn_interval')
                box.label(text='出現する敵（リスト順）')
                choices = {}
                try:
                    choices = definitions(context.scene, 'enemies')
                except ValueError as error:
                    box.label(text=str(error), icon='ERROR')
                for index, entry in enumerate(settings.members):
                    row = box.row(align=True)
                    row.context_pointer_set('yan_pool_entry', entry)
                    definition = choices.get(entry.identifier.strip())
                    picker = row.row(align=True)
                    picker.alert = definition is None
                    picker.menu('YAN_MT_pool_choices', text=definition_label(definition) if definition else f'Missing / Unknown: {entry.identifier}')
                    row.prop(entry, 'spawn_point', text='')
                    for direction, icon in ((-1, 'TRIA_UP'), (1, 'TRIA_DOWN')):
                        button = row.row(align=True)
                        button.enabled = 0 <= index + direction < len(settings.members)
                        op = button.operator('yanengine.group_member', text='', icon=icon)
                        op.index, op.move = index, direction
                    row.operator('yanengine.group_member', text='', icon='REMOVE').index = index
                box.operator('yanengine.group_member', text='敵を追加', icon='ADD')
            if settings.role in {'COLLIDER', 'TRIGGER', 'GOAL'}:
                layout.label(text='範囲はビューポートで拡縮してください')
        layout.separator()
        layout.operator('yanengine.validate', icon='CHECKMARK')
        layout.operator('yanengine.export', icon='EXPORT')


CLASSES = (YAN_PoolEntry, YAN_WeaponType, YAN_SpawnGroupEnemy, YAN_EnemySpawnCount, YAN_ObjectSettings, YAN_SceneSettings, YAN_OT_pool,
           YAN_CatalogEntry, YAN_CatalogState, YAN_OT_catalog_open, YAN_OT_catalog_action,
           YAN_OT_catalog_type, YAN_MT_catalog_types, YAN_OT_catalog_apply, YAN_UL_catalog, YAN_PT_weapon_catalog,
           YAN_OT_choose_pool, YAN_MT_pool_choices, YAN_OT_enemy_composition, YAN_OT_group_member, YAN_OT_weapon_type, YAN_OT_refresh_definitions,
           YAN_OT_generate_auto_colliders, YAN_OT_clear_auto_colliders,
           YAN_OT_validate, YAN_OT_export, YAN_PT_level)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Object.yan_level = PointerProperty(type=YAN_ObjectSettings)
    bpy.types.Scene.yan_level = PointerProperty(type=YAN_SceneSettings)
    bpy.types.WindowManager.yan_catalog = PointerProperty(type=YAN_CatalogState, options={'SKIP_SAVE'})
    bpy.app.handlers.load_post.append(preserve_legacy_spawns)
    bpy.app.timers.register(initialize_legacy_spawns, first_interval=0.0)


def unregister():
    _definition_cache.clear()
    if bpy.app.timers.is_registered(initialize_legacy_spawns):
        bpy.app.timers.unregister(initialize_legacy_spawns)
    if preserve_legacy_spawns in bpy.app.handlers.load_post:
        bpy.app.handlers.load_post.remove(preserve_legacy_spawns)
    del bpy.types.WindowManager.yan_catalog
    del bpy.types.Scene.yan_level
    del bpy.types.Object.yan_level
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == '__main__':
    register()
