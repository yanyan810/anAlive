"""Rebuild ONLY Stage03: a convention hotel with compact stairs and a three-room guest wing.
Run with Blender --background --factory-startup --python this_file.py.
All coordinates are engine metres (X/right, Y/up, Z/forward).
"""
import json
import math
import sys
from pathlib import Path
import bpy
from mathutils import Euler, Matrix, Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter
exporter.register()
root=Path(__file__).resolve().parents[2]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene
scene.name='Stage03_Hotel'
scene.yan_level.stage_id='stage03'
scene.yan_level.project_root=str(root)
scene.yan_level.output_directory='resources/levels/stage03'
scene.yan_level.player_gravity=True
scene.yan_level.fixed_seed=True
scene['hotel_footprint']=[0,38,0,36]
scene['hotel_floor_heights']=[0,6,12]
scene['hotel_roof_height']=15.35
scene['hotel_slab_thickness']=.35
scene['hotel_stair_shafts']='West X=0..6 Z=0..12; East X=32..38 Z=0..12'
scene['hotel_guest_footprint']=[0,24,0,12]
scene['hotel_floor_holes']='F2:32,38,0,10; F3:0,6,0,10'
parent=bpy.data.collections.new('Stage03_Hotel')
scene.collection.children.link(parent)
groups={}
for floor in ('F1_Escape','F2_Middle','F3_Start','Outdoor'):
    collection=bpy.data.collections.new(floor)
    parent.children.link(collection)
    for kind in ('Floors','Walls','Envelope','Stairs','Colliders','Markers'):
        sub=bpy.data.collections.new(f'{floor}_{kind}')
        collection.children.link(sub)
        groups[f'{floor}_{kind}']=sub
for kind in ('SpawnRooms','ExitFixtures'):
    collection=bpy.data.collections.new(f'F1_Escape_{kind}')
    bpy.data.collections['F1_Escape'].children.link(collection)
    groups[f'F1_Escape_{kind}']=collection
roof_collection=bpy.data.collections.new('Building_Roof')
parent.children.link(roof_collection)
groups['Building_Roof']=roof_collection
materials={}
for key,rgb in {'floor':(.28,.32,.38),'wall':(.55,.60,.65),
                'envelope':(.48,.51,.55),'roof':(.26,.29,.32),'stairs':(.48,.36,.22),
                'F3':(.15,.40,.65),'F2':(.62,.42,.12),'F1':(.16,.52,.38),
                'fixture':(.36,.42,.48),'button':(.16,.80,.25),'curtain':(.035,.045,.055),
                'road':(.15,.18,.21),'white':(.94,.94,.94)}.items():
    material=bpy.data.materials.new(f'Hotel_{key}')
    material.diffuse_color=(*rgb,1)
    material.use_nodes=True
    shader=material.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value=(*rgb,1)
    shader.inputs['Roughness'].default_value=.95
    materials[key]=material


def make(name,group,position,size=None,rotation=(0,0,0),role='STATIC',
         material='wall',collision=True,walkable=False,player_only=False):
    if size is not None:
        bpy.ops.mesh.primitive_cube_add(size=2)
        obj=bpy.context.object
        for old in list(obj.users_collection):
            old.objects.unlink(obj)
    else:
        obj=bpy.data.objects.new(name,None)
    obj.name=name
    groups[group].objects.link(obj)
    engine=Matrix.LocRotScale(Vector(position),Euler(rotation,'XYZ').to_quaternion(),
                             Vector(tuple(v*.5 for v in size) if size else (1,1,1)))
    obj.matrix_world=exporter.ENGINE_BASIS.inverted() @ engine @ exporter.ENGINE_BASIS
    obj.yan_level.role=role
    obj.yan_level.collision='BOX' if collision else 'NONE'
    obj.yan_level.walkable=walkable
    obj.yan_level.player_only=player_only
    obj['hotel_collection']=group
    if size is not None and role=='STATIC':
        obj.data.materials.append(materials[material])
    return obj


