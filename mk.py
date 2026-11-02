# -*- coding: utf-8 -*-
"""从 base_core.hpp 生成实验用 bassmax_core.hpp: 改限幅器常量 + 各段曲线整体平移
用法: python mk.py LOOK ATT REL CEIL dLO dMID dHI [dLO_slope dMID_slope dHI_slope]
"""
import re, sys, os, subprocess

CL = "D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe"
SDK = "C:/Program Files (x86)/Windows Kits/10"
MSVC = "D:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231"
ENV = dict(os.environ, MSYS_NO_PATHCONV="1",
           INCLUDE=f"{SDK}/Include/10.0.26100.0/ucrt;{MSVC}/include;"
                   f"{SDK}/Include/10.0.26100.0/um;{SDK}/Include/10.0.26100.0/shared",
           LIB=f"{SDK}/Lib/10.0.26100.0/ucrt/x64;{MSVC}/lib/x64;{SDK}/Lib/10.0.26100.0/um/x64")

def consts(s, name, val):
    return re.sub(rf"({name}\s*=\s*)[-0-9.]+f", rf"\g<1>{val}f", s)

def shift(s, band, delta):
    m = re.search(rf"static constexpr float {band}_Y\[10\]\s*=\s*\{{([^}}]*)\}}", s)
    vals = [float(v.strip().rstrip('f')) for v in m.group(1).split(',') if v.strip()]
    return s[:m.start(1)] + ",".join(f"{v+delta:.1f}f" for v in vals) + s[m.end(1):]

def main():
    look, att, rel, ceil, dlo, dmid, dhi = sys.argv[1:8]
    s = open("base_core.hpp", encoding='utf-8').read()
    s = consts(s, "LIM_LOOK", look); s = consts(s, "LIM_ATT", att)
    s = consts(s, "LIM_REL", rel);  s = consts(s, "CEILING", ceil)
    s = shift(s, "LO", float(dlo)); s = shift(s, "MID", float(dmid)); s = shift(s, "HI", float(dhi))
    open("bassmax_core.hpp", 'w', encoding='utf-8', newline='').write(s)
    r = subprocess.run([CL, "/nologo", "/utf-8", "/O2", "/EHsc", "/std:c++17", "/I.", "render_wav.cpp",
                        "/Fe:render_wav.exe"], env=ENV, capture_output=True)
    if r.returncode:
        print(r.stdout.decode('utf-8', 'ignore')[-1500:]); sys.exit(1)
    tag = f"L{look}A{att}R{rel}C{ceil}g{dlo}_{dmid}_{dhi}"
    out = f"v_{tag}.wav"
    subprocess.run(["./render_wav.exe", "Zac Efron & Zendaya - Rewrite The Stars (Movie Version).THD(原始).wav",
                    out, "0.85", "4.5", "0"], capture_output=True)
    print(out)

if __name__ == '__main__':
    main()
