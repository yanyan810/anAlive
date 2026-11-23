"""Independent Enemy Asset authoring add-on. No dependency on the level exporter.
Face-domain integer IDs survive polygon reordering; IDs are never reused on Add.
Exports rest-pose snapshots or face references from an engine editing mesh.
"""
bl_info = {"name": "YanEngine Enemy Parts", "author": "YanEngine", "version": (2, 0, 0),
           "blender": (4, 4, 0), "location": "View3D > Sidebar > Enemy Parts", "category": "Object"}
import json
import math
import hashlib
import struct
from pathlib import Path
import bpy
import bmesh
from bpy.props import (StringProperty, IntProperty, FloatProperty, BoolProperty,
                       EnumProperty, CollectionProperty)
from bpy_extras.io_utils import ExportHelper, ImportHelper

ATTRIBUTE = "yan_enemy_part"
SOURCE_ATTRIBUTES = ["yan_enemy_" + name for name in ("instance", "mesh", "material", "triangle")]
MODEL_REFERENCE = "yan_enemy_model_reference"
MESH_SIGNATURE = "yan_enemy_mesh_signature"
CONTROLLER = "yan_enemy_controller"
ROLES = ["Generic", "Head", "Body", "Arm", "Leg", "Core", "Armor"]


def active_mesh(context):
    obj = context.active_object
    if not obj or obj.type != 'MESH':
        raise ValueError("Select a mesh object")
    return obj


def asset_collection(obj):
    return next((collection for collection in obj.users_collection if MODEL_REFERENCE in collection), None)


def controller(obj):
    collection = asset_collection(obj)
    if collection is None:
        return obj
    return next(member for member in collection.objects if member.get(CONTROLLER))


def selected_meshes(context, obj):
    candidates = context.objects_in_mode if obj.mode == 'EDIT' else context.selected_objects
    collection = asset_collection(obj)
    return sorted({obj, *(other for other in candidates
                         if other.type == 'MESH' and asset_collection(other) == collection)}, key=lambda item: item.name)


def edit_faces(obj, action, part_id=0):
    """One integer per face guarantees mutually exclusive assignment."""
    editing = obj.mode == 'EDIT'
    bm = bmesh.from_edit_mesh(obj.data) if editing else bmesh.new()
    if not editing:
        bm.from_mesh(obj.data)
    layer = bm.faces.layers.int.get(ATTRIBUTE) or bm.faces.layers.int.new(ATTRIBUTE)
    for face in bm.faces:
        if action == 'SELECT':
            face.select_set(face[layer] == part_id)
        elif action == 'REMOVE':
            if face[layer] == part_id:
                face[layer] = 0
        elif face.select:
            face[layer] = part_id if action == 'ASSIGN' else 0
    if action == 'SELECT':
        bm.select_mode = {'FACE'}
        if editing:
            bpy.context.tool_settings.mesh_select_mode = (False, False, True)
        bm.select_flush_mode()
    if editing:
        bmesh.update_edit_mesh(obj.data)
    else:
        bm.to_mesh(obj.data)
        bm.free()
    obj.data.update()


def add_part(mesh, name="Part", role="Generic"):
    mesh.yan_enemy_next_id += 1
    part = mesh.yan_enemy_parts.add()
    part.part_id = mesh.yan_enemy_next_id
    part.name = name
    part.role = role
    mesh.yan_enemy_part_index = len(mesh.yan_enemy_parts) - 1
    return part


def export_asset(obj, filepath, objects=None):
    # FACE attributes have no RNA data while edit BMesh owns them. Flush and
    # temporarily leave edit mode; always restore it, including validation failure.
    editing = obj.mode == 'EDIT'
    if editing:
        bpy.ops.object.mode_set(mode='OBJECT')
    try:
        collection = asset_collection(obj)
        if collection is not None:
            return export_reference(controller(obj), collection, filepath)
        return _export_asset(obj, filepath, objects or [obj])
    finally:
        if editing:
            bpy.ops.object.mode_set(mode='EDIT')


