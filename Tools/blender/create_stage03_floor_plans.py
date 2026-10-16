"""Create labelled PNG/SVG plans from the exported Stage03 layout and collision.
Run with Python + Pillow. No Blender scene or game asset is modified.
"""
import html
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root=Path(__file__).resolve().parents[2]
source=root/'resources/levels/stage03'
layout=json.loads((source/'stage03-layout.json').read_text(encoding='utf-8'))
level=json.loads((source/'stage03.json').read_text(encoding='utf-8'))
output=root/'docs/stage03-floor-plans'
output.mkdir(parents=True,exist_ok=True)
font_path=Path('C:/Windows/Fonts/meiryo.ttc')
labels={**{f'Room{i}':f'{i}\n客室' for i in range(301,309)},'Room308':'308\nSTART',
    'Linen':'リネン室','Cleaning':'清掃用具室','ServiceAccess':'設備\n通路','ElevatorHall':'EVホール', 'ElevatorBay':'停止EV',
    'GuestCorridor':'客室前廊下','WestStair':'西階段\n3F ↔ 2F','EastStair':'東階段\n2F ↔ 1F',
    'StairLobby':'階段\nロビー','Foyer':'宴会ホール\n前室','Cloakroom':'クローク区画',
    'Preparation':'宴会準備室','Ballroom':'宴会ホール','BanquetStore':'宴会備品庫',
    'EastGallery':'ホール\n東廊下','ReceptionLobby':'ホテル入口ロビー',
    'EntranceLounge':'入口待合ロビー','ExitPassage':'出口通路'}
paths={3:[(34,19),(35.25,19),(35.25,11.5),(8,11.5),(8,10.8),(4.4,10.8)],
    2:[(1.6,10.8),(9,10.8),(9,26),(16,26),(29,26),(35,26),(36.4,14),(36.4,10.8)],
    1:[(33.6,10.8),(33.6,14),(35,18),(30,18),(12,18),(0,18),(-16,18)]}

