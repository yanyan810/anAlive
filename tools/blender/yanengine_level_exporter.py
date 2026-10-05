"""YanEngine level authoring. Blender 4.4+; install this file as a legacy add-on."""
bl_info = {"name": "YanEngine Level", "author": "YanEngine", "version": (1, 6, 0),
           "blender": (4, 4, 0), "location": "View3D > Sidebar > YanEngine Level", "category": "Import-Export"}
import bpy
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
    settings = scene.yan_level
    stage_id = settings.stage_id.strip()
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]*', stage_id):
        raise ValueError("Stage ID must start with a letter; use letters, digits, '-' or '_'")
    objects = sorted((o for o in scene.objects if o.yan_level.role != 'IGNORE'), key=lambda o: o.name)
    ids = set()
    for obj in objects:
        key = object_id(obj)
        if key in ids:
            raise ValueError(f"Duplicate ID: {key}")
        ids.add(key)
        finite([v for row in obj.matrix_world for v in row], obj.name)
    players = [o for o in objects if o.yan_level.role == 'PLAYER']
    if len(players) != 1:
        raise ValueError(f"Exactly one Player Spawn required (found {len(players)})")
    enemies = definitions(scene, 'enemies') if any(o.yan_level.role == 'ENEMY' for o in objects) else {}
    weapons = definitions(scene, 'weapons') if any(o.yan_level.role == 'WEAPON' for o in objects) else {}
    data = {"version": 1, "stage": {"id": stage_id, "model": f"levels/{stage_id}/{stage_id}.gltf"},
            "playerSpawn": {}, "colliders": [], "enemyRandom": {"useFixedSeed": settings.fixed_seed, "seed": settings.seed},
            "spawnPoints": [], "spawnTriggers": [], "weaponRandom": {"useFixedSeed": settings.fixed_seed, "seed": settings.seed},
            "weaponSpawnPoints": [], "goalTriggers": []}
    groups = {}
    geometry = []
    for obj in objects:
        cfg = obj.yan_level
        key = object_id(obj)
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
            data['colliders'].append(dict(id=key, **box))
        elif cfg.role in {'PLAYER', 'ENEMY', 'WEAPON'}:
            trs, _ = engine_transform(obj.matrix_world)
            pose = {"position": trs['position'], "rotation": trs['rotation']}
            if cfg.role == 'PLAYER':
                data['playerSpawn'] = pose
            elif cfg.role == 'ENEMY':
                if not cfg.group.strip():
                    raise ValueError(f"{key}: Spawn Group required")
                groups.setdefault(cfg.group.strip(), []).append(key)
                data['spawnPoints'].append(dict(id=key, **pose, enemyPool=pool_data(cfg.pool, key, enemies)))
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
        if cfg.role != 'TRIGGER':
            continue
        points = groups.get(cfg.group.strip(), [])
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
        data['title'] = {'weaponId': weapon_id, 'startDelay': settings.title_start_delay,
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
    backup = target.with_name(f'.{target.name}-backup-{uuid.uuid4().hex}')
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
        for source in geometry + letters:
            mesh = bpy.data.meshes.new_from_object(source.evaluated_get(depsgraph), depsgraph=depsgraph)
            finite([v for vertex in mesh.vertices for v in vertex.co], source.name)
            mesh.transform(source.matrix_world)
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
        # GAME START is one Head asset, rendered and shattered by the ordinary Enemy.
        for source in letters:
            export_start_head(exported[source.name], scratch/'game_start.enemy.json')
        if letters:
            # Remove only the previous exporter's per-letter generated files.
            for old_file in scratch.iterdir():
                if re.fullmatch(r'start_\d{2,3}\.(gltf|bin)', old_file.name):
                    old_file.unlink()
        (scratch / f"{data['stage']['id']}.json").write_text(json.dumps(data, indent=2, allow_nan=False)+'\n', encoding='utf-8')
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


class YAN_ObjectSettings(bpy.types.PropertyGroup):
    role: EnumProperty(name="YanEngine Object Type", default='IGNORE', update=role_changed, items=[
        ('STATIC', 'Static Mesh', ''), ('COLLIDER', 'Collider', ''), ('PLAYER', 'Player Spawn', ''),
        ('ENEMY', 'Enemy Spawn', ''), ('TRIGGER', 'Spawn Trigger', ''), ('WEAPON', 'Weapon Spawn', ''),
        ('GOAL', 'Goal', ''), ('IGNORE', 'Ignore', ''),
        ('START_LETTER', 'Game Start (Head)', 'Title only; join the whole text into one Mesh')])
    identifier: StringProperty(name="ID", description="Blank uses Object name")
    group: StringProperty(name="Spawn Group", default="A")
    collision: EnumProperty(name="Collision", items=[('NONE', 'None', ''), ('BOX', 'Box', ''), ('CUSTOM', 'Custom', '')])
    custom_collider: PointerProperty(name="Collider Object", type=bpy.types.Object)
    pool: CollectionProperty(type=YAN_PoolEntry)
    weapon_slot: EnumProperty(name='Slot', items=[('ALL', 'All', ''), ('Main', 'Main', ''), ('Sub', 'Sub', '')], default='ALL')
    min_rarity: EnumProperty(name='Min Rarity', items=[(str(i), f'★{i}', '') for i in range(1, 6)], default='1')
    max_rarity: EnumProperty(name='Max Rarity', items=[(str(i), f'★{i}', '') for i in range(1, 6)], default='5')
    weapon_types: CollectionProperty(type=YAN_WeaponType)
    spawn_count: IntProperty(name="Spawn Count", default=5, min=1, max=10000)
    spawn_interval: FloatProperty(name="Spawn Interval", default=.5, min=0)
    initial_delay: FloatProperty(name="Initial Delay", default=0, min=0)
    max_alive: IntProperty(name="Max Alive", default=5, min=1, max=10000)
    selection: EnumProperty(name="Selection", items=[('Random', 'Random', ''), ('RoundRobin', 'RoundRobin', '')])
    one_shot: BoolProperty(name="One Shot", default=True)
    auto_collider_count: IntProperty(name="Max Box Count", description="Upper limit; simple shapes use fewer boxes", default=4, min=1, max=32)
    auto_collider_padding: FloatProperty(name="Padding", description="Extra local-space margin added to generated boxes", default=0.02, min=0.0, max=10.0)


class YAN_SceneSettings(bpy.types.PropertyGroup):
    stage_id: StringProperty(name="Stage ID", default="stage01")
    project_root: StringProperty(name="Project Root", description="Engine project folder; // paths are relative to the blend file")
    output_directory: StringProperty(name="Output Directory", default="resources/levels/stage01", subtype='DIR_PATH')
    fixed_seed: BoolProperty(name="Fixed Seed", default=False)
    seed: IntProperty(name="Seed", default=12345, min=0)
    title_weapon: StringProperty(name='Title Weapon ID', default='pistol')
    title_start_delay: FloatProperty(name='Start Explosion Duration', default=.75, min=.5, max=1.0)


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
    bl_label = 'Choose Pool ID'
    bl_options = {'UNDO'}
    index: IntProperty()
    identifier: StringProperty()

    def execute(self, context):
        cfg = context.object.yan_level
        if 0 <= self.index < len(cfg.pool):
            cfg.pool[self.index].identifier = self.identifier
            return {'FINISHED'}
        return {'CANCELLED'}


class YAN_MT_pool_choices(bpy.types.Menu):
    bl_label = 'Choose Definition'

    def draw(self, context):
        cfg = context.object.yan_level
        entry = context.yan_pool_entry
        index = next(i for i, row in enumerate(cfg.pool) if row.as_pointer() == entry.as_pointer())
        try:
            choices = definitions(context.scene, 'weapons' if cfg.role == 'WEAPON' else 'enemies')
            if entry.identifier.strip() not in choices:
                self.layout.label(text=f'Missing / Unknown: {entry.identifier}', icon='ERROR')
            for identifier, definition in choices.items():
                op = self.layout.operator('yanengine.choose_pool', text=definition_label(definition, cfg.role == 'WEAPON'))
                op.index, op.identifier = index, identifier
        except ValueError as error:
            self.layout.label(text=str(error), icon='ERROR')


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
    bl_label = 'Refresh Definitions'

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
    bl_label = 'Validate Level'
    def execute(self, context):
        try:
            context.view_layer.update()
            build_level(context.scene, context.evaluated_depsgraph_get())
            output_path(context.scene)
            self.report({'INFO'}, "Level valid")
            return {'FINISHED'}
        except Exception as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_OT_export(bpy.types.Operator):
    bl_idname = 'yanengine.export'
    bl_label = 'Export Level'
    def execute(self, context):
        try:
            target = export_level(context)
            self.report({'INFO'}, f"Exported {target}")
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
        for key in ('stage_id', 'project_root', 'output_directory', 'fixed_seed'):
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
                layout.prop(settings, 'identifier')
            if settings.role == 'START_LETTER':
                layout.label(text='One combined Mesh; all faces are Head, 1 HP')
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
            if settings.role in {'ENEMY', 'TRIGGER'}:
                layout.prop(settings, 'group')
            if settings.role in {'ENEMY', 'WEAPON'}:
                layout.label(text='Enemy Pool' if settings.role == 'ENEMY' else 'Weapon Pool (Manual Pool)')
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
                layout.operator('yanengine.pool', text='Add Entry', icon='ADD').index = -1
                if settings.role == 'WEAPON':
                    layout.operator('yanengine.catalog_open', text='Open Weapon Catalog').matched_only = False
                    draw_weapon_filter(layout, settings, choices)
            if settings.role == 'TRIGGER':
                for key in ('spawn_count', 'spawn_interval', 'initial_delay', 'max_alive', 'selection', 'one_shot'):
                    layout.prop(settings, key)
            if settings.role in {'COLLIDER', 'TRIGGER', 'GOAL'}:
                layout.label(text='Resize the Cube / Cube Empty in the viewport')
        layout.separator()
        layout.operator('yanengine.validate', icon='CHECKMARK')
        layout.operator('yanengine.export', icon='EXPORT')


CLASSES = (YAN_PoolEntry, YAN_WeaponType, YAN_ObjectSettings, YAN_SceneSettings, YAN_OT_pool,
           YAN_CatalogEntry, YAN_CatalogState, YAN_OT_catalog_open, YAN_OT_catalog_action,
           YAN_OT_catalog_type, YAN_MT_catalog_types, YAN_OT_catalog_apply, YAN_UL_catalog, YAN_PT_weapon_catalog,
           YAN_OT_choose_pool, YAN_MT_pool_choices, YAN_OT_weapon_type, YAN_OT_refresh_definitions,
           YAN_OT_generate_auto_colliders, YAN_OT_clear_auto_colliders,
           YAN_OT_validate, YAN_OT_export, YAN_PT_level)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Object.yan_level = PointerProperty(type=YAN_ObjectSettings)
    bpy.types.Scene.yan_level = PointerProperty(type=YAN_SceneSettings)
    bpy.types.WindowManager.yan_catalog = PointerProperty(type=YAN_CatalogState, options={'SKIP_SAVE'})


def unregister():
    _definition_cache.clear()
    del bpy.types.WindowManager.yan_catalog
    del bpy.types.Scene.yan_level
    del bpy.types.Object.yan_level
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == '__main__':
    register()
