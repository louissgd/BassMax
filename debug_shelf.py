# -*- coding: utf-8 -*-
# 打印低架系数与极点/零点，定位公式错误。
import math, cmath

def coeffs(kind, f0, q, db, sr):
    A = 10.0**(db/40.0)
    w0 = 2*math.pi*f0/sr
    cw = math.cos(w0); sw = math.sin(w0)
    alpha = sw/(2.0*q)
    sq = 2.0*math.sqrt(A)*alpha
    if kind == 'lowshelf':
        b0 =    A*((A+1) - (A-1)*cw + sq)
        b1 = -2*A*((A-1) - (A+1)*cw)
        b2 =    A*((A+1) - (A-1)*cw - sq)
        a0 =   (A+1) + (A-1)*cw + sq
        a1 = -2*((A+1) + (A-1)*cw)
        a2 =   (A+1) + (A-1)*cw - sq
    b0/=a0; b1/=a0; b2/=a0; a1/=a0; a2/=a0
    return (b0,b1,b2,a1,a2)

SR=44100
c = coeffs('lowshelf',1000,0.707,6,SR)
print("系数 b0,b1,b2,a1,a2 =", c)
# 零点: b0 + b1 z + b2 z^2 = 0 -> z^2 + (b1/b0) z + (b2/b0) = 0
zb = [complex(r) for r in 
      [(-c[1]/c[0] + cmath.sqrt((c[1]/c[0])**2 - 4*c[2]/c[0]))/2,
       (-c[1]/c[0] - cmath.sqrt((c[1]/c[0])**2 - 4*c[2]/c[0]))/2]]
za = [complex(r) for r in 
      [(-c[3] + cmath.sqrt(c[3]**2 - 4*c[4]))/2,
       (-c[3] - cmath.sqrt(c[3]**2 - 4*c[4]))/2]]
print("零点 z =", zb)
print("极点 z =", za)
# DC 增益
Hdc = (c[0]+c[1]+c[2])/(1+c[3]+c[4])
print("DC 增益(数值) =", Hdc, "-> %.2f dB" % (20*math.log10(abs(Hdc)+1e-12)))
HNq = (c[0]-c[1]+c[2])/(1-c[3]+c[4])
print("Nyquist 增益 =", HNq, "-> %.2f dB" % (20*math.log10(abs(HNq)+1e-12)))
print("A =", 10.0**(6/40.0), " A^2 =", 10.0**(6/20.0))
