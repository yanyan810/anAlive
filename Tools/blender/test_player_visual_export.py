"""F7-owned Player Visual survives the real Blender export without preserving stale layout."""
import sys
import json
import shutil
import hashlib
from pathlib import Path
import bpy
sys.path.insert(0, str(Path(__file__).resolve().parent))
import yanengine_level_exporter as e
root=Path(__file__).resolve().parents[2]
e.register()
source=root/'resources/levels/stage01/stage01.blend'
original_hash=hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source))
project=root/'generated/player-visual-export/project'
(project/'resources/Data').mkdir(parents=True,exist_ok=True)
for name in ('enemies.json','weapons.json'):
    shutil.copyfile(root/'resources/Data'/name,project/'resources/Data'/name)
scene=bpy.context.scene
scene.yan_level.project_root=str(project)
scene.yan_level.output_directory='resources/levels/stage01'
target=project/'resources/levels/stage01'
target.mkdir(parents=True,exist_ok=True)
path=target/'stage01.json'
sidecar=target/'enemy_placements.json'
sidecar.write_bytes(b'Enemy placement ownership test: preserve exact bytes\r\n')
original_sidecar=sidecar.read_bytes()
data, _=e.build_level(scene,bpy.context.evaluated_depsgraph_get())
camera_sidecar=target/'stage01.camera_sequences.json'
camera_data=json.loads(json.dumps(data['cameraSequences']))
camera_data[0]['points'][2]['position'][0]+=2.5
camera_data[0]['points'][2].update(pathType='Bezier',easing='EaseInOut',handleType='Free',handleIn=[-1,0,0],handleOut=[1,1,0])
camera_data[0]['points'][3].update(handleType='Free',handleIn=[-1,1,0],handleOut=[1,0,0])
camera_bytes=(json.dumps({'version':1,'stage':'stage01','cameraSequences':camera_data})+'\r\n').encode()
camera_sidecar.write_bytes(camera_bytes)
visual={'yawOffsetDegrees':-42.5,'futureModel':{'asset':'characters/final.gltf','nativeForward':'+Z'}}
old=dict(data,playerVisual=visual,staleBlenderField='must not reimport')
path.write_text(json.dumps(old),encoding='utf-8')
for _ in range(2):
    assert e.export_level(bpy.context)==target
    exported=json.loads(path.read_text(encoding='utf-8'))
    assert exported['playerVisual']==visual
    assert exported['cameraSequences']==data['cameraSequences']
    assert 'staleBlenderField' not in exported
    exported.pop('playerVisual')
    assert exported==data
    assert sidecar.read_bytes()==original_sidecar
    assert camera_sidecar.read_bytes()==camera_bytes
# Unsafe/unknown settings must abort before publication, preserving all existing bytes.
for bad in (None,[],{'yawOffsetDegrees':True},{'yawOffsetDegrees':'90'},
            {'yawOffsetDegrees':181},{'yawOffsetDegrees':float('nan')}):
    path.write_text(json.dumps(dict(data,playerVisual=bad)),encoding='utf-8')
    before={p.relative_to(target):p.read_bytes() for p in target.rglob('*') if p.is_file()}
    try:
        e.export_level(bpy.context)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid Player Visual was overwritten by Export')
    after={p.relative_to(target):p.read_bytes() for p in target.rglob('*') if p.is_file()}
    assert before==after
# Old stages remain compatible, with no new presentation field introduced implicitly.
path.write_text(json.dumps(data),encoding='utf-8')
e.export_level(bpy.context)
assert 'playerVisual' not in json.loads(path.read_text())
assert sidecar.read_bytes()==original_sidecar
assert camera_sidecar.read_bytes()==camera_bytes
assert hashlib.sha256(source.read_bytes()).hexdigest()==original_hash
e.unregister()
print('PASS: actual Blender re-export retains edited F7 camera sidecar byte-for-byte, F7 yaw and future model fields, refreshes Blender layout/camera base, preserves enemy sidecar, rejects invalid tuning atomically, legacy export and source blend unchanged')