def slab(name,floor,x1,x2,z1,z2,top,material='floor'):
    obj=make(name,f'{floor}_Floors',((x1+x2)/2,top-.175,(z1+z2)/2),
             (x2-x1,.35,z2-z1),material=material,walkable=True)
    obj['hotel_role']='floor_slab' if floor!='Outdoor' else 'outdoor_road'
    obj['hotel_floor_top']=top
    obj['hotel_ceiling_under']=top-.35
    return obj


def wall(name,group,axis,fixed,lo,hi,y,doors=(),height=None,material='wall'):
    """Plumb wall with real openings: guest doors 2.6 m, banquet doors 3.2 m."""
    height=(3.0 if y>=12 else 5.65) if height is None else height
    clear=2.6 if y>=12 else 3.2
    cuts=[(lo,lo)]+sorted((center-width/2,center+width/2) for center,width in doors)+[(hi,hi)]
    def segment(suffix,a,b,bottom,h):
        if b-a<=.01:
            return
        pos=((a+b)/2,bottom+h/2,fixed) if axis=='X' else (fixed,bottom+h/2,(a+b)/2)
        size=(b-a,h,.3) if axis=='X' else (.3,h,b-a)
        make(f'{name}_{suffix}',group,pos,size,material=material)
    for i in range(len(cuts)-1):
        segment(f'Segment{i+1:02}',cuts[i][1],cuts[i+1][0],y,height)
    for i,(a,b) in enumerate(cuts[1:-1]):
        segment(f'Lintel{i+1:02}',a,b,y+clear,height-clear)


def stair(name,floor,x,start_z,end_z,top,bottom):
    run=abs(end_z-start_z)
    angle=math.atan2(top-bottom,run)*(1 if end_z>start_z else -1)
    length=math.hypot(run,top-bottom)
    mid_z=(start_z+end_z)/2
    mid_y=(top+bottom)/2
    make(f'COL_{name}_SmoothRamp',f'{floor}_Colliders',
         (x,mid_y-math.cos(angle)*.15,mid_z-math.sin(angle)*.15),
         (2.3,.3,length),(angle,0,0),role='COLLIDER',walkable=True)
    for i in range(20):
        z=start_z+(end_z-start_z)*(i+.5)/20
        y=top-(top-bottom)*(i+.5)/20
        make(f'{name}_Tread_{i+1:02}',f'{floor}_Stairs',(x,y-.075,z),
             (2.3,.15,run/20+.005),material='stairs',collision=False)
    # Slim stringers and vertical shafts replace the oversized inclined walls.
    for suffix,offset in (('Left',-1.0),('Right',1.0)):
        make(f'{name}_Stringer_{suffix}',f'{floor}_Stairs',
             (x+offset,mid_y-.2,mid_z),(.15,.25,length),(angle,0,0),
             material='stairs',collision=False)


# Two banquet floors; a small guest wing above the north-west end.
# All remaining 2F ceiling is a LOW ROOF, never an unexplained 3F room.
slab('F1_Foundation_Floor','F1_Escape',0,38,0,36,0)
for name,bounds in (
    ('F2_West_Floor',(0,12,0,36)),
    ('F2_Ballroom_Floor',(12,32,6,30)),
    ('F2_BanquetPreparation_Floor',(12,32,0,6)),
    ('F2_BanquetStore_Floor',(12,38,30,36)),
    ('F2_EastGallery_Floor',(32,38,12,30)),
    ('F2_EastSouthLanding_Floor',(32,38,10,12))):
    slab(name,'F2_Middle',*bounds,6)
for i,(x1,x2) in enumerate(((6,12),(12,18),(18,24)),1):
    slab(f'F3_Room30{i}_Floor','F3_Start',x1,x2,0,8,12)
slab('F3_GuestCorridor_Floor','F3_Start',6,24,8,12,12)
slab('F3_WestSouthLanding_Floor','F3_Start',0,6,10,12,12)
for name,bounds in (('Hotel_LowRoof_East',(24,38,0,12)),('Hotel_LowRoof_South',(0,38,12,36))):
    x1,x2,z1,z2=bounds
    obj=make(name,'Building_Roof',((x1+x2)/2,11.825,(z1+z2)/2),(x2-x1,.35,z2-z1),material='roof')
    obj['hotel_role']='low_roof_slab'
