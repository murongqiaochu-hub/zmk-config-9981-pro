# Xbox 布局标准 HID：v2.3.0 行为保持型重构候选（2026-10-21）

- 候选分支 `codex/firmware-refactor-v2.3.0-20261021`，重构源码 `5c82f7ac3a4fa0acd5d4e2ae504a0fc5f5659fde`；行为基线v2.2.9 `f7412ec90c925487617a1e8d40dd886c310142a6`，基线源归档与完整host日志已保留。
- 当前专项/机械证明、68万事件旧新差分、524288组PWM对照和6组合独立TU链接已PASS；当前完整host exit0、80配置/四层图同步PASS；Actions ARM、UF2与导出待完成。CMake实际矩阵本地缺工具，CI强制执行；不把旧v2.2.9 ARM/UF2冒作重构结果。
- [全局架构](<../docs/firmware_architecture.md>)列出模块职责、保留的细节、验证口径与回退。四层键图、时序、保存格式、协议、GPIO/矩阵、SDK门与原限制均保持；不自动刷写/清配对/settings，不发布/归档或推稳定分支。

## 历史候选：v2.2.9 UPPER 快捷键（2026-10-21）

- 当前候选分支：`codex/xbox-layout-v2.2.9-20261021`；功能源码提交 `ec6e492ca452b19cf3fa1ec35e7a72f0774fc43e`，构建提交 `bd66b98890f56efaec2bcd7a0361236afd4688aa`。五项灯控向左移一物理键：X/P29键盘RGB_BRD、C/P30触板BL_DEC、V/P31键盘RGB_TOG、B/P32触板BL_INC、N/P33键盘RGB_BRI；M/P34在右aA按住时双击切鼠标翻转，F/P20在UPPER无输出。无其他行为变化。
- v2.2.9完整host exit0、四层42键图/manifest同步、Actions Run 39563315080 regression/ARM和27页A5 PDF/DOCX均PASS；UF2边界/身份校验PASS，SHA `8a3506d9ad47b0ca033fea07f7e2bf8724d1e4ac434b2a789a2826ff859e907e`（613376B/1198块，family `0xada52840`，`[0x1000,0x4be00)`）。[UF2](</vol1/1000/projects/output/1021/152518_9981Pro灯控键位_v2.2.9/09_云端构建产物/bbp9981-zmk.uf2>)、[校验JSON](</vol1/1000/projects/output/1021/152518_9981Pro灯控键位_v2.2.9/09_云端构建产物/validation.json>)；未刷写、未实机验收、未发布/归档。不得把v2.2.8历史构建证据套用到本版。

## 历史候选：v2.2.8 固定方向档 / R/U 菜单（2026-10-21）

