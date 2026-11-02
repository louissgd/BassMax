# -*- coding: utf-8 -*-
# 权威 RBJ 公式 + 解析频响，验证低架/高架是否符合预期。
import math

def coeffs(kind, f0, q, db, sr):
    A = 10.0**(db/40.0)          # RBJ: A = sqrt(10^(db/20))
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
    elif kind == 'highshelf':
        b0 =    A*((A+1) + (A-1)*cw + sq)
        b1 = -2*A*((A-1) + (A+1)*cw)
        b2 =    A*((A+1) + (A-1)*cw - sq)
        a0 =   (A+1) - (A-1)*cw + sq
        a1 =  2*((A-1) + (A+1)*cw)
        a2 =   (A+1) - (A-1)*cw - sq
    else:  # peaking
        b0 = 1 + alpha*A
        b1 = -2*cw
        b2 = 1 - alpha*A
        a0 = 1 + alpha/A
        a1 = -2*cw
        a2 = 1 - alpha/A
    return (b0/a0, b1/a0, b2/a0, a1/a0, a2/a0)

def resp_db(c, f, sr):
    b0,b1,b2,a1,a2 = c
    w = 2*math.pi*f/sr
    z1 = complex(math.cos(-w), math.sin(-w))
    z2 = z1*z1
    num = b0 + b1*z1 + b2*z2
    den = 1 + a1*z1 + a2*z2
    return 20*math.log10(abs(num/den)+1e-12)

def show(name, c, sr):
    print(name)
    for f in [100, 500, 1000, 2000, 5000, 10000, 20000]:
        print("   %7d Hz: %+.2f dB" % (f, resp_db(c, f, sr)))

SR=44100
print("== 校验 peaking -6dB@1000 Q1 (中心应下凹 ~-6) ==")
show("  peaking", coeffs('peaking',1000,1.0,-6,SR), SR)
print("== 低架 +6dB@1000 Q0.707 (期望 低~+6 高~0) ==")
show("  low-shelf", coeffs('lowshelf',1000,0.707,6,SR), SR)
print("== 高架 +6dB@1000 Q0.707 (期望 低~0 高~+6) ==")
show("  high-shelf", coeffs('highshelf',1000,0.707,6,SR), SR)
print("== 低架 +4dB@150 Q0.707 ==")
show("  low-shelf4", coeffs('lowshelf',150,0.707,4,SR), SR)