roof=make('Hotel_Roof_MainSlab','Building_Roof',(12,15.175,6),(24,.35,12),material='roof')
roof['hotel_role']='roof_slab'

for number,floor,y,width,depth,height in (
    (1,'F1_Escape',0,38,36,6),(2,'F2_Middle',6,38,36,6),(3,'F3_Start',12,24,12,3.35)):
    prefix=f'F{number}_Envelope'
    group=f'{floor}_Envelope'
    wall(f'{prefix}_West',group,'Z',.15,0,depth,y,[(18,4)] if number==1 else (),height,'envelope')
    wall(f'{prefix}_East',group,'Z',width-.15,0,depth,y,height=height,material='envelope')
    wall(f'{prefix}_North',group,'X',.15,0,width,y,height=height,material='envelope')
    wall(f'{prefix}_South',group,'X',depth-.15,0,width,y,height=height,material='envelope')

# Ordinary dogleg stair rooms: two 3 m rises, 20 steps per flight,
# 2.3 m flight width, 8 m run, a full-width turning landing.
for name,floor,base,xbase in (
    ('Stair_F3_to_F2','F3_Start',6,0),('Stair_F2_to_F1','F2_Middle',0,32)):
    stair(name+'_UpperFlight',floor,xbase+4.4,10,2,base+6,base+3)
    stair(name+'_LowerFlight',floor,xbase+1.6,2,10,base+3,base)
    landing=slab(name+'_TurningLanding',floor,xbase,xbase+6,.3,2,base+3,'stairs')
    landing['hotel_role']='stair_landing'
    wall(name+'_CentreWall',f'{floor}_Walls','Z',xbase+3,2,10,base,height=9 if xbase==0 else 6)
    wall(name+'_UpperLandingGuard',f'{floor}_Walls','X',10,xbase+.3,xbase+3,base+6,height=1.1)
# Both levels of west staircase open sideways onto their own floor lobby.
for floor,y in (('F2_Middle',6),('F3_Start',12)):
    wall(f'{floor[:2]}_WestStair_LobbyOpening',f'{floor}_Walls','Z',6,.3,12,y,[(10.8,1.6)])
wall('F2_WestStair_South','F2_Middle_Walls','X',12,.3,6,6)
# East staircase opens south: different halves of the landing on each level.
for floor,y,xdoor in (('F1_Escape',0,33.6),('F2_Middle',6,36.4)):
    wall(f'{floor[:2]}_EastStair_West',f'{floor}_Walls','Z',32,.3,12,y)
    wall(f'{floor[:2]}_EastStair_SouthOpening',f'{floor}_Walls','X',12,32,37.7,y,[(xdoor,2.4)])

# Only the three requested guest rooms. 6 x 8 m rooms and a 4 m corridor.
for x in (12,18):
    wall(f'F3_GuestRoom_Partition_{x}','F3_Start_Walls','Z',x,.3,8,12)
wall('F3_GuestRoom_CorridorWall','F3_Start_Walls','X',8,6,23.7,12,[(9,2.4),(15,2.4),(21,2.4)])
for i,x in enumerate((9,15,21),1):
    make(f'F3_Room30{i}_DoorHeader','F3_Start_Markers',(x,14.45,8.2),(2.2,.18,.08),material='F3',collision=False)
make('PlayerSpawn_F3_Room301','F3_Start_Markers',(9,12,4),role='PLAYER')

# 2F pre-function foyer, a stair lobby, and two banquet support rooms.
# The east gallery is reached THROUGH the ballroom (no outer ring bypass).
wall('F2_StairLobby_FoyerOpening','F2_Middle_Walls','X',12,6,12,6,[(9,4)])
wall('F2_Foyer_CloakroomOpening','F2_Middle_Walls','X',30,.3,12,6,[(6,3)])
wall('F2_Ballroom_West','F2_Middle_Walls','Z',12,.3,35.7,6,[(10,3.2),(26,3.2)])
wall('F2_Ballroom_EastGallery','F2_Middle_Walls','Z',32,12,30,6,[(18,3.2),(26,3.2)])
wall('F2_Ballroom_PreparationOpening','F2_Middle_Walls','X',6,12,32,6,[(22.5,3)])
wall('F2_Ballroom_StoreOpening','F2_Middle_Walls','X',30,12,37.7,6,[(22.5,3)])