for floor in (1,2,3):
    minimum=-20 if floor==1 else 0
    scale=18 if floor==1 else 26
    left,top=76,145
    width=int((38-minimum)*scale+left*2)
    height=36*scale+top+150
    canvas=Image.new('RGB',(width,height),'#f7f9fc')
    draw=ImageDraw.Draw(canvas)
    svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
         '<rect width="100%" height="100%" fill="#f7f9fc"/>']
    def point(x,z): return (left+(x-minimum)*scale,top+z*scale)
    def rectangle(box,color,outline=None):
        x1,x2,z1,z2=box
        a,b=point(x1,z1),point(x2,z2)
        draw.rectangle((*a,*b),fill=color,outline=outline,width=1)
        svg.append(f'<rect x="{a[0]}" y="{a[1]}" width="{b[0]-a[0]}" height="{b[1]-a[1]}" fill="{color}" stroke="{outline or color}"/>')
    def line(points,color,width=3):
        pixels=[point(x,z) for x,z in points]
        draw.line(pixels,fill=color,width=width)
        svg.append('<polyline points="'+ ' '.join(f'{x},{y}' for x,y in pixels)+f'" fill="none" stroke="{color}" stroke-width="{width}"/>')
    def text_at(x,y,value,size=22,color='#172941',center=True):
        font=ImageFont.truetype(str(font_path),size)
        lines=value.split('\n')
        for i,string in enumerate(lines):
            yy=y+(i-(len(lines)-1)/2)*(size+4)
            draw.text((x,yy),string,font=font,fill=color,anchor='mm' if center else 'lm')
            svg.append(f'<text x="{x}" y="{yy}" text-anchor="{"middle" if center else "start"}" dominant-baseline="central" font-family="Meiryo, sans-serif" font-size="{size}" fill="{color}">{html.escape(string)}</text>')
    text_at(left,42,f'Stage03 ホテル・{floor}階 平面図',28,center=False)
    text_at(left,83,f'床 Y={(floor-1)*6} m　｜　上が北（Z=0）、右が東（+X）　｜　単位 m',18,center=False)
    if floor==1:
        rectangle((-20,0,12,24),'#d4dce6','#b3becb')
        text_at(*point(-10,14),'屋外道路',20)
    if floor==3:
        rectangle((0,38,0,36),'#e4e7ec','#bec7d3')
        text_at(*point(20,29),'低い屋根（上面Y=12）\nこの下は2階ホール・前室\n屋内の3階フロアには含めない',24,color='#6f7d90')
    floor_zones=[zone for zone in layout['zones'] if zone['floor']==floor]
    for zone in floor_zones:
        name=zone['name']
        color=('#f3d6d6' if zone['access']=='enemy_only' else '#d8e9ff' if name.startswith('Room')
               else '#d5e2f0' if 'Stair' in name and name!='StairLobby'
               else '#f1e5c8' if name=='Ballroom' else '#e2ede2' if name in ('Preparation','BanquetStore','Cloakroom') else '#e9eff7')
        rectangle(zone['bounds'],color,'#b4c0cf')
    # Draw the actual exported wall cross section at player torso height.
    plane=(floor-1)*6+.8
    for collider in level['colliders']:
        name=collider['id']
        if collider.get('walkable') or any(abs(v)>.001 for v in collider['rotation']): continue
        if not (name.startswith(f'F{floor}_') or name.startswith(f'COL_F{floor}_') or name.startswith('Stair_')): continue
        x,y,z=collider['position']; sx,sy,sz=collider['scale']
        if not y-sy<=plane<=y+sy: continue
        rectangle((x-sx,x+sx,z-sz,z+sz),'#b54444' if collider.get('playerOnly') else '#344358')
    line(paths[floor],'#cd7920',4)
    # Show the two flights and turn before labels, so their names remain readable.
    stairs=[(0,'西')] if floor==3 else [(0,'西'),(32,'東')] if floor==2 else [(32,'東')]
    for base,side in stairs:
        for x in (base+1.6,base+4.4):
            for i in range(20): line([(x-1.05,2+i*.4),(x+1.05,2+i*.4)],'#657d97',1)
        line([(base+4.4,10),(base+4.4,1.1),(base+1.6,1.1),(base+1.6,10)],'#507296',3)
    for zone in floor_zones:
        x1,x2,z1,z2=zone['bounds']
        label=labels.get(zone['name'],zone['name'])
        if zone['access']=='enemy_only': label=f'敵待機 {zone["name"][-1]}\n侵入不可'
        if zone['name']=='ExitPassage': continue
        x,z=(x1+x2)/2,(z1+z2)/2
        if zone['name']=='GuestCorridor': z=11.5
        if zone['name']=='Ballroom': z=16 if floor==2 else 22
        if zone['name']=='EastGallery': z=21
        if zone['name']=='StairLobby': z=6
        if zone['name'] in ('WestStair','EastStair'):
            rectangle((x-2.4,x+2.4,z-1.5,z+1.5),'#d5e2f0')
        text_at(*point(x,z),label,18 if floor==1 else 21)
    if floor==1:
        for item in level['spawnPoints']:
            x,y,z=item['position']; px,py=point(x,z)
            draw.ellipse((px-4,py-4,px+4,py+4),fill='#a63838')
            svg.append(f'<circle cx="{px}" cy="{py}" r="4" fill="#a63838"/>')
        text_at(*point(12,15),'シャッター',16)
        text_at(*point(8,18),'出口通路',15)
        text_at(*point(2.5,21.5),'防火扉',16)
        text_at(*point(14.5,21),'ボタン',15)
        text_at(*point(-16,21),'屋外Goal',17)
    for x in range(minimum,39,6): text_at(*point(x,-1),str(x),14,color='#64748b')
    for z in range(0,37,6): text_at(left-25,top+z*scale,str(z),14,color='#64748b')
    text_at(left,height-91,'橙線：主な進行経路　青：階段　赤：Player専用侵入防止ゲート',16,center=False)
    text_at(left,height-55,'家具・装飾は省略。壁線と出入口はstage03.jsonのCollisionに対応。',16,center=False)
    svg.append('</svg>')
    canvas.save(output/f'f{floor}.png')
    (output/f'f{floor}.svg').write_text('\n'.join(svg)+'\n',encoding='utf-8')
print('PASS: labelled F1/F2/F3 PNG and SVG plans created from exported layout/collision')