- 功能源码：`88edf7f323b643465fa1470fdc2e090dd3ee0ffa`；构建提交：`ffc41698f6972f3a26cc4ce459956a06fe8c3a46`；候选分支 `codex/xbox-layout-v2.2.8-20261021`。完整host exit0、70配置/四层42键图/manifest同步PASS；[Actions Run39557844223](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39557844223>) 的regression、bbp9981 ARM和合并artifact均成功，deliver job按候选条件跳过。未发布/归档，未刷写/实机验收。
- 新UF2本地边界验证PASS：SHA `a7753c5e242f5e42a95d183f2802a8d189abc9f0311aedac8a690a94236e6593`，613376 B/1198块/family `0xada52840`/连续应用区`[0x1000,0x4be00)`。[候选UF2](</vol1/1000/agent/output/1021/143516_9981Pro固定方向_v2.2.8/09_云端构建产物/bbp9981-zmk.uf2>)、[校验记录](</vol1/1000/agent/output/1021/143516_9981Pro固定方向_v2.2.8/09_云端构建产物/validation.json>)、[SHA256SUMS](</vol1/1000/agent/output/1021/143516_9981Pro固定方向_v2.2.8/09_云端构建产物/SHA256SUMS>)；device_tested=false。
- 云端最终DTS逐项核验42个MEDIA位置：View R/P10、Menu U/P13，T/Y None；与v2.2.7相比仅MEDIA P10–P13四位置变化，其余三层全部绑定一致。见[DTS核对JSON](</vol1/1000/agent/output/1021/143516_9981Pro固定方向_v2.2.8/11_云端DTS键位核对.json>)；原始日志确认固定档/手势对象实际编译，FLASH306576B/RAM88862B。
- 主任务回传定向strict UBSan PASS：全部旧档位绝对选档/幂等/互切、两种listener顺序真实A320双档四向/aA临时覆盖且不触BT、真实preset API全向量/饱和；scope gate PASS（45旧配置/8集成+16新增）。完整host和ARM现已另行通过；这些结果仍不代替实机验收。
- 用户于 **2026-10-21 14:33** 确认 v2.2.7 蓝牙按钮已恢复、触板三击已生效、手柄基本可用；要求方向改为固定档而非累加，并将MEDIA菜单移到R/U。该反馈只覆盖所述现象，不是USB/BT、全部App、多键及保护场景全面硬件验收；旧校验记录的 `device_tested=false` 保留原状。
- 固定方向档只在 **aA松开且锁轮生效** 时选择：P3黑莓完整三击直接选 **左→上、右→下、上→右、下→左**；P5顶部返回完整三击直接选 **右→上、左→下、上→左、下→右**。同键重复幂等，无论原档位都直接选目标；换另一键直接选其目标，不继续转90°。默认锁轮0°、解锁/重锁回正；按住aA临时原方向与原生肩轮，松开恢复所选固定档，肩键半速/屏蔽自身Wheel不变。
- 按住右aA时P3/P5立即 `BT_SEL 1/2`，已锁轮亦优先选槽；aA+F/P20双击鼠标翻转不改。三击按住≤180ms、释放至下次按下≤220ms，其他键取消未完成序列，其余点按吞掉；aA锁轮双击每拍<450ms、间隔≤350ms不变。
- 仅MEDIA：View从T/P11移到 **R/P10（action12）**，Menu从Y/P12移到 **U/P13（action13）**，原T/Y留空。其余保持v2.2.7：W↑D→Z↓A←，S/L=L3/R3，肩LT/RT、Call/End LB/RB，O/K/BSPC/$=Y/X/B/A，P4/Space=Guide，19绑定/2出口/21None，仅P40/P41退出。aA层owner仲裁、鼠标/手柄隔离、协议和安全限制均保留。
- 用户已授权今后同范围固件修正直接完成必要的版本专属新候选分支推送、既有Actions ARM构建、下载及UF2校验，不再重复询问；仍须核验精确仓库，不推稳定/已发布分支，不发布、归档回推、刷写、清配对、擦设置或改主机。v2.2.4仍为最后已发布/归档版本。

## 历史构建：v2.2.7（不含本次固定档与R/U菜单）

- 功能源码：`88e744f51e386db853ae25c4438b33cb23557af5`；构建提交：`045a7a3a0da9ba372d99ea747e27a15ababb8694`，分支 `codex/xbox-layout-v2.2.7-20261021`。完整主机回归exit0（70配置/四层42键图与手册同步）；[Actions Run 39551522029](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39551522029>) 的regression与`bbp9981` ARM构建均PASS，候选deliver job跳过。v2.2.7未发布/归档；交付时记录未刷写/未实机验收，用户14:33后续反馈见上方，不能外推全场景通过。
- 新UF2本地边界验证PASS：SHA-256 `140d608be34b35c237fcc11db8c8bd56c5b55c453c9cae921b2c753750887bc6`，613376 bytes、1198 blocks、family `0xada52840`、连续应用区 `[0x1000,0x4be00)`，无bootloader/NVS块。[候选UF2](</vol1/1000/agent/output/1021/133611_9981Pro修正版_v2.2.7/09_云端构建产物/bbp9981-zmk.uf2>)、[SHA256SUMS](</vol1/1000/agent/output/1021/133611_9981Pro修正版_v2.2.7/09_云端构建产物/SHA256SUMS>)、[validation.json](</vol1/1000/agent/output/1021/133611_9981Pro修正版_v2.2.7/09_云端构建产物/validation.json>)；`device_tested=false`。
- 云端最终DTS的42个MEDIA位置及DEFAULT/SYM/UPPER右aA、P3/P5/F绑定已独立核对，见[逐位置核对JSON](</vol1/1000/agent/output/1021/133611_9981Pro修正版_v2.2.7/11_云端DTS键位核对.json>)；日志确认新AA owner实际编译，FLASH306592B/RAM88862B，仍非设备验收。
- 最终MEDIA：W/P8↑、D/P19→、Z/P28↓、A/P17←；S/P18=L3、L/P25=R3；肩P0/P1=LT/RT，Call/End P2/P6=LB/RB。O/K/BSPC/$=Y/X/B/A，P4/Space=Guide，T/Y=View/Menu不变；E/X/F新空，19绑定/2出口/21None，仅P40/P41退出。
- 恢复蓝牙槽：按住右aA时P3/P5立即选槽1/2，即使scroll latch已锁定；松aA且锁轮时P3/P5三击分别左→上/右→上，其他点按吞掉。鼠标翻转改到右aA按住+F/P20双击（180/220ms），再次切回；普通Y/DEFAULT肩键互换，其余与volatile限制不变。aA双击450/350ms、三击180/220ms保留。
- aA层由momentary/sticky/fixed owner仲裁；旧sticky的3秒超时/迟到UP不撤销fixed，SYM退层不解scroll latch。pinned sticky/mo/to三份Git blob身份、owner12case旧bug/新修复、真实A320/owner/gesture/rotation两种listener顺序联动（超3秒、切槽双松手顺序、解锁重锁回正）、gamepad专项与scope gate PASS（45旧配置/8集成+16新增）；完整回归现已通过，但这些模型/构建结果仍不替代硬件验收。
- 用户此前确认刷入 **v2.2.6**、未在Studio保存，反馈映射、手势与第三蓝牙槽入口异常。该旧版实际默认图不是v2.2.7图；v2.2.7的14:33有限正面反馈见本页开头，不追改v2.2.6/v2.2.7旧校验记录，也不将host/ARM PASS改写为全面硬件通过。