# 1F: continuous reception lobbies replace the broken west service core.
# Doors in the exit passage walls make those lobbies usable, with no blind bays.
wall('F1_Ballroom_West_ShutterOpening','F1_Escape_Walls','Z',12,.3,35.7,0,[(18,4)])
for z in (16,20):
    wall(f'F1_ExitCorridor_LobbyOpening_{z}','F1_Escape_Walls','X',z,.3,12,0,[(8,3)])
wall('F1_Ballroom_EastGallery','F1_Escape_Walls','Z',32,12,30,0,[(18,3.2),(26,3.2)])
for floor,y in (('F1_Escape',0),('F2_Middle',6)):
    for x in (20,28):
        for z in (14,22):
            make(f'{floor[:2]}_FrameColumn_X{x}_Z{z}',f'{floor}_Walls',(x,y+2.825,z),(.6,5.65,.6))

# Six contiguous back-of-house holding/store rooms, no inaccessible slit gaps.
# Existing spawn points and event fixture transforms are deliberately preserved.
centers=(16,22.5,29)
edges=(12,19.25,25.75,32)
for side,boundary,outer,sign in (('Upper',6,.3,-1),('Lower',30,35.7,1)):
    wall(f'F1_Ballroom_{side}','F1_Escape_Walls','X',boundary,12,32 if side=='Upper' else 37.7,0,[(x,3) for x in centers])
    for edge in edges[1:-1]:
        wall(f'F1_SpawnRoom_{side}_SharedPartition_{edge}','F1_Escape_SpawnRooms','Z',edge,min(boundary,outer),max(boundary,outer),0)
    for i,x in enumerate(centers,1):
        prefix=f'F1_SpawnRoom_{side}_{i:02}'
        make(f'{prefix}_PrivacyCurtain','F1_Escape_SpawnRooms',(x,1.6,boundary),(3,3.2,.08),material='curtain',collision=False)
        make(f'COL_{prefix}_PlayerGate','F1_Escape_Colliders',(x,2.825,boundary),(3,5.65,.3),role='COLLIDER',player_only=True)
        point=make(f'SP_F1_{side}_{i:02}','F1_Escape_Markers',(x,0,boundary+sign*3),rotation=(0,math.pi if sign>0 else 0,0),role='ENEMY')
        point.yan_level.group='Hotel_Shutter_Future'
        entry=point.yan_level.pool.add()
        entry.identifier='normal'
        entry.weight=1

make('Hotel_Shutter_Raised_Placeholder','F1_Escape_ExitFixtures',(12,3.6,18),(.45,1,4),material='fixture')
make('Hotel_Shutter_Button_Placeholder','F1_Escape_ExitFixtures',(12.25,1.25,21),(.3,.4,.4),material='button')
make('Hotel_FireDoor_Open_Placeholder','F1_Escape_ExitFixtures',(2.9,1.6,19.8),(.15,3.2,3.8),rotation=(0,math.pi/2,0),material='fixture')
make('Hotel_FireDoor_Frame_Header','F1_Escape_ExitFixtures',(1,3.4,18),(.3,.4,4),material='F1')
make('Hotel_OutdoorExit_Header','F1_Escape_ExitFixtures',(.15,3.25,18),(.25,.25,4),material='button')
slab('Outdoor_Road_Floor','Outdoor',-20,0,12,24,0,'road')
for suffix,z in (('North',12),('South',24)):
    wall(f'Outdoor_Boundary_{suffix}','Outdoor_Walls','X',z,-20,0,0,height=1.2)
wall('Outdoor_Road_End','Outdoor_Walls','Z',-20,12,24,0,height=1.2)
for i,x in enumerate((-3,-6,-9,-12,-15,-18),1):
    make(f'Outdoor_RoadMark_{i:02}','Outdoor_Markers',(x,.012,18),(1.5,.02,.12),material='white',collision=False)
