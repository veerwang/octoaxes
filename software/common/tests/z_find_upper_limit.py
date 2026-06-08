#!/usr/bin/env python3
"""测新 Z 上限位（行程远端）(2026-06-08)

home(干净 0) → 放宽软下限(防 clamp 干扰) → 从 Z=0 往行程方向(firmware 负向 =
GUI 正向)小步进，找上限位：
  - STOPR 上限开关触发(STATUS bit8) → 干净
  - 否则编码器堵转：到机械端 ENC 不跟随 XACTUAL(Δenc << Δxact)
安全：0.5mm/步、堵转/限位即停、上限 40mm 兜底。
"""
import sys, time, serial, re
sys.path.insert(0, "software/common/tests")
import z_homing_safedist as z

PORT="/dev/ttyACM0"; STEP=25600; SPMM=51200.0; CAP_MM=40.0
STALL=0.5; STOPR=1<<8
ser=serial.Serial(PORT,2000000,timeout=0.05); time.sleep(0.4); ser.reset_input_buffer()
seq=[0]
def cmd(c,b2=0,b3=0,b4=0,b5=0,b6=0):
    seq[0]=(seq[0]+1)&0xFF; z.send_cmd(ser,seq[0],c,b2,b3,b4,b5,b6)
def dz():
    z.send_debug_cmd(ser,"S:DUMPREGS Z")
    t=time.perf_counter(); b=bytearray()
    while time.perf_counter()-t<1.5:
        if ser.in_waiting: b.extend(ser.read(ser.in_waiting))
        if b"S:DUMPREGS:END" in b: break
        time.sleep(0.01)
    s=b.decode("latin1","ignore"); g=lambda p:(int(re.search(p,s).group(1)) if re.search(p,s) else None)
    st=re.search(r"STATUS=0x([0-9A-Fa-f]+)",s)
    return g(r"XACTUAL=(-?\d+)"),g(r"VACTUAL=(-?\d+)"),(int(st.group(1),16) if st else 0)
def enc():
    z.send_debug_cmd(ser,"S:ENCPOS")
    t=time.perf_counter(); b=bytearray()
    while time.perf_counter()-t<1.5:
        if ser.in_waiting: b.extend(ser.read(ser.in_waiting))
        if b"S:ENCPOS:END" in b: break
        time.sleep(0.01)
    m=re.search(r"S:ENCPOS:Z:enc=(-?\d+) xactual=(-?\d+)",b.decode("latin1","ignore"))
    return (int(m.group(1)),int(m.group(2))) if m else (None,None)
def setlim(code,val):
    v=val&0xFFFFFFFF
    cmd(9,code,(v>>24)&0xFF,(v>>16)&0xFF,(v>>8)&0xFF,v&0xFF)  # SET_LIM=9
def moveto(target):
    v=target&0xFFFFFFFF
    cmd(8,(v>>24)&0xFF,(v>>16)&0xFF,(v>>8)&0xFF,v&0xFF)
    time.sleep(0.2); t=time.perf_counter()
    while time.perf_counter()-t<6:
        x,va,_=dz()
        if va==0:
            time.sleep(0.05); x,va,_=dz()
            if va==0: return
        time.sleep(0.03)

# 1) home
print("[1] HOME Z（可能较慢，当前离 home 较远）...")
cmd(5,b2=2,b3=0)
t0=time.perf_counter(); moved=False
while time.perf_counter()-t0<30:
    x,va,_=dz()
    if va and abs(va)>1000: moved=True
    if moved and va==0 and x is not None and abs(x)<8000:
        time.sleep(0.3); x,va,_=dz()
        if va==0: break
    time.sleep(0.25)
e,x=enc(); print(f"    home 完成: XACTUAL={x} enc={e}")

# 2) 放宽软下限（防 clamp）；上限保留
print("[2] 放宽软下限 SET_LIM Z_NEG=-5,000,000")
setlim(5,-5000000); time.sleep(0.2)
# 2b) 降速到 2mm/s（撞机械端更轻柔）SET_MAX_VELOCITY_ACCELERATION=22
vel=200; acc=200  # vel*100=2mm/s, acc*10=20mm/s^2
cmd(22, b2=2, b3=(vel>>8)&0xFF, b4=vel&0xFF, b5=(acc>>8)&0xFF, b6=acc&0xFF)
time.sleep(0.2); print("[2b] 搜索降速 2mm/s")

# 3) 步进找上限位
print("\n[3] 往行程方向(负向)步进，0.5mm/步：")
print(f"{'步':>3} {'XACTUAL':>10} {'enc':>10} {'GUI_um':>9} {'Δenc':>8} {'STOPR':>6}")
pe,px=enc(); target=px if px is not None else 0
hit=None; reason=""
nmax=int(CAP_MM*SPMM/STEP)
for i in range(1,nmax+1):
    target-=STEP; moveto(target)
    e,x=enc(); _,_,st=dz()
    de=e-pe; gui=-x/SPMM*1000; sr=bool(st&STOPR)
    print(f"{i:>3} {x:>10} {e:>10} {gui:>9.1f} {de:>8} {'ON' if sr else '-':>6}")
    if sr: hit=x; reason="STOPR 上限开关"; break
    if abs(de)<STALL*STEP: hit=x; reason="编码器堵转(机械端)"; break
    pe=e
if hit is not None:
    print(f"\n[4] 检测到上限位（{reason}）。退回 1.5mm 保安全 ...")
    moveto(hit+STEP*3);
e,x=enc(); print(f"    退回后 XACTUAL={x} enc={e}")
ser.close()
if hit is not None:
    upper=round(-hit/SPMM*1000)
    print("\n================ 结果 ================")
    print(f"  触发: {reason}")
    print(f"  上限位 XACTUAL = {hit} 微步 = {hit/SPMM:.2f} mm(firmware)")
    print(f"  → GUI 行程上限 ≈ {upper} um ({upper/1000:.2f} mm)")
    print(f"  建议 limits = (-10, {upper})")
    print("======================================")
else:
    print(f"\n{CAP_MM}mm 内未触发上限/堵转。")