def part_settings(mesh):
    groups = []
    group_ids = set()
    for group in mesh.yan_enemy_groups:
        if not group.name or group.name in group_ids or not math.isfinite(group.max_hp) or group.max_hp <= 0:
            raise ValueError("HP group names must be unique, and max HP positive")
        group_ids.add(group.name)
        groups.append(dict(id=group.name, maxHp=group.max_hp, deathOnZero=group.death_on_zero))
    parts = {}
    names = set()
    for part in mesh.yan_enemy_parts:
        if not part.name or part.name == '__Unassigned' or part.name in names:
            raise ValueError("Part names must be unique; __Unassigned is reserved")
        if part.part_id <= 0 or part.part_id in parts:
            raise ValueError("Invalid/duplicate face assignment ID")
        names.add(part.name)
        local = part.hp_mode in {'Local', 'LocalAndShared'}
        shared = part.hp_mode in {'Shared', 'LocalAndShared'}
        if local and (not math.isfinite(part.local_hp) or part.local_hp <= 0):
            raise ValueError("Local HP must be positive")
        if not math.isfinite(part.shared_rate) or part.shared_rate < 0:
            raise ValueError("Shared Damage Rate must be nonnegative")
        if shared and part.shared_group not in group_ids:
            raise ValueError("Unknown HP group for " + part.name)
        parts[part.part_id] = dict(name=part.name, role=part.role,
            localHp=part.local_hp if local else None,
            sharedHpGroup=part.shared_group if shared else '', sharedDamageRate=part.shared_rate,
            breakable=part.breakable, deathOnZero=part.death_on_zero and local, faces=[])
        if not part.bullet_collision:
            parts[part.part_id]['bulletCollision'] = False
    return groups, parts


def assignments(mesh):
    attr = mesh.attributes.get(ATTRIBUTE)
    if attr and (attr.domain != 'FACE' or attr.data_type != 'INT'):
        raise ValueError("Enemy Part attribute must be FACE / INT")
    return [entry.value for entry in attr.data] if attr else [0] * len(mesh.polygons)


def snapshot_mesh(obj, parts, first_face):
    mesh = obj.data
    # Evaluating topology modifiers would detach polygon IDs from their authored faces.
    if any(m.type != 'ARMATURE' and (m.show_viewport or m.show_render) for m in obj.modifiers):
        raise ValueError("Apply topology/deformation modifiers before assigning parts; armature exports rest mesh only")
    if abs(obj.matrix_world.to_3x3().determinant()) < 1e-8:
        raise ValueError("Object transform is singular")
    owners = assignments(mesh)
    for polygon in mesh.polygons:
        part_id = owners[polygon.index]
        if part_id and part_id not in parts:
            raise ValueError("Face references a removed/missing Part; use Clear Assignment")
        if part_id == 0 and 0 not in parts:
            parts[0] = dict(name='__Unassigned', role='Generic', localHp=None,
                sharedHpGroup='', sharedDamageRate=0, breakable=False, deathOnZero=False, faces=[], triangles=[])
        parts[part_id]['faces'].append(first_face + polygon.index)
    mesh.calc_loop_triangles()
    matrix = obj.matrix_world
    normal_matrix = matrix.to_3x3().inverted().transposed()
    uv = mesh.uv_layers.active
    # Blender (X,Y,Z) -> engine (-X,Z,-Y), including winding parity.
    def engine(v):
        result = [-float(v.x), float(v.z), -float(v.y)]
        if not all(math.isfinite(x) for x in result):
            raise ValueError("Non-finite geometry")
        return result
    for triangle in mesh.loop_triangles:
        face = triangle.polygon_index
        part_id = owners[face]
        loops = list(triangle.loops)
        if matrix.to_3x3().determinant() > 0:
            loops.reverse()
        vertices = []
        for loop_index in loops:
            loop = mesh.loops[loop_index]
            p = matrix @ mesh.vertices[loop.vertex_index].co
            n = (normal_matrix @ mesh.corner_normals[loop_index].vector).normalized()
            texcoord = uv.data[loop_index].uv if uv else (0, 0)
            vertices.append(dict(p=engine(p), n=engine(n), uv=[float(texcoord[0]), 1-float(texcoord[1])]))
        parts[part_id]['triangles'].append(dict(face=first_face + face, vertices=vertices))


