"""Inspect the actual .blend and render floor plans, elevations and a section.
Blender --background --factory-startup --python-exit-code 1 --python this.py
Add -- --before for the preserved original, without rendering/validation.
Review cameras/cut meshes are temporary and are never saved or exported.
"""
import json
import sys
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter
exporter.register()
root=Path(__file__).resolve().parents[2]
before='--before' in sys.argv
source=root/('generated/hotel-interior-before/stage03/stage03.blend' if before else 'resources/levels/stage03/stage03.blend')
output=root/'generated/hotel-interior-review'
output.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(source))
scene=bpy.context.scene
levels=('F1_Escape','F2_Middle','F3_Start')


def bounds(obj):
    points=[exporter.ENGINE_BASIS @ obj.matrix_world @ Vector((*corner,1)) for corner in obj.bound_box]
    return ([min(p[i] for p in points) for i in range(3)],
            [max(p[i] for p in points) for i in range(3)])


def united(objects):
    boxes=[bounds(obj) for obj in objects]
    return {'min':[round(min(a[i] for a,b in boxes),4) for i in range(3)],
            'max':[round(max(b[i] for a,b in boxes),4) for i in range(3)]}


geometry=[obj for obj in scene.objects if obj.type=='MESH' and obj.yan_level.role=='STATIC']
hotel=[obj for obj in geometry if not any(c.name.startswith('Outdoor') for c in obj.users_collection)]
report={'source':str(source),'hotelBounds':united(hotel),'floors':{}}
for level in levels:
    objects=[obj for obj in hotel if any(c.name.startswith(level) for c in obj.users_collection)]
    plates=[obj for obj in objects if obj.get('hotel_role')=='floor_slab' or obj.name.endswith('_Floor')]
    report['floors'][level]={'geometryBounds':united(objects),
        'slabs':[dict(name=obj.name,min=[round(n,4) for n in bounds(obj)[0]],
                      max=[round(n,4) for n in bounds(obj)[1]]) for obj in plates]}
