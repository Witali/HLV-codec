#!/usr/bin/env python3
"""Compare two HLV syntax or encoder configurations on identical frames."""

from __future__ import annotations
import argparse, json, math, os, subprocess, tempfile, time
import numpy as np
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ENC=ROOT/'hlvenc'

def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)

def make_y4m(src:Path, out:Path, duration:float, fps:int):
    run(['ffmpeg','-y','-hide_banner','-loglevel','error','-t',str(duration),'-i',str(src),'-an',
         '-vf',f'fps={fps},scale=320:240:force_original_aspect_ratio=decrease:flags=lanczos,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p',
         '-f','yuv4mpegpipe',str(out)])

def read_y4m(path:Path):
    with path.open('rb') as f:
        header=f.readline()
        toks=header.split()
        w=int(next(x[1:] for x in toks if x.startswith(b'W')))
        h=int(next(x[1:] for x in toks if x.startswith(b'H')))
        n=w*h*3//2
        frames=[]
        while True:
            marker=f.readline()
            if not marker: break
            if not marker.startswith(b'FRAME'): raise RuntimeError('bad Y4M marker')
            data=f.read(n)
            if len(data)!=n: raise RuntimeError('truncated Y4M')
            frames.append(data)
    return w,h,frames

def psnr(ref_frames, dist_frames, w,h):
    assert len(ref_frames)==len(dist_frames)
    ys=w*h; cs=ys//4
    sse=[0,0,0]; count=[ys*len(ref_frames),cs*len(ref_frames),cs*len(ref_frames)]
    for a,b in zip(ref_frames,dist_frames):
        aa=np.frombuffer(a,dtype=np.uint8).astype(np.int16)
        bb=np.frombuffer(b,dtype=np.uint8).astype(np.int16)
        diff=aa-bb
        for pi,(off,n) in enumerate(((0,ys),(ys,cs),(ys+cs,cs))):
            d=diff[off:off+n].astype(np.int32)
            sse[pi]+=int(np.dot(d,d))
    vals=[]
    for s,n in zip(sse,count):
        vals.append(float('inf') if s==0 else 10*math.log10(255*255*n/s))
    total_s=sum(sse); total_n=sum(count)
    avg=float('inf') if total_s==0 else 10*math.log10(255*255*total_n/total_s)
    return *vals,avg

def encode_candidate(ref:Path, ref_frames, w,h, version:int, q:int, duration:float, work:Path, deadzone:float, motion_candidates:int):
    out=work/f'v{version}_mc{motion_candidates}_q{q}.hlv'; recon=work/f'v{version}_mc{motion_candidates}_q{q}.y4m'
    quv=min(2040,max(1,round(q*1.35)))
    print(f'  encode v{version} mc={motion_candidates} q={q}', flush=True)
    t=time.perf_counter()
    run([str(ENC),str(ref),str(out),'--syntax',str(version),'--preset','balanced',
         '--qstep-y',str(q),'--qstep-uv',str(quv),'--ac-deadzone',str(deadzone),
         '--motion-candidates',str(motion_candidates),'--recon',str(recon)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    enc_s=time.perf_counter()-t
    _,_,dist=read_y4m(recon)
    py,pu,pv,pa=psnr(ref_frames,dist,w,h)
    bits=out.stat().st_size*8
    return {'version':version,'motion_candidates':motion_candidates,'qstep':q,'qstep_uv':quv,'bytes':out.stat().st_size,
            'bitrate_kbps':bits/duration/1000,'psnr_y':py,'psnr_u':pu,'psnr_v':pv,'psnr_avg':pa,
            'encode_s':enc_s}

def nearest_for_targets(ref,ref_frames,w,h,version,targets,duration,work,deadzone,motion_candidates):
    cache={}
    def get(q):
        q=max(1,min(2040,q))
        if q not in cache: cache[q]=encode_candidate(ref,ref_frames,w,h,version,q,duration,work,deadzone,motion_candidates)
        return cache[q]
    results=[]
    for target in targets:
        lo,hi=1,2040
        while lo<=hi:
            mid=(lo+hi)//2; c=get(mid)
            if c['bitrate_kbps']>target: lo=mid+1
            else: hi=mid-1
        qs={1,2040}
        for q in range(max(1,hi-3),min(2040,lo+3)+1): qs.add(q)
        best=min((get(q) for q in qs),key=lambda c:abs(c['bitrate_kbps']-target))
        row=dict(best); row['target_kbps']=target; row['error_pct']=100*(best['bitrate_kbps']-target)/target
        results.append(row)
    return results, list(cache.values())

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--sources',nargs='+',required=True)
    ap.add_argument('--versions',default='5,6')
    ap.add_argument('--targets',default='200,400,800')
    ap.add_argument('--duration',type=float,default=2.0)
    ap.add_argument('--fps',type=int,default=25)
    ap.add_argument('--deadzone',type=float,default=1.0)
    ap.add_argument('--motion-candidates',type=int,default=1)
    ap.add_argument('--output',required=True)
    a=ap.parse_args()
    versions=[int(x) for x in a.versions.split(',')]
    targets=[float(x) for x in a.targets.split(',')]
    rows=[]; curves=[]
    out=Path(a.output); out.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='hlvcmp_') as td:
        td=Path(td)
        for s0 in a.sources:
            src=Path(s0); sd=td/src.stem; sd.mkdir()
            print(f'source {src}', flush=True)
            ref=sd/'reference.y4m'; make_y4m(src,ref,a.duration,a.fps)
            w,h,rf=read_y4m(ref); duration=len(rf)/a.fps
            for v in versions:
                print(f'version {v}', flush=True)
                vr,vc=nearest_for_targets(ref,rf,w,h,v,targets,duration,sd,a.deadzone,a.motion_candidates)
                for r in vr: r.update(source=src.name,frames=len(rf),duration_s=duration)
                for r in vc: r.update(source=src.name,frames=len(rf),duration_s=duration)
                rows.extend(vr); curves.extend(vc)
                out.write_text(json.dumps({'matched':rows,'curves':curves},indent=2),encoding='utf-8')
    print(out)
if __name__=='__main__': main()
