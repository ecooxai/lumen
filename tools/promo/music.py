import numpy as np, wave
SR=44100; BPM=100; beat=60/BPM; DUR=62
N=int(SR*DUR); L=np.zeros(N); R=np.zeros(N)
t=np.arange(N)/SR
def mtof(m): return 440*2**((m-69)/12)
def saw(f,tt,ph=0): return 2*((f*tt+ph)%1)-1
def lp(x,a):  # one-pole lowpass
    y=np.empty_like(x); s=0.0
    for i in range(0,len(x),1024):
        seg=x[i:i+1024]; out=np.empty_like(seg)
        for j,v in enumerate(seg): s+=a*(v-s); out[j]=s
        y[i:i+1024]=out
    return y
chords=[[57,60,64,69],[53,57,60,65],[48,52,55,60],[55,59,62,67]]  # Am F C G
bar=4*beat
# pads
pad=np.zeros(N)
for k in range(int(DUR/bar)+1):
    s=int(k*bar*SR); e=min(N,int((k+1)*bar*SR)+SR//4)
    if s>=N: break
    tt=t[s:e]-t[s]; env=np.minimum(1,tt/0.6)*np.minimum(1,np.maximum(0,(bar+0.25-tt)/0.5))
    sig=sum(saw(mtof(m),tt)+saw(mtof(m)*1.006,tt,0.3)+saw(mtof(m-12)*0.997,tt,0.6) for m in chords[k%4])
    pad[s:e]+=sig*env*0.035
pad=lp(pad,0.06)
# arp
arp=np.zeros(N); step=beat/4
for k in range(int(DUR/step)):
    if k*step<4*bar*0.5: continue
    c=chords[int(k*step/bar)%4]; m=c[[0,1,2,3,2,1,3,1][k%8]]+12
    s=int(k*step*SR); e=min(N,s+int(0.22*SR)); tt=t[s:e]-t[s]
    arp[s:e]+=(np.sign(np.sin(2*np.pi*mtof(m)*tt))*0.5+np.sin(2*np.pi*mtof(m)*2*tt)*0.5)*np.exp(-tt*14)*0.06
arp=lp(arp,0.25)
# bass + kick + hat
bass=np.zeros(N); kick=np.zeros(N); hat=np.zeros(N); rng=np.random.default_rng(1)
for k in range(int(DUR/beat)):
    s=int(k*beat*SR)
    if k*beat>=bar*2:
        e=min(N,s+int(0.45*SR)); tt=t[s:e]-t[s]
        kick[s:e]+=np.sin(2*np.pi*(45*tt+60*(1-np.exp(-tt*30))/30))*np.exp(-tt*7)*0.5
    for half in (0,1):
        hs=s+int((half*0.5+0.5)*beat*SR) if half==0 else s
        if k*beat>=bar*4 and half==0:
            he=min(N,hs+int(0.05*SR)); hat[hs:he]+=rng.standard_normal(he-hs)*np.exp(-np.arange(he-hs)/SR*90)*0.05
    c=chords[int(k*beat/bar)%4]; m=c[0]-24
    if k*beat>=bar*2:
        e=min(N,s+int(beat*0.9*SR)); tt=t[s:e]-t[s]
        bass[s:e]+=(np.sin(2*np.pi*mtof(m)*tt)+0.3*saw(mtof(m),tt))*np.minimum(1,tt/0.01)*np.exp(-tt*2.5)*0.22
hat=hat-lp(hat,0.3)
# simple stereo delay on arp
d=int(beat*0.75*SR); arpL=arp.copy(); arpR=np.zeros(N); arpR[d:]=arp[:-d]*0.6; arpL[2*d:]+=arp[:-2*d]*0.35
L=pad+arpL+bass+kick+hat*0.8; R=pad+arpR+bass+kick+hat
env=np.minimum(1,t/2.0)*np.minimum(1,np.maximum(0,(DUR-t)/3.0))
L*=env; R*=env; m=max(abs(L).max(),abs(R).max()); L/=m*1.15; R/=m*1.15
out=(np.stack([L,R],1)*32767).astype(np.int16)
w=wave.open('music.wav','wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(out.tobytes()); w.close()
print('ok', DUR)