if before:
    (output/'before-structure.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('BEFORE_STRUCTURE_RECORDED',json.dumps(report['hotelBounds']))
    raise SystemExit(0)

# The building envelope is an invariant, not a box added around scattered rooms.
assert list(scene['hotel_footprint'])==[0,38,0,36]
assert list(scene['hotel_floor_heights'])==[0,6,12]
for obj in hotel:
    a,b=bounds(obj)
    assert a[0]>=-.001 and b[0]<=38.001 and a[2]>=-.001 and b[2]<=36.001, ('outside envelope',obj.name,a,b)
    assert a[1]>=-.351 and b[1]<=15.351, ('outside building height',obj.name,a,b)
holes={levels[0]:None,levels[1]:(32,38,0,10),levels[2]:(0,6,0,10)}
for index,level in enumerate(levels):
    plates=report['floors'][level]['slabs']
    for plate in plates:
        assert abs(plate['max'][1]-index*6)<.001
        assert abs(plate['max'][1]-plate['min'][1]-.35)<.001
    for i,a in enumerate(plates):
        for b in plates[i+1:]:
            overlap=max(0,min(a['max'][0],b['max'][0])-max(a['min'][0],b['min'][0])) * max(0,min(a['max'][2],b['max'][2])-max(a['min'][2],b['min'][2]))
            assert overlap<.001, ('duplicate slabs',a['name'],b['name'])
    # Check the whole footprint, including secondary rooms and service cores.
    for ix in range(48 if index==2 else 76):
        for iz in range(24 if index==2 else 72):
            x,z=(ix+.5)*.5,(iz+.5)*.5
            hole=holes[level]
            expected=not (hole and hole[0]<x<hole[1] and hole[2]<z<hole[3])
            covered=any(p['min'][0]<x<p['max'][0] and p['min'][2]<z<p['max'][2] for p in plates)
            assert covered==expected, ('slab gap/stair obstruction',level,x,z)
    area=sum((p['max'][0]-p['min'][0])*(p['max'][2]-p['min'][2]) for p in plates)
    assert abs(area-([1368,1308,228][index]))<.001
    report['floors'][level]['slabArea']=area
roof=bounds(bpy.data.objects['Hotel_Roof_MainSlab'])
assert all(abs(actual-expected)<.001 for actual,expected in zip(roof[0]+roof[1],[0,15,0,24,15.35,12]))
low_roofs=[obj for obj in hotel if obj.get('hotel_role')=='low_roof_slab']
assert len(low_roofs)==2
assert sum((bounds(o)[1][0]-bounds(o)[0][0])*(bounds(o)[1][2]-bounds(o)[0][2]) for o in low_roofs)==1080
# A complete use map: each point belongs to exactly one purpose on its floor.
zones=json.loads(scene['hotel_zones'])
for floor,width,depth in ((1,38,36),(2,38,36),(3,24,12)):
    floor_zones=[z for z in zones if z['floor']==floor]
    for ix in range(width*2):
        for iz in range(depth*2):
            x,z=(ix+.5)*.5,(iz+.5)*.5
            matches=[v for v in floor_zones if v['bounds'][0]<=x<v['bounds'][1] and v['bounds'][2]<=z<v['bounds'][3]]
            assert len(matches)==1, ('unknown/duplicate spatial use',floor,x,z,matches)
report['zones']=zones
old=json.loads((root/'generated/hotel-interior-before/stage03/stage03.json').read_text(encoding='utf-8'))
new=json.loads((root/'resources/levels/stage03/stage03.json').read_text(encoding='utf-8'))
assert old['spawnPoints']==new['spawnPoints'], 'Future enemy spawn transforms changed'
for item in old['colliders']:
    if item['id'].startswith(('Hotel_Shutter_','Hotel_FireDoor_','Hotel_OutdoorExit_')):
        assert item==next(c for c in new['colliders'] if c['id']==item['id']), ('event fixture changed',item['id'])
for x in (20,28):
    for z in (14,22):
        low=bounds(bpy.data.objects[f'F1_FrameColumn_X{x}_Z{z}'])
        high=bounds(bpy.data.objects[f'F2_FrameColumn_X{x}_Z{z}'])
        assert all(abs(low[side][axis]-high[side][axis])<.001 for side in (0,1) for axis in (0,2))
assert not any('SideWall' in obj.name or ('Ceiling' in obj.name) for obj in hotel)
report['roofBounds']={'min':list(roof[0]),'max':list(roof[1])}
report['checks']='PASS: coherent stepped envelope, nonoverlapping floors/roofs, shared ceilings, compact shaft openings, aligned columns, complete zone uses, preserved spawn/event transforms'
(output/'after-structure.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')

# Workbench uses the real meshes and materials, with no furniture or decoration.
scene.render.engine='BLENDER_WORKBENCH'
scene.render.resolution_x=1600
scene.render.resolution_y=1100
scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.film_transparent=False
shading=scene.display.shading
shading.light='STUDIO'
shading.color_type='MATERIAL'
shading.show_shadows=True
shading.show_cavity=True
shading.cavity_type='BOTH'
shading.show_object_outline=True
shading.background_type='WORLD'
if scene.world is None:
    scene.world=bpy.data.worlds.new('__ArchitectureReviewWorld')
scene.world.color=(.8,.8,.8)
camera=bpy.data.objects.new('__ArchitectureReviewCamera',bpy.data.cameras.new('__ArchitectureReviewCamera'))
scene.collection.objects.link(camera)
camera.data.type='ORTHO'
scene.camera=camera
for obj in scene.objects:
    if obj not in geometry and obj!=camera:
        obj.hide_render=True


def render(name,position,target,scale):
    a=(exporter.ENGINE_BASIS.inverted() @ Vector((*position,1))).to_3d()
    b=(exporter.ENGINE_BASIS.inverted() @ Vector((*target,1))).to_3d()
    camera.location=a
    camera.rotation_euler=(b-a).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=scale
    scene.render.filepath=str(output/f'{name}.png')
    bpy.ops.render.render(write_still=True)


for i,level in enumerate(levels,1):
    for obj in geometry:
        obj.hide_render=not any(c.name.startswith(level) for c in obj.users_collection)
    render(f'0{i}-f{i}-top',(21,80,18),(21,i*6-6,18),58)
for obj in geometry:
    obj.hide_render=obj not in hotel
render('04-top-roof',(21,80,18),(21,0,18),58)
render('05-west-side',(-65,9,18),(21,9,18),48)
for obj in geometry:
    obj.hide_render=False
render('06-exterior',(-45,35,-38),(15,7,18),85)

# Actual mesh sections, including profiles along both staircase centre lines.
def section_render(name,point,normal,keep_positive,position,target,scale):
    cuts=bpy.data.collections.new('__ArchitectureReviewSection')
    scene.collection.children.link(cuts)
    for obj in geometry:
        obj.hide_render=True
    plane_co=(exporter.ENGINE_BASIS.inverted() @ Vector((*point,1))).to_3d()
    plane_no=exporter.ENGINE_BASIS.inverted().to_3x3() @ Vector(normal)
    for obj in hotel:
        bm=bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.transform(bm,matrix=obj.matrix_world,verts=list(bm.verts))
        result=bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
            plane_co=plane_co,plane_no=plane_no,dist=.00001,
            clear_inner=keep_positive,clear_outer=not keep_positive)
        cut_edges=[edge for edge in result['geom_cut'] if isinstance(edge,bmesh.types.BMEdge) and edge.is_boundary]
        if cut_edges:
            bmesh.ops.holes_fill(bm,edges=cut_edges,sides=0)
        if bm.faces:
            mesh=bpy.data.meshes.new(f'__Section_{obj.name}')
            bm.to_mesh(mesh)
            for material in obj.data.materials:
                mesh.materials.append(material)
            section=bpy.data.objects.new(mesh.name,mesh)
            cuts.objects.link(section)
        bm.free()
    render(name,position,target,scale)
    for obj in list(cuts.objects):
        mesh=obj.data
        bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.meshes.remove(mesh)
    bpy.data.collections.remove(cuts)

section_render('07-section-z18',(0,0,18),(0,0,1),True,(21,9,-65),(21,9,18),50)
section_render('08-west-stair-section',(4.4,0,0),(1,0,0),False,(40,10.5,6),(4.4,10.5,6),21)
section_render('09-east-stair-section',(36.4,0,0),(1,0,0),False,(70,5,6),(36.4,5,6),21)
print(report['checks'])
print('ARCHITECTURE_REVIEW_RENDERED',str(output))
