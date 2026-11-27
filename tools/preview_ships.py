#!/usr/bin/env python3
"""Render every ship to a PNG contact sheet to check the geometry.
Usage: python3 tools/extract_ships.py <elite-source.asm> components/elite_clock/ships.h ships.json
       python3 tools/preview_ships.py ships.json ships.png   (needs Pillow)"""
import sys
import json, math
from PIL import Image, ImageDraw
ships = json.load(open(sys.argv[1]))
def rot(ax, ay, az):
    cx,sx,cy,sy,cz,sz = math.cos(ax),math.sin(ax),math.cos(ay),math.sin(ay),math.cos(az),math.sin(az)
    Rx=[[1,0,0],[0,cx,-sx],[0,sx,cx]]; Ry=[[cy,0,sy],[0,1,0],[-sy,0,cy]]; Rz=[[cz,-sz,0],[sz,cz,0],[0,0,1]]
    mm=lambda A,B:[[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mm(Rz,mm(Ry,Rx))
def ap(R,v): return [sum(R[i][k]*v[k] for k in range(3)) for i in range(3)]
W,H=200,160; names=list(ships); cols=6; rows=math.ceil(len(names)/cols)
img=Image.new('RGB',(W*cols,H*rows)); d=ImageDraw.Draw(img)
R=rot(-0.5,0.7,0.2)
for idx,n in enumerate(names):
    s=ships[n]; ox,oy=(idx%cols)*W,(idx//cols)*H
    V=s['verts']; r=max(math.sqrt(v[0]**2+v[1]**2+v[2]**2) for v in V)
    D=r*3.0; f=(H*0.45)*D/r
    T=[ap(R,v[:3]) for v in V]; T=[[t[0],t[1],t[2]+D] for t in T]
    vis=[True]*32
    if not s['borrowed']:
        for fi,fc in enumerate(s['faces']):
            pts=set()
            for e in s['edges']:
                if e[2]==fi or e[3]==fi: pts.update([e[0],e[1]])
            if not pts: continue
            p=[sum(T[i][k] for i in pts)/len(pts) for k in range(3)]
            nrm=ap(R,fc[:3])
            vis[fi]= (nrm[0]*-p[0]+nrm[1]*-p[1]+nrm[2]*-p[2])>0
    P=[(ox+W/2+t[0]*f/t[2], oy+H/2+8-t[1]*f/t[2]) for t in T]
    for e in s['edges']:
        if s['borrowed'] or vis[e[2]] or vis[e[3]]:
            d.line([P[e[0]],P[e[1]]],fill=(255,255,255))
    d.text((ox+4,oy+2),n,fill=(255,220,0))
img.save(sys.argv[2])