def _export_asset(obj, filepath, objects):
    groups, parts = part_settings(obj.data)
    for part in parts.values():
        part['triangles'] = []
    next_face = 0
    for source in objects:
        snapshot_mesh(source, parts, next_face)
        next_face += len(source.data.polygons)
    if 0 in parts and not obj.data.yan_enemy_unassigned_blocks_bullets:
        parts[0]['bulletCollision'] = False
    if not parts or any(not part['triangles'] for part in parts.values()):
        raise ValueError("Each Part needs faces; assign faces or remove empty Parts")
    data = dict(version=1, coordinateSystem='yanengine', texture=obj.data.yan_enemy_texture,
                hpGroups=groups, parts=list(parts.values()))
    return write_asset(filepath, data)


def write_asset(filepath, data):
    output = Path(filepath)
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(output.suffix + '.tmp')
    temp.write_text(json.dumps(data, ensure_ascii=False, separators=(',', ':'), allow_nan=False), encoding='utf-8')
    temp.replace(output)
    return data


def set_face_values(mesh, name, values):
    attribute = mesh.attributes.new(name=name, type='INT', domain='FACE')
    attribute.data.foreach_set('value', values)


def read_settings(mesh, document):
    mesh.yan_enemy_parts.clear()
    mesh.yan_enemy_groups.clear()
    mesh.yan_enemy_next_id = 0
    for item in document['hpGroups']:
        group = mesh.yan_enemy_groups.add()
        group.name = item['id']
        group.max_hp = item['maxHp']
        group.death_on_zero = item.get('deathOnZero', True)
    mesh.yan_enemy_unassigned_blocks_bullets = document.get('unassignedFacesBlockBullets', True)
    for item in document['parts']:
        if item['name'] == '__Unassigned':
            if document['version'] == 1:
                mesh.yan_enemy_unassigned_blocks_bullets = item.get('bulletCollision', True)
            continue
        part = add_part(mesh, item['name'], item.get('role', 'Generic'))
        hp, shared = item.get('localHp'), item.get('sharedHpGroup', '')
        part.hp_mode = ('LocalAndShared' if shared else 'Local') if hp is not None else ('Shared' if shared else 'Invincible')
        if hp is not None:
            part.local_hp = hp
        part.shared_group = shared
        part.shared_rate = item.get('sharedDamageRate', 1.0)
        part.breakable = item.get('breakable', True)
        part.bullet_collision = item.get('bulletCollision', True)
        part.death_on_zero = item.get('deathOnZero', False)
    mesh.yan_enemy_part_index = 0
    mesh.yan_enemy_group_index = 0
    mesh.yan_enemy_texture = document.get('texture', 'resources/white1x1.png')


def blender_point(values):
    x, y, z = values
    return -x, -z, y


def welded_triangles(triangles):
    """Share exact positions across seams. Keep duplicate or collapsed faces intact."""
    unique, seen_faces, vertices, faces = {}, set(), [], []
    for triangle in triangles:
        points = [tuple(point) for point in triangle]
        indices = []
        for point in points:
            if point not in unique:
                unique[point] = len(vertices)
                vertices.append(point)
            indices.append(unique[point])
        key = tuple(sorted(indices))
        if len(set(indices)) != 3 or key in seen_faces:
            indices = list(range(len(vertices), len(vertices)+3))
            vertices.extend(points)
        else:
            seen_faces.add(key)
        faces.append(indices)
    return vertices, faces


