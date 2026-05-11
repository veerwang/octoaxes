# Firmware 性能对比：octoaxes vs 老 Squid

**日期**: 2026-05-11
**测试脚本**: `software/tests/benchmark_xyz_speed.py`（commit a257d22）
**参数**: 通过 SET_MAX_VELOCITY_ACCELERATION 强制下发到两个 firmware，确保公平对比
- X/Y vmax=30 mm/s, accel=500 mm/s²
- Z vmax=3.8 mm/s, accel=20 mm/s²
- 微步=256, 电流 X/Y=1000mA / Z=500mA RMS
- home_pol X/Y=1, Z=0

## 三组数据

| 数据 | firmware | config 重点 |
|---|---|---|
| `benchmark_xyz_20260511_145604.{csv,md}` | octoaxes commit ef05554 | 原始 baseline，vmax X/Y=25, Z=3 |
| `benchmark_xyz_20260511_172413_octoaxes_vmax30.{csv,md}` | octoaxes commit 405efb7 | vmax 提到 30/30/3.8 |
| `benchmark_xyz_20260511_173427_oldsquid_fw.{csv,md}` | 老 Squid firmware | 同上参数下发 |

## 性能对比（mean ms，每方向 10 trial）

| 距离 | octoaxes baseline | octoaxes vmax30 | 老 Squid fw | (老 - octoaxes vmax30) |
|---|---|---|---|---|
| X 10μm | 123 | 123 | **126** | +3ms |
| X 100μm | 197 | 197 | **210** | +13ms |
| X 1mm | 366 | 366 | **380** | +14ms |
| X 5mm | 622 | 623 | **603** | **−20ms** |
| X 10mm | 825 | 825 | **749** | **−76ms** |
| X 30mm | 1593 | **1450** | **1376** | −74ms |
| Y 10μm | 123 | 123 | 126 | +3ms |
| Y 100μm | 197 | 197 | 210 | +13ms |
| Y 1mm | 366 | 366 | 380 | +14ms |
| Y 5mm | 621 | 623 | 603 | −20ms |
| Y 10mm | 823 | 825 | 750 | −75ms |
| Y 30mm | 1593 | 1450 | 1376 | −74ms |
| Z 10μm | 188 | 188 | 199 | +11ms |
| Z 100μm | 348 | 348 | 357 | +9ms |
| Z 1mm | 697 | 697 | 708 | +11ms |

## 关键观察

### 1. octoaxes VMAX 25→30 优化收益（baseline vs vmax30）

- 仅 30mm 档位减 143ms (9%)，其他档位 < 1% 变化
- 原因：大距离 cruise 段受益于 vmax，小距离 ramp 主导不变

### 2. octoaxes vs 老 Squid firmware（同参数下）

| 距离段 | 谁快 | 平均差异 |
|---|---|---|
| 小距离 10μm–1mm | **octoaxes 快** | 3–14ms (3–7%) |
| 大距离 5mm–30mm | **老 Squid 快** | 20–76ms (3–9%) |
| Z 全档位 | octoaxes 略快 | 9–11ms (3–6%) |

### 3. 老 Squid 大距离优势来源（推测）

老 Squid 30mm 1376ms vs octoaxes 1450ms，差 74ms 都在 ramp 段：

- octoaxes BOW 参数（S-ramp jerk 限制）可能比老 Squid 更保守 → 加减速段更长
- ASTART=0 / DFINAL=0 让 ramp 从静止线性起
- DMAX = AMAX 对称，老 Squid 可能不对称

要复刻老 Squid 大距离性能需对比 chip 寄存器（BOW1-4, ASTART, DFINAL, DMAX）。

## 结论

**两个 firmware 在同等参数下性能相当**，octoaxes 没有明显落后。大距离差 5-10% 是 ramp 曲线细节差异，不是协议或电流问题。

如继续优化 octoaxes：
- 大距离（5mm-30mm）：对比 chip 寄存器，调 BOW/ASTART/DFINAL 逼近老 Squid
- 小距离（≤1mm）：已经更快，不需要进一步优化
