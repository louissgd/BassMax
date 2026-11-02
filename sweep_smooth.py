import numpy as np, scipy.signal as sg, os, subprocess
from pair_io import load

SRC='周华健 - 风雨无阻.24bit(原始).wav'
REF='周华健 - 风雨无阻.24bit(音效).wav'
CASES=[
 ('base',        {}),
 ('g2=0.6',      {'BASSMAX_G2REL':'0.6'}),
 ('g2=1.0',      {'BASSMAX_G2REL':'1.0'}),
 ('env150+g2.6', {'BASSMAX_ENV_MS':'0.15','BASSMAX_G2REL':'0.6'}),
 ('lo20/400',    {'BASSMAX_LOATT':'0.020','BASSMAX_LOREL':'0.400'}),
 ('g2.6+lo.4',   {'BASSMAX_G2REL':'0.6','BASSMAX_LOATT':'0.015','BASSMAX_LOREL':'0.400'}),
]
N=int(40*44100)
o,sr=load(SRC); r,_=load(REF)
o=o[:N].mean(1); r=r[:N].mean(1)
def band(x,lo,hi):
    y=x
    if hi: y=sg.lfilter(*sg.butter(4,hi/(sr/2),'low'),y)
    if lo: y=sg.lfilter(*sg.butter(4,lo/(sr/2),'high'),y)
    return y
def env(x,ms=10):
    w=max(1,int(sr*ms/1000)); k=np.ones(w)/w
    return np.sqrt(np.convolve(x*x,k,'same')+1e-18)
LAG={'LO':483,'MID':329,'HI':330}
def rms(x): return np.sqrt((x**2).mean())
def stats(x, lag):
    out=[]
    for bn,lo,hi in [('LO',0,120),('MID',120,2500),('HI',2500,0)]:
        eo=env(band(o,lo,hi)); ex=env(band(x,lo,hi))
        g=20*np.log10((ex+1e-12)/(eo+1e-12)); g=g[eo>1e-4]
        w=int(0.1*sr); gs=np.convolve(g,np.ones(w)/w,'same')
        out.append((np.std(g), np.std(g-gs)))
    return out
print('%-14s %-22s %-22s %-22s | %s' % ('case','LO std/慢起伏','MID std/慢起伏','HI std/慢起伏','响度差/峰值'))
rref=stats(r,0)
print('%-14s %-22s %-22s %-22s |' % ('参考音效', *[f'{a:5.2f}/{b:5.2f}' for a,b in rref]))
for nm,envd in CASES:
    e=dict(os.environ); e.update(envd)
    e.update({'BASSMAX_PRESET':'4','BASSMAX_DRIVE':'1','BASSMAX_BASS':'0','BASSMAX_TRIM_9K':'3'})
    f=f'sw_{nm.replace("=","").replace("/","")}.wav'
    subprocess.run(['./render_wav.exe',SRC,f,'1','0','40'],env=e,stdout=subprocess.DEVNULL,check=True)
    x,_=load(f); x=x[:N].mean(1)
    st=stats(x,0)
    pk=np.abs(x).max()
    print('%-14s %-22s %-22s %-22s | %+5.2f dB / %.3f' % (nm,
        *[f'{a:5.2f}/{b:5.2f}' for a,b in st],
        20*np.log10(rms(x)/rms(r)), pk))