def connected_face_sizes(mesh):
    # Connectivity uses shared edges, so shells touching at one point remain separate.
    parent = list(range(len(mesh.polygons)))

    def root(index):
        while index != parent[index]:
            parent[index] = parent[parent[index]]
            index = parent[index]
        return index

    edge_owner = {}
    for face in mesh.polygons:
        for edge in face.edge_keys:
            other = edge_owner.setdefault(tuple(sorted(edge)), face.index)
            parent[root(face.index)] = root(other)
    sizes = {}
    for i in range(len(parent)):
        group = root(i)
        sizes[group] = sizes.get(group, 0) + 1
    return sorted(sizes.values(), reverse=True)


def make_edit_mesh(collection, name, records, materials):
    corners = [list(reversed(record['vertices'])) for record in records]
    vertices, faces = welded_triangles([[blender_point(v['p']) for v in tri] for tri in corners])
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    uv = mesh.uv_layers.new(name='UVMap')
    uv.uv.foreach_set('vector', [c for triangle in corners for v in triangle for c in (v['uv'][0], 1-v['uv'][1])])
    mesh.normals_split_custom_set([blender_point(v['n']) for triangle in corners for v in triangle])
    for material in materials:
        mesh.materials.append(material)
    for polygon, record in zip(mesh.polygons, records):
        polygon.use_smooth = True
        if materials and 'id' in record:
            polygon.material_index = record['id'][2]
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj


def make_materials(filepath, descriptions):
    result = []
    for index, item in enumerate(descriptions):
        material = bpy.data.materials.new('Enemy Material %d' % index)
        material.diffuse_color = item['color']
        texture = item.get('texture', '')
        if texture:
            candidates = [Path(texture), *(folder / texture for folder in filepath.parents)]
            path = next((candidate for candidate in candidates if candidate.is_file()), None)
            if path:
                try:
                    image = bpy.data.images.load(str(path.resolve()), check_existing=True)
                    material.use_nodes = True
                    shader = material.node_tree.nodes.get('Principled BSDF')
                    image_node = material.node_tree.nodes.new('ShaderNodeTexImage')
                    image_node.image = image
                    material.node_tree.links.new(image_node.outputs['Color'], shader.inputs['Base Color'])
                    material.node_tree.links.new(image_node.outputs['Alpha'], shader.inputs['Alpha'])
                except RuntimeError:
                    pass
        result.append(material)
    return result


def reference_faces(objects):
    for obj in objects:
        mesh = obj.data
        if obj.modifiers:
            raise ValueError("Remove modifiers before exporting face references")
        attributes = [mesh.attributes.get(name) for name in SOURCE_ATTRIBUTES]
        if any(a is None or a.domain != 'FACE' or a.data_type != 'INT' for a in attributes):
            raise ValueError("Engine face ID attributes are missing")
        owners = assignments(mesh)
        for face in mesh.polygons:
            if len(face.vertices) != 3:
                raise ValueError("The engine editing mesh must keep its original triangles")
            key = tuple(attr.data[face.index].value for attr in attributes)
            positions = [tuple(obj.matrix_world @ mesh.vertices[i].co) for i in face.vertices]
            yield key, owners[face.index], positions


def reference_digest(faces):
    digest = hashlib.sha256()
    for key, _, positions in faces:
        coordinates = [float(value) if value else 0.0 for point in positions for value in point]
        digest.update(struct.pack('<4I9f', *key, *coordinates))
    return digest.hexdigest()


def export_reference(obj, collection, filepath):
    reference = json.loads(collection[MODEL_REFERENCE])
    groups, parts = part_settings(obj.data)
    faces = sorted(reference_faces(collection.objects))
    if len(faces) != reference['faceCount'] or reference_digest(faces) != collection[MESH_SIGNATURE]:
        raise ValueError("The engine editing mesh changed. Reimport it before exporting face references.")
    for key, part_id, _ in faces:
        if part_id:
            if part_id not in parts:
                raise ValueError("Face references a missing Part; use Clear Assignment")
            parts[part_id]['faces'].append(list(key))
    if any(not part['faces'] for part in parts.values()):
        raise ValueError("Each Part needs faces; assign faces or remove empty Parts")
    data = dict(version=2, coordinateSystem='yanengine', model=reference, hpGroups=groups, parts=list(parts.values()))
    if not obj.data.yan_enemy_unassigned_blocks_bullets:
        data['unassignedFacesBlockBullets'] = False
    return write_asset(filepath, data)