## 历史构建：v2.2.6（不含本次修正）

> **2026-10-21 v2.2.6 未发布候选曾完成 Actions ARM 构建与 UF2 本地校验。** 功能源码 `7a956e9b1a7fb2dc465bfdb9212cdf7f27977e2b`；构建提交 `b05dee65dc929855c809036c539b14488cd7d4b0`，分支 `codex/xbox-layout-v2.2.6-20261021`；[Actions Run 39540020475](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39540020475>) 的 host regression 与 `bbp9981` ARM 构建 PASS，候选归档 job 跳过。UF2 SHA-256 `9f4daf7b22ab833c01ed5322115b1a8977cbd4aff8701c04e29ddd73b76d155a`，611328 bytes / 1194 blocks / family `0xada52840` / 地址 `[0x1000, 0x4ba00)`；[本地 UF2](</vol1/1000/agent/output/1021/105843_9981ProXbox固件_v2.2.6/09_云端构建产物/bbp9981-zmk.uf2>) 与[校验记录](</vol1/1000/agent/output/1021/105843_9981ProXbox固件_v2.2.6/09_云端构建产物/validation.json>)。当时只推新候选分支、未归档至发布分支；交付记录的`device_tested=false`是当时状态。用户后续确认已刷v2.2.6、未Studio保存并反馈失败，不将该反馈改为验收通过；无代理刷写、清配对或擦设置。v2.2.4仍为最后已发布/归档版本。

### v2.2.6 历史实现范围（非 v2.2.7 目标）

- MEDIA：P0/P1=LB/RB，P2/P6=LT/RT，A/P17=D-pad 左，O/K/Backspace/$=Y/X/B/A，D/L=L3/R3；只留 SYM/P40 与右 aA/P41 两个出口。
- 锁轮：锁定且未按 aA 时，P3 三击左→上、P5 三击右→上；锁定期间 P3 永不选槽1，P5 非三击吞掉。按住右 aA 时 P5 双击切换 volatile 鼠标模式，不发槽2。
- 鼠标模式：普通指针 Y 反向；DEFAULT 肩键 LCLK/RCLK 角色互换；X、滚轮、手柄及 SYM/UPPER/MEDIA 键位/肩键功能不变，重启关闭。
- 完整 `python3 -B tests/run_host_tests.py` exit 0；scope gate 保持 45 份旧配置逐字节相同。Actions Run 39540020475 的 `regression` 与 `bbp9981` ARM build PASS；候选分支 `deliver` job 按分支条件跳过，UF2 只在本次输出目录下载并本地校验，未归档。

