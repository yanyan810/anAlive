"""Read-only PMX/GLB inventory used to author explicit secondary-motion profiles."""
import json, struct, sys
sys.stdout.reconfigure(encoding='utf-8')
from pathlib import Path

def inspect(path):
    data = path.read_bytes()
    offset = 0
    def read(fmt):
        nonlocal offset
        values = struct.unpack_from('<' + fmt, data, offset)
        offset += struct.calcsize('<' + fmt)
        return values[0] if len(values) == 1 else values
    def skip(n):
        nonlocal offset
        offset += n
    assert data[:4] == b'PMX '
    skip(8)
    size = read('B'); globals_ = read('B' * size)
    enc, uv, vi, ti, _, bi = globals_[:6]
    def txt():
        nonlocal offset
        n = read('i'); s = data[offset:offset+n].decode('utf-16-le' if enc == 0 else 'utf-8'); skip(n); return s
    def idx(n): return read({1:'b',2:'h',4:'i'}[n])
    name = txt(); txt(); txt(); txt()
    nv = read('i'); positions=[]; skins=[]
    for _ in range(nv):
        positions.append(read('fff')); skip(20 + uv*16)
        mode = read('B')
        if mode == 0: bones=[idx(bi)]; weights=[1.]
        elif mode in (1,3):
            bones=[idx(bi),idx(bi)]; w=read('f'); weights=[w,1-w]
            if mode==3: skip(36)
        else: bones=[idx(bi) for _ in range(4)]; weights=list(read('ffff'))
        skins.append(list(zip(bones,weights))); skip(4)
    ni=read('i'); skip(ni*vi)
    textures=[txt() for _ in range(read('i'))]
    mats=[]
    for _ in range(read('i')):
        mn=txt(); txt(); skip(65); texture=idx(ti); idx(ti); skip(1)
        shared=read('B'); skip(1 if shared else ti); txt(); count=read('i')
        mats.append({'name':mn,'indices':count,'texture':textures[texture] if texture>=0 else ''})
    weighted={}
    for skin in skins:
        for bone in set(b for b,w in skin if w>0): weighted[bone]=weighted.get(bone,0)+1
    bones=[]
    for i in range(read('i')):
        bn=txt(); en=txt(); pos=read('fff'); parent=idx(bi); skip(4); flags=read('H')
        skip(bi if flags&1 else 12)
        if flags&0x300: skip(bi+4)
        if flags&0x400: skip(12)
        if flags&0x800: skip(24)
        if flags&0x2000: skip(4)
        if flags&0x20:
            skip(bi+8)
            for _ in range(read('i')):
                skip(bi)
                if read('B'): skip(24)
        bones.append({'index':i,'name':bn,'english':en,'position':pos,'parent':parent,
                      'weighted_vertices':weighted.get(i,0)})
    return {'path':str(path),'name':name,'vertices':nv,'materials':mats,'bones':bones}

root=Path(__file__).resolve().parents[1]
reports=[inspect(root/'resources/MyGtYUhe6t/安比.pmx'),inspect(root/'resources/ema/SakurabaEma_ByPOWER.pmx')]
glb=(root/'resources/MyGtYUhe6t/anbi.glb').read_bytes()
n=struct.unpack_from('<I',glb,12)[0]; j=json.loads(glb[20:20+n])
reports[0]['glb']={'meshes':[m.get('name') for m in j.get('meshes',[])],
                   'animations':[a.get('name') for a in j.get('animations',[])]}
out=root/'generated/cloth/model-inventory.json'
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
for r in reports:
    print(r['name'],r['vertices'],'vertices',len(r['bones']),'bones')
    print('Materials:',', '.join(m['name'] for m in r['materials']))
    for b in r['bones']:
        print(b['index'],b['name'],'parent',b['parent'],'pos',tuple(round(p,3) for p in b['position']),'weights',b['weighted_vertices'])
    print(r.get('glb',''))