def import_asset(context, filepath):
    filepath = Path(filepath)
    document = json.loads(filepath.read_text(encoding='utf-8'))
    if document['coordinateSystem'] != 'yanengine' or document['version'] not in {1, 2}:
        raise ValueError("Unsupported Enemy Asset format")
    version = document['version']
    by_instance = {}
    owners = {}
    part_index = 0
    for part in document['parts']:
        assigned = part['name'] != '__Unassigned'
        if assigned:
            part_index += 1
        for face in part['faces']:
            key = tuple(face) if version == 2 else face
            if key in owners:
                raise ValueError("Duplicate face assignments")
            owners[key] = part_index if assigned else 0

    if version == 2:
        proxy_path = filepath.with_name(filepath.name + '.mesh.json')
        if not proxy_path.is_file():
            raise ValueError("Export the Blender mesh from Enemy Editor and place its .mesh.json beside this file")
        proxy = json.loads(proxy_path.read_text(encoding='utf-8'))
        reference = document['model']
        compare = ('path', 'geometrySignature', 'faceCount')
        keys = [tuple(record['id']) for record in proxy['faces']]
        if (any(proxy['model'][key] != reference[key] for key in compare) or
                len(keys) != reference['faceCount'] or len(set(keys)) != len(keys) or not owners.keys() <= set(keys)):
            raise ValueError("The Blender mesh does not match this Enemy Asset")
        for record in proxy['faces']:
            by_instance.setdefault(record['id'][0], []).append(record)
        materials = make_materials(filepath, proxy['materials'])
    else:
        by_instance[0] = [record for part in document['parts'] for record in part['triangles']]
        materials = make_materials(filepath, [dict(color=[1, 1, 1, 1], texture=document.get('texture', ''))])
    if not by_instance or any(not records for records in by_instance.values()):
        raise ValueError("Enemy Asset contains no triangles")

    collection = bpy.data.collections.new(filepath.stem + ' Parts')
    context.collection.children.link(collection)
    created = []
    for instance, records in by_instance.items():
        obj = make_edit_mesh(collection, '%s %d' % (filepath.stem, instance), records, materials)
        created.append(obj)
        if version == 2:
            for i, name in enumerate(SOURCE_ATTRIBUTES):
                set_face_values(obj.data, name, [record['id'][i] for record in records])
        ids = [tuple(record['id']) if version == 2 else record['face'] for record in records]
        set_face_values(obj.data, ATTRIBUTE, [owners.get(key, 0) for key in ids])
    read_settings(created[0].data, document)
    if version == 2:
        collection[MODEL_REFERENCE] = json.dumps(reference)
        created[0][CONTROLLER] = True
        context.view_layer.update()
        collection[MESH_SIGNATURE] = reference_digest(sorted(reference_faces(created)))
    bpy.ops.object.select_all(action='DESELECT')
    for obj in created:
        obj.select_set(True)
    context.view_layer.objects.active = created[0]
    return created


class YAN_EnemyPart(bpy.types.PropertyGroup):
    part_id: IntProperty(default=0)
    role: EnumProperty(items=[(x, x, '') for x in ROLES], default='Generic')
    hp_mode: EnumProperty(items=[(x, x, '') for x in ['Local', 'Shared', 'LocalAndShared', 'Invincible']], default='Local')
    local_hp: FloatProperty(name='Local HP', default=60, min=.001, max=1e8)
    shared_group: StringProperty(name='Shared HP Group')
    shared_rate: FloatProperty(name='Shared Damage Rate', default=1, min=0, max=1e8)
    breakable: BoolProperty(name='Breakable', default=True)
    bullet_collision: BoolProperty(name='Bullet Collision', default=True)
    death_on_zero: BoolProperty(name='Death on Local HP Zero', default=False)