> **历史候选 v2.2.5（未发布 / 未归档）。** 功能源码 `3c5235b21f8452630850053620635d4aea7cb47d`；构建提交 `dcfabda69c9ed81a7fb92aaf0df29e74566be763`，分支 `codex/xbox-layout-v2.2.5-20261021`；[GitHub Actions Run 39520450915](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39520450915>) 的 host regressions 与 `bbp9981` ARM 构建 PASS，候选归档 job 跳过。UF2 SHA-256 `6b562773e714d564da1b4351db185e63adeefbf3ad6f2139be9311249fde5b5f`，608256 bytes / 1188 blocks / family `0xada52840` / 地址 `[0x1000, 0x4b400)`；未刷写，v2.2.4 仍为最后已发布 / 归档版本。

# Xbox 布局标准 HID：v2.2.4 最后已发布 / 归档交付记录

## 最后已发布 / 归档版本 v2.2.4

- 硬件目标：9981 Pro / P9981，`bbp9981`，nRF52840；不是 Air。
- 协议：Xbox 布局标准 HID，**不是原生 XInput**。
- 功能源码提交：`9f41197bbafea13eab61ce926378dc13c8bacc69`；用户确认的 E/X/S/F=D-pad、D/K=L3/R3、触板点击与空格=Guide，并含 aA 锁轮旋转/肩键半速。
- 构建源提交：`3343a5005eb97b34728822db0422b1a7c5e178c5`；相对功能源码仅追加同步说明书/记录，`config/` 无差异。
- [GitHub Actions Run 39428313318](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39428313318>)：主机回归、ARM 构建、UF2 边界验证及归档全部成功。
- 目标分支：`codex/xbox-layout-20261020`；Actions 归档提交：`89311aa0d70e7219d8b51b07a9f14bbd683d1c03`。
- [v2.2.4 UF2](<3343a5005eb97b34728822db0422b1a7c5e178c5/bbp9981-zmk.uf2>)；[SHA256SUMS](<3343a5005eb97b34728822db0422b1a7c5e178c5/SHA256SUMS>)；[云端验证记录](<3343a5005eb97b34728822db0422b1a7c5e178c5/validation.json>)。

### UF2 独立校验摘要

```text
SHA-256  dc7027b0706d299e015c6d7646fa13ed7347bb32865d33d164296e8e386a8521
UF2 bytes 607232
Blocks    1186
Family    0xada52840 (nRF52840)
Addresses [0x1000, 0x4b200) — contiguous application blocks only
Source run 39428313318; device_tested=false
```

本地重新运行 `tools/verify_firmware.py` 的 UF2 校验函数并对照云端 `validation.json`、`SHA256SUMS`，长度、块数、MCU family、起止地址与 SHA-256 一致；地址位于应用区间 `0x1000–0xD4000`，不含 bootloader / NVS 块。构建日志使用 ZMK `build-user-config.yml@v0.3.0`，target 为 `bbp9981`。

**未刷写、未 USB / BT 真实枚举、未目标 App 实玩。** 不把主机模拟或 ARM 编译成功当成真实 IRQ / RTOS 行为、触板手感、矩阵鬼键或应用兼容验收；没有清配对、擦设置或修改主机配置。是否刷写由用户后续决定。

## 更早的历史 ARM 构建（仅历史，键图不同）

以下旧 UF2 对应 **S=↓、Z/K 未分配**，不可用于 v2.2.4：

- 实际 ARM 构建源：`1c61b817f7e2d52fd2bbea888d1867f354e4c2a8`。
- [GitHub Actions 39361865259](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39361865259>)：主机回归、ARM 构建、UF2 边界验证及归档全部成功。
- 首次归档提交：`f377ce5733c8193cc1afd6695ea3b0a25920d16a`。
- [原始 UF2](<1c61b817f7e2d52fd2bbea888d1867f354e4c2a8/bbp9981-zmk.uf2>)；SHA-256 `3b0b5124cd740b63889bcaaf720dfa532ee5cf1ad6574685a4ffbb767e02289c`；604672 bytes、1181 blocks、family `0xada52840`，地址 `[0x1000, 0x4ad00)`。
- 该旧 payload 的 141-byte Xbox HID 描述符与旧版生产源码一致；旧构建 FLASH 302168 B / 844 KB、RAM 88694 B / 256 KB。它只说明旧版，不能替代 v2.2.4 的构建/验证。
