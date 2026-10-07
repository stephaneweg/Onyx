#!/usr/bin/env python3
# Onyx case -- a small z-buffer renderer for the previews (MIT licence).
# python3 render_preview.py out.png "file.stl[:dz[:rrggbb]][+file...]@elev,azim" ...
import sys, numpy as np, trimesh
from PIL import Image
def render(parts, elev, azim, W=900, H=650):
    # parts: list of (mesh, rgb, offset)
    e, a = np.radians(elev), np.radians(azim)
    d = np.array([np.cos(e)*np.cos(a), np.cos(e)*np.sin(a), np.sin(e)])   # towards the camera
    right = np.cross([0,0,1], d); right/=np.linalg.norm(right); up = np.cross(d, right)
    allv = np.vstack([m.vertices+o for m,_,o in parts]); c=(allv.max(0)+allv.min(0))/2
    P = lambda v: np.c_[(v-c)@right, (v-c)@up, (v-c)@d]
    pv = P(allv); s = 0.85*min(W/(np.ptp(pv[:,0])), H/(np.ptp(pv[:,1])))
    zb = np.full((H,W), -1e9); img = np.ones((H,W,3))*np.array([0.96,0.96,0.97])
    L = np.array([0.3,-0.5,0.9]); L/=np.linalg.norm(L); L2=np.array([-0.6,0.4,0.3]); L2/=np.linalg.norm(L2)
    for m, rgb, o in parts:
        q = P(m.vertices+o); X = q[:,0]*s+W/2; Y = H/2-q[:,1]*s; Z=q[:,2]
        n = m.face_normals
        sh = 0.28 + 0.6*np.clip(n@L,0,1) + 0.25*np.clip(n@L2,0,1)
        spec = np.clip(n@((L+d)/np.linalg.norm(L+d)),0,1)**30*0.35
        for fi,(i,j,k) in enumerate(m.faces):
            if n[fi]@d <= 0: continue
            xs, ys, zs = X[[i,j,k]], Y[[i,j,k]], Z[[i,j,k]]
            x0,x1 = int(max(0,np.floor(xs.min()))), int(min(W-1,np.ceil(xs.max())))
            y0,y1 = int(max(0,np.floor(ys.min()))), int(min(H-1,np.ceil(ys.max())))
            if x1<x0 or y1<y0: continue
            gx, gy = np.meshgrid(np.arange(x0,x1+1)+0.5, np.arange(y0,y1+1)+0.5)
            den = (ys[1]-ys[2])*(xs[0]-xs[2])+(xs[2]-xs[1])*(ys[0]-ys[2])
            if abs(den)<1e-12: continue
            w0=((ys[1]-ys[2])*(gx-xs[2])+(xs[2]-xs[1])*(gy-ys[2]))/den
            w1=((ys[2]-ys[0])*(gx-xs[2])+(xs[0]-xs[2])*(gy-ys[2]))/den
            w2=1-w0-w1; ins=(w0>=-1e-6)&(w1>=-1e-6)&(w2>=-1e-6)
            if not ins.any(): continue
            z = w0*zs[0]+w1*zs[1]+w2*zs[2]
            sub = zb[y0:y1+1,x0:x1+1]; upd = ins & (z>sub)
            sub[upd]=z[upd]; img[y0:y1+1,x0:x1+1][upd] = np.clip(np.array(rgb)*sh[fi]+spec[fi],0,1)
    return (img*255).astype(np.uint8)
if __name__=="__main__":
    out=sys.argv[1]; specs=sys.argv[2:]; tiles=[]
    for sp in specs:   # "file[:dz][+file...]@elev,azim"
        files, ang = sp.split('@'); el, az = map(float, ang.split(','))
        parts=[]
        for f in files.split('+'):
            fn, _, rest = f.partition(':')
            dz, _, hexcol = rest.partition(':')
            col = (0.62,0.63,0.68) if 'base' in fn else (0.13,0.13,0.15)
            if 'inlay' in fn: col=(0.42,0.53,0.66)
            if 'accent' in fn: col=(0.93,0.63,0.42)
            if hexcol: col = tuple(int(hexcol[i:i+2], 16) / 255 for i in (0, 2, 4))
            parts.append((trimesh.load(fn), col, np.array([0,0,float(dz or 0)])))
        tiles.append(render(parts, el, az))
    Image.fromarray(np.hstack(tiles)).save(out)