class YAN_EnemyHpGroup(bpy.types.PropertyGroup):
    max_hp: FloatProperty(name='Max HP', default=5000, min=.001, max=1e8)
    death_on_zero: BoolProperty(name='Death on Group HP Zero', default=True)


class YAN_OT_enemy_part(bpy.types.Operator):
    bl_idname = 'yan.enemy_part'
    bl_label = 'Enemy Part'
    bl_options = {'REGISTER', 'UNDO'}
    action: EnumProperty(items=[(x, x, '') for x in ['ADD', 'REMOVE', 'ASSIGN', 'SELECT', 'CLEAR', 'GROUP_ADD', 'GROUP_REMOVE']])

    def execute(self, context):
        try:
            obj = active_mesh(context)
            mesh = controller(obj).data
            objects = selected_meshes(context, obj)
            if self.action == 'REMOVE' and (collection := asset_collection(obj)) is not None:
                objects = list(collection.objects)
            if self.action in {'REMOVE', 'ASSIGN', 'SELECT', 'CLEAR'} and any(source.data.users > 1 for source in objects):
                raise ValueError("Make the selected mesh data single-user before changing face assignments")
            if self.action == 'ADD':
                add_part(mesh, 'Part' + str(mesh.yan_enemy_next_id + 1))
            elif self.action == 'GROUP_ADD':
                group = mesh.yan_enemy_groups.add()
                group.name = 'Group' + str(len(mesh.yan_enemy_groups))
                mesh.yan_enemy_group_index = len(mesh.yan_enemy_groups) - 1
            elif self.action == 'GROUP_REMOVE':
                if mesh.yan_enemy_groups:
                    mesh.yan_enemy_groups.remove(mesh.yan_enemy_group_index)
                    mesh.yan_enemy_group_index = max(0, len(mesh.yan_enemy_groups)-1)
            elif self.action == 'CLEAR':
                for source in objects:
                    edit_faces(source, 'CLEAR')
            elif mesh.yan_enemy_parts:
                index = mesh.yan_enemy_part_index
                part = mesh.yan_enemy_parts[index]
                for source in objects:
                    edit_faces(source, self.action, part.part_id)
                if self.action == 'REMOVE':
                    mesh.yan_enemy_parts.remove(index)
                    mesh.yan_enemy_part_index = max(0, min(index, len(mesh.yan_enemy_parts)-1))
            return {'FINISHED'}
        except (ValueError, IndexError, StopIteration) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_OT_enemy_export(bpy.types.Operator, ExportHelper):
    bl_idname = 'yan.export_enemy_parts'
    bl_label = 'Export Enemy Asset'
    filename_ext = '.enemy.json'
    filter_glob: StringProperty(default='*.json', options={'HIDDEN'})

    def execute(self, context):
        try:
            obj = active_mesh(context)
            data = export_asset(obj, self.filepath, selected_meshes(context, obj))
            self.report({'INFO'}, 'Exported %d parts' % len(data['parts']))
            return {'FINISHED'}
        except (ValueError, OSError) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_OT_enemy_import(bpy.types.Operator, ImportHelper):
    bl_idname = 'yan.import_enemy_parts'
    bl_label = 'Import Enemy Asset'
    bl_options = {'REGISTER', 'UNDO'}
    filename_ext = '.enemy.json'
    filter_glob: StringProperty(default='*.enemy.json', options={'HIDDEN'})

    def execute(self, context):
        if context.object and context.object.mode != 'OBJECT':
            self.report({'ERROR'}, 'Switch to Object Mode before importing an Enemy Asset')
            return {'CANCELLED'}
        try:
            objects = import_asset(context, self.filepath)
            islands = sum(len(connected_face_sizes(obj.data)) for obj in objects)
            self.report({'INFO'}, 'Imported %d meshes, %d edge-connected islands' % (len(objects), islands))
            return {'FINISHED'}
        except (ValueError, OSError, TypeError, KeyError, IndexError, RuntimeError) as error:
            self.report({'ERROR'}, str(error))
            return {'CANCELLED'}