make('Outdoor_DistantWhiteStructure_Placeholder','Outdoor_Markers',(-24,2,18),(2,4,2),material='white',collision=False)
make('Goal_Outdoor_CityConnection','Outdoor_Markers',(-16,1.5,18),(3,3,4),role='GOAL')

# Editable zone metadata and floor plans describe uses instead of inventing rooms.
zones=[]
def zone(floor,name,use,bounds,connections,access='player'):
    zones.append(dict(floor=floor,name=name,use=use,bounds=list(bounds),connections=connections,access=access))
for i in range(1,4):
    zone(3,f'Room30{i}','客室・開始室' if i==1 else '客室',(6*i,6*i+6,0,8),['GuestCorridor'])
zone(3,'GuestCorridor','客室前廊下',(6,24,8,12),['Room301','Room302','Room303','WestStair'])
zone(3,'WestStair','2階へ降りる階段室',(0,6,0,12),['GuestCorridor','F2_StairLobby'])
zone(2,'WestStair','3階からの階段室',(0,6,0,12),['F3_GuestCorridor','StairLobby'])
zone(2,'StairLobby','階段ロビー',(6,12,0,12),['WestStair','Foyer','Ballroom'])
zone(2,'Foyer','宴会ホール前室',(0,12,12,30),['StairLobby','Ballroom','Cloakroom'])
zone(2,'Cloakroom','クローク用区画',(0,12,30,36),['Foyer'])
zone(2,'Preparation','宴会準備室',(12,32,0,6),['Ballroom'])
zone(2,'Ballroom','中間階の宴会ホール',(12,32,6,30),['Foyer','StairLobby','Preparation','BanquetStore','EastGallery'])
zone(2,'BanquetStore','宴会備品庫',(12,38,30,36),['Ballroom'])
zone(2,'EastGallery','階段へ続くホール東廊下',(32,38,12,30),['Ballroom','EastStair'])
zone(2,'EastStair','1階へ降りる階段室',(32,38,0,12),['EastGallery','F1_EastGallery'])
zone(1,'ReceptionLobby','ホテル入口ロビー',(0,12,0,16),['ExitPassage'])
zone(1,'EntranceLounge','入口待合ロビー',(0,12,20,36),['ExitPassage'])
zone(1,'ExitPassage','シャッター後の出口通路',(0,12,16,20),['Ballroom','ReceptionLobby','EntranceLounge','FireDoor','Outdoor'])
zone(1,'Ballroom','1階の宴会ホール',(12,32,6,30),['EastGallery','ExitPassage']+[f'{side}{i}' for side in ('Upper','Lower') for i in range(1,4)])
zone(1,'EastGallery','階段からの入口廊下',(32,38,12,30),['EastStair','Ballroom'])
zone(1,'EastStair','2階からの階段室',(32,38,0,12),['F2_EastGallery','EastGallery'])
for side,z1,z2 in (('Upper',0,6),('Lower',30,36)):
    for i in range(1,4):
        zone(1,f'{side}{i}','敵専用の裏方・待機区画',(edges[i-1],38 if side=='Lower' and i==3 else edges[i],z1,z2),['Ballroom'],'enemy_only')
layout=dict(coordinates='engine X right, Y up, Z plan downward; metres',footprint=[0,38,0,36],guestFootprint=[0,24,0,12],floorHeights=[0,6,12],roofHeights=[12,15.35],zones=zones)
scene['hotel_zones']=json.dumps(zones,ensure_ascii=False)
bpy.context.view_layer.update()
exporter.export_level(bpy.context)
(root/'resources/levels/stage03/stage03-layout.json').write_text(json.dumps(layout,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
scene.yan_level.project_root='//../../../'
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.shading.color_type='MATERIAL'
        area.spaces.active.region_3d.view_distance=60
        area.spaces.active.region_3d.view_location=(-19,-18,7)
bpy.ops.object.select_all(action='DESELECT')
player=bpy.data.objects['PlayerSpawn_F3_Room301']
player.select_set(True)
bpy.context.view_layer.objects.active=player
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(root/'resources/levels/stage03/stage03.blend'))
print('STAGE03_INTERIOR_REBUILT: 38x36 banquet base, 24x12 guest wing, 3 rooms, 2 dogleg stair rooms, 6 preserved spawn points')
