# -*- coding: utf-8 -*-
"""一轮校准: 设检测器时间常数 -> 用我们的检测器电平反推曲线 -> 写回 -> 渲染 -> 评估
用法: python iter.py LO_ATT LO_REL MID_ATT MID_REL HI_ATT HI_REL LOOK LIM_ATT LIM_REL CEIL
"""
import json, re, subprocess, sys, os

CL = "D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
SDK = "C:/Program Files (x86)/Windows Kits/10"
MSVC = "D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
ENV = dict(os.environ, MSYS_NO_PATHCONV="1",
           INCLUDE=f"{SDK}/Include/10.0.26100.0/ucrt;{MSVC}/include;"
                   f"{SDK}/Include/10.0.26100.0/um;{SDK}/Include/10.0.26100.0/shared",
           LIB=f"{SDK}/Lib/10.0.26100.0/ucrt/x64;{MSVC}/lib/x64;{SDK}/Lib/10.0.26100.0/um/x64")
GRID = [-70, -56, -48, -42, -38, -34, -30, -25, -19, -12]

def build(tool):
    exe = tool.replace('.cpp', '.exe')
    r = subprocess.run([CL, "/nologo", "/utf-8", "/O2", "/EHsc", "/std:c++17", "/I.", tool, f"/Fe:{exe}"],
                       env=ENV, capture_output=True)
    if r.returncode:
        print(r.stdout.decode('utf-8', 'ignore')[-1500:]); sys.exit(1)

def setf(s, name, val):
    return re.sub(rf"({name}\s*=\s*)[-0-9.]+f", rf"\g<1>{val}f", s)

def setarr(s, name, vals, grid=False):
    pat = rf"static constexpr float {name}\[10\]\s*=\s*\{{[^}}]*\}}"
    if grid:
        body = ",".join(f"{v:.1f}f" for v in GRID)
    else:
        body = ",".join(f"{v:.1f}f" for v in vals)
    return re.sub(pat, f"static constexpr float {name}[10] = {{{body}}}", s)

def main():
    la, lr, ma, mr, ha, hr, look, latt, lrel, ceil = [float(v) for v in sys.argv[1:11]]
    modes = [int(v) for v in sys.argv[11:14]] if len(sys.argv) > 13 else [0,0,0]
    # 1) 写时间常数 (base)
    s = open("base_core.hpp", encoding='utf-8').read()
    s = setf(s, "LO_ATT", la);   s = setf(s, "LO_REL", lr)
    s = setf(s, "MID_ATT", ma);  s = setf(s, "MID_REL", mr)
    s = setf(s, "HI_ATT", ha);   s = setf(s, "HI_REL", hr)
    s = setf(s, "LIM_LOOK", look); s = setf(s, "LIM_ATT", latt)
    s = setf(s, "LIM_REL", lrel);  s = setf(s, "CEILING", ceil)
    s = re.sub(r"static constexpr int LO_MODE = \d+, MID_MODE = \d+, HI_MODE = \d+;",
               f"static constexpr int LO_MODE = {modes[0]}, MID_MODE = {modes[1]}, HI_MODE = {modes[2]};", s)
    open("base_core.hpp", 'w', encoding='utf-8', newline='').write(s)
    open("bassmax_core.hpp", 'w', encoding='utf-8', newline='').write(s)

    # 2) 用当前曲线导出电平
    build("dump_gain.cpp")
    subprocess.run(["./dump_gain.exe", "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav",
                    "gfull.csv", "0.85", "4.5", "0", "200"], capture_output=True)
    # 3) 反推曲线
    r = subprocess.run([sys.executable, "fit_bins.py"], capture_output=True)
    if r.returncode:
        print(r.stderr.decode('utf-8', 'ignore')[-1500:]); sys.exit(1)
    cur = json.load(open("curve.json"))
    s = open("base_core.hpp", encoding='utf-8').read()
    for b in ("LO", "MID", "HI"):
        s = setarr(s, f"{b}_X", None, grid=True)
        s = setarr(s, f"{b}_Y", cur[b])
    open("base_core.hpp", 'w', encoding='utf-8', newline='').write(s)
    open("bassmax_core.hpp", 'w', encoding='utf-8', newline='').write(s)
    print("曲线:", {k: [round(v, 1) for v in cur[k]] for k in cur})

    # 4) 渲染 + 评估
    build("render_wav.cpp")
    tag = f"it_{la}_{lr}_{ma}_{mr}_{ha}_{hr}_{modes[0]}{modes[1]}{modes[2]}"
    out = f"v_{tag}.wav"
    subprocess.run(["./render_wav.exe", "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav",
                    out, "0.85", "4.5", "0"], capture_output=True)
    print("渲染:", out)
    subprocess.run([sys.executable, "eval.py", out])
    subprocess.run([sys.executable, "dev_metric.py", out])
    build("dump_gain.cpp")
    subprocess.run(["./dump_gain.exe", "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav",
                    "gchk.csv", "0.85", "4.5", "0", "300"], capture_output=True)
    import numpy as np
    d = np.genfromtxt('gchk.csv', delimiter=',', names=True)
    lim = d['lim_db']
    print(f"  限幅: min={lim.min():.2f}dB <-2dB占{100*np.mean(lim<-2):.1f}% <-4dB占{100*np.mean(lim<-4):.2f}%")

if __name__ == '__main__':
    main()
