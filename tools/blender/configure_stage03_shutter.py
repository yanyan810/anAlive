"""Configure only the existing hotel shutter; do not rebuild the rest of the hotel.
Blender --background --python-exit-code 1 --python this_file.py.
"""
import sys
from pathlib import Path
import bpy
from mathutils import Matrix, Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
import yanengine_level_exporter as exporter

def configure(scene):
    shutter=scene.objects['Hotel_Shutter_Raised_Placeholder']
    button=scene.objects['Hotel_Shutter_Button_Placeholder']
    # Reuse the authored mesh/material; retain X/Z placement and full 4 m width.
    trs, matrix=exporter.engine_transform(shutter.matrix_world)
    trs['position'][1]=1.6
    trs['scale'][1]=1.6
    engine=Matrix.LocRotScale(Vector(trs['position']),matrix.to_quaternion(),Vector(trs['scale']))
    shutter.matrix_world=exporter.ENGINE_BASIS.inverted() @ engine @ exporter.ENGINE_BASIS
    cfg=shutter.yan_level
    cfg.role='SHUTTER'; cfg.shutter_button=button
    cfg.shutter_duration=30; cfg.shutter_distance=3.2; cfg.shutter_reach=2
    cfg.shutter_progress_events='[{"progress":0.5,"event":"halfOpened"}]'
    for kind in ('switch','start','motor','complete'):
        setattr(cfg,f'shutter_{kind}_sound',f'resources/audio/hotel_shutter_{kind}.wav')
    bpy.context.view_layer.update()

if __name__=='__main__':
    exporter.register()
    root=Path(__file__).resolve().parents[2]
    path=root/'resources/levels/stage03/stage03.blend'
    bpy.ops.wm.open_mainfile(filepath=str(path))
    configure(bpy.context.scene)
    exporter.export_level(bpy.context)
    bpy.ops.wm.save_as_mainfile(filepath=str(path))