class YAN_PT_enemy_parts(bpy.types.Panel):
    bl_label = 'Enemy Parts'
    bl_idname = 'YAN_PT_enemy_parts'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Enemy Parts'

    def draw(self, context):
        layout = self.layout
        layout.operator('yan.import_enemy_parts')
        obj = context.active_object
        if not obj or obj.type != 'MESH':
            return
        mesh = controller(obj).data
        layout.prop(mesh, 'yan_enemy_unassigned_blocks_bullets')
        layout.template_list('UI_UL_list', 'enemy_parts', mesh, 'yan_enemy_parts', mesh, 'yan_enemy_part_index')
        row = layout.row()
        row.operator('yan.enemy_part', text='Add Part').action = 'ADD'
        row.operator('yan.enemy_part', text='Remove Part').action = 'REMOVE'
        if mesh.yan_enemy_parts:
            part = mesh.yan_enemy_parts[mesh.yan_enemy_part_index]
            layout.prop(part, 'name', text='Part Name')
            layout.prop(part, 'role', text='Role')
            layout.prop(part, 'bullet_collision')
            layout.operator('yan.enemy_part', text='Assign Selected Faces').action = 'ASSIGN'
            layout.operator('yan.enemy_part', text='Select Part Faces').action = 'SELECT'
            layout.prop(part, 'hp_mode', text='HP Mode')
            if part.hp_mode in {'Local', 'LocalAndShared'}:
                layout.prop(part, 'local_hp')
                layout.prop(part, 'breakable')
                layout.prop(part, 'death_on_zero')
            if part.hp_mode in {'Shared', 'LocalAndShared'}:
                layout.prop_search(part, 'shared_group', mesh, 'yan_enemy_groups')
                layout.prop(part, 'shared_rate')
        layout.operator('yan.enemy_part', text='Clear Assignment (selected faces)').action = 'CLEAR'
        layout.separator()
        layout.label(text='HP Groups')
        layout.template_list('UI_UL_list', 'enemy_groups', mesh, 'yan_enemy_groups', mesh, 'yan_enemy_group_index', rows=3)
        row = layout.row()
        row.operator('yan.enemy_part', text='Add Group').action = 'GROUP_ADD'
        row.operator('yan.enemy_part', text='Remove Group').action = 'GROUP_REMOVE'
        if mesh.yan_enemy_groups:
            group = mesh.yan_enemy_groups[mesh.yan_enemy_group_index]
            layout.prop(group, 'name', text='Group ID')
            layout.prop(group, 'max_hp')
            layout.prop(group, 'death_on_zero')
        if asset_collection(obj) is None:
            layout.prop(mesh, 'yan_enemy_texture', text='Texture (game-relative)')
        layout.operator('yan.export_enemy_parts')


CLASSES = [YAN_EnemyPart, YAN_EnemyHpGroup, YAN_OT_enemy_part, YAN_OT_enemy_export, YAN_OT_enemy_import, YAN_PT_enemy_parts]


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Mesh.yan_enemy_parts = CollectionProperty(type=YAN_EnemyPart)
    bpy.types.Mesh.yan_enemy_groups = CollectionProperty(type=YAN_EnemyHpGroup)
    bpy.types.Mesh.yan_enemy_part_index = IntProperty(default=0, min=0)
    bpy.types.Mesh.yan_enemy_group_index = IntProperty(default=0, min=0)
    bpy.types.Mesh.yan_enemy_next_id = IntProperty(default=0, min=0)
    bpy.types.Mesh.yan_enemy_texture = StringProperty(default='resources/white1x1.png')
    bpy.types.Mesh.yan_enemy_unassigned_blocks_bullets = BoolProperty(name='Unassigned Faces Block Bullets', default=True)


def unregister():
    for name in ['yan_enemy_parts', 'yan_enemy_groups', 'yan_enemy_part_index', 'yan_enemy_group_index',
                 'yan_enemy_next_id', 'yan_enemy_texture', 'yan_enemy_unassigned_blocks_bullets']:
        delattr(bpy.types.Mesh, name)
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


if __name__ == '__main__':
    register()
