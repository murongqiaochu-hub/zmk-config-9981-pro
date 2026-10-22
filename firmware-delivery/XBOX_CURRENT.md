# Xbox 布局标准 HID：v2.3.3 五档记忆/MEDIA慢呼吸候选（2026-10-22）

- 用户已刷v2.3.2并反馈“有提升，但是提升不大”；坏C-to-C线已由用户确认。随后明确要求Z循环调速、重启保留和MEDIA键盘慢呼吸，选择五档0.6/0.9/1.2/1.5/1.8、首次1.2、物理左Ctrl半速。不把有限改善或本轮软件结果称实机手感彻底解决。
- 功能源 `048201f8caeedeb729df3ca22aa2186c50aea476`，候选分支 `codex/cursor-presets-v2.3.3-20261022`，基线 `11218ad83292f5ccf9105e41010c1062073e474d`。仅UPPER Z/P28变正常可映射behavior；独立两字节设置key、1秒合并保存、最多3失败、用户优先、frame latch避免跨gain旧tap/replay。MEDIA键盘仅改约3.6秒10→100→10%呼吸，其他层与触板灯保留；非硬件DPI。见[本轮说明](<../docs/cursor_preferences.md>)。
- 父侧新增专项strict UBSan/非PIE ASan已通过：actual service/behavior四组合、五档全signed8 XY小数、actual A320直发/microtap/raw shadow、I2C中途1/5次cycle、旧轮/RAW游戏杆隔离、MEDIA ON+三OFF灯oracle；fresh-gain旁路实际编译后被负例拒绝。初轮仅新测试误用旧idle无报告帮助器，在预期RAW replay的130ms改为普通poll后复验PASS，旧oracle未动。独立只读审查无未解决P1/P2；历史重构机械证明/差分PASS。
- 范围：73/81旧配置原字节不变，8受控修改+4新文件；明确历史投影精确还原v2.3.2后再进原BASE，所有旧C oracle/SDK fixture冻结。DTS先于Kconfig，node/Z无条件，adapter始终编译且OFF纯OPAQUE、不链接速度service；独立OFF八组合/nm/ASan通过。OFF输入等效原none，非DTS字节相同；SDK首次注册行为可能单独存内部ID。首次Run39740546545虽ARM成功但云DTS未绑定Z，已隔离禁止刷写，后续必须重新构建并验实际node/Z/其余167键。
- 完整host exit0，85配置/四层168绑定与手册同步通过；27页A5 PDF/离线HTML/可编辑Word和四图同批导出，DOM最小页脚间距9px、PDF正文和Word4图/原生文本结构通过；Word未独立Office渲染。ARM/UF2与云端DTS/Kconfig核验待本轮Actions，不拿旧件冒充。候选未刷/未实机视觉/断电记忆验收，device_tested=false；不发布/归档、不推稳定、不刷写、清配对/settings或改主机，不以600KB删功能。
- 精确回退：[已刷v2.3.2原UF2](</vol1/1000/agent/output/1022/124037_9981Pro蓝牙指针稳态修正_v2.3.2/09_云端构建产物/bbp9981-zmk.uf2>)，SHA `dbfc480a7da14ec8932b92083a60aea13986f98847320ba854577b838eda940c`。恢复固定1.2/0.6、UPPER Z无输出和MEDIA快闪；不擦新独立速度记录。下方旧记录是当时交付状态，不追改旧device_tested。

## 历史：v2.3.2 普通鼠标修正候选（2026-10-22）

- 用户已刷v2.3.1，反馈蓝牙普通鼠标小幅往返抖动/速度不匀，明确授权防抖、输出突发与旧削峰一起修，不以关轻拍为前提；此前封版决定被本次明确授权取代，不删功能、不以600KB为指标。
- 功能源码 `1124252fcd43375a27b826224747121ba78e06a2`，候选分支 `codex/pointer-steady-v2.3.2-20261022`。普通cursor增加一计数滞回+四慢帧削峰；BLE仅首发前合并既有积压中≤10ms同向共线纯XY，不加等候且不跨按钮/零/轮/peer等屏障。4份配置仅feature追加+1纯核，其他76份原字节不变；RAW轻拍资格/键位/滚轮/手柄/灯/设置/USB修复保留。
- strict UBSan/ASan新专项和独立复核已通过；修掉故障后旧候选回放重新污染削峰历史的边界。完整host exit0、81份配置/四层42键图与手册同步通过；本地CMake缺失仍SKIP，既有CI将实际跑32组合/9负例。[Actions Run39725910232](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39725910232>) regression、bbp9981 ARM和artifact合并均成功，deliver/failure_context按候选条件跳过；构建 `3d7078e65bbc1e0c30cb4a8da272c1f578aaf343` 与功能源码config完全一致。手册v2.3.2同批同步，范围与手感取舍见[修正说明](<../docs/pointer_steady.md>)。
- [本轮UF2](</vol1/1000/agent/output/1022/124037_9981Pro蓝牙指针稳态修正_v2.3.2/09_云端构建产物/bbp9981-zmk.uf2>)：615424B（601KiB）/1202块，family `0xada52840`，连续应用区 `[0x1000,0x4c200)`、无bootloader/NVS块；SHA `dbfc480a7da14ec8932b92083a60aea13986f98847320ba854577b838eda940c`。见[UF2校验](</vol1/1000/agent/output/1022/124037_9981Pro蓝牙指针稳态修正_v2.3.2/09_云端构建产物/validation.json>)、[Actions身份](</vol1/1000/agent/output/1022/124037_9981Pro蓝牙指针稳态修正_v2.3.2/17_Actions运行记录.json>)、[云日志](</vol1/1000/agent/output/1022/124037_9981Pro蓝牙指针稳态修正_v2.3.2/18_Actions完整日志.zip>)。
- 最终DTS全文与v2.3.1逐字节相同（去日志时间），1166项旧Kconfig不变，唯一新增 `CONFIG_A320_CURSOR_STEADY=y`；FLASH307692B、RAM89102B，较v2.3.1增加924B/224B，不删功能压体积。见[云端核验](</vol1/1000/agent/output/1022/124037_9981Pro蓝牙指针稳态修正_v2.3.2/14_云端基线一致性核验.json>)。手册27页A5 PDF/四图可编辑DOCX/离线HTML同批导出；Word只做结构/文本验证，未独立Office渲染。
- 新候选尚未刷写/未实机手感验收；系统加速度仍影响屏幕速度，不保证抖动已消除/绝对线性。不刷写、清配对/settings、改主机、推稳定分支或发布/自动归档；不安排多日专项长测。
- 精确回退：[原v2.3.1 UF2](</vol1/1000/agent/output/1022/104251_9981Pro最终封版_v2.3.1/09_最终固件/9981Pro_v2.3.1_封版.uf2>)，SHA `ab9c0c10cb46e5c957ae4f55ecd71dac0817e345353e7f7777776a2abf66f50a`，保留当前键位/手柄/灯与USB修复。下方“未刷写”等为对应历史交付时状态，不否认用户后续已刷及本次反馈。

## 历史：v2.3.1研发封版（2026-10-22，后由本次明确修正取代）

- **最终只推荐v2.3.1现有二进制，不再继续改键/调参/重构，不以600KB为指标。** 本次封版只增加测试/文档，80份配置全部与实际构建d607df8相同，不重复ARM编译。手册v2.3.1.1只是文档修订；研发封版不是稳定发布或实机认证。
- [最终单一UF2](</vol1/1000/agent/output/1022/104251_9981Pro最终封版_v2.3.1/09_最终固件/9981Pro_v2.3.1_封版.uf2>)与下方原v2.3.1逐字节相同，SHA不变；已刷相同SHA不需再刷。边界与来源仍是Run39614649971及下方完整记录。
- 两名独立只读审查与父侧固定SDK/HAL核查未找到新增封版阻断。补充4500周期恢复边界只证明后续stock报告实际完成可恢复许可，不代表轻拍FAULT自动开启或物理恢复保证；原HID_0限制完整保留。
- 日用三项注意、保留风险与不再安排多日研发长测见[封版说明](<../docs/final_release.md>)；下方为原加固/ARM/UF2核验证据，均保持真实历史身份。

## v2.3.1 加固与实际ARM构建记录（2026-10-21）

- 源码 `7d6ab94e57653deb8e20d142665eee351736f297`，候选分支 `codex/firmware-stability-v2.3.1-20261021`；基线v2.3.0 `2b2c7dc3cacfc64e5ec4a2b64c306f1caf2a1d6e`。79/80配置原字节不变，仅USB轻拍snapshot的异步RAM寿命与等待后BOOT复验；不改键/参数/灯效/存储格式/手柄实现。
- strict UBSan/ASan专项与同公共API旧失败新通过对照PASS；两种BOOT各50000次USB连续周期、30000手柄/24000输入/20000轻拍周期、两灯各200万poll的加速模拟均PASS。本轮完整host exit0、80配置/四层图/manifest、原机械证明/68万事件差分及新稳定性runner均PASS。[Actions Run 39614649971](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39614649971>) regression、bbp9981 ARM及artifact合并成功，deliver/failure_context按候选条件跳过；构建提交 `d607df8b55ab676aa8910e787fb0a18e9e221389`。CI实际32组合CMake/9负例、新ASan/稳定性runner全部PASS；本地无CMake仍如实SKIP。
- [UF2](</vol1/1000/agent/output/1021/213336_9981Pro长期稳定加固_v2.3.1/09_云端构建产物/bbp9981-zmk.uf2>)已下载并独立校验：SHA `ab9c0c10cb46e5c957ae4f55ecd71dac0817e345353e7f7777776a2abf66f50a`，613888B（599.5KiB）/1199块，family `0xada52840`，`[0x1000,0x4bf00)`，无bootloader/NVS块。见[校验JSON](</vol1/1000/agent/output/1021/213336_9981Pro长期稳定加固_v2.3.1/09_云端构建产物/validation.json>)、[运行记录](</vol1/1000/agent/output/1021/213336_9981Pro长期稳定加固_v2.3.1/10_Actions运行记录.json>)、[完整云日志](</vol1/1000/agent/output/1021/213336_9981Pro长期稳定加固_v2.3.1/11_Actions完整日志.zip>)。
- 最终云DTS全文与1166项赋值Kconfig和v2.3.0完全一致；[一致性核验](</vol1/1000/agent/output/1021/213336_9981Pro长期稳定加固_v2.3.1/14_云端基线一致性核验.json>)。FLASH306768B、RAM88878B，比v2.3.0分别多288B/16B；UF2增加512B是256B应用块装入512B容器，不是新增512B RAM。27页A5 PDF与4图可编辑DOCX/离线HTML同批更新，Word只做结构/文本验收，未独立Office渲染。
- **未刷写、未设备长时间验收，device_tested=false；未发布/归档，不推稳定分支。** stock HID_0全局timeout/mutable DMA、abort丢ready缺许可限制未全修；RAM安全不是host ACK。详见[稳定性说明](<../docs/firmware_stability.md>)、[实机验收清单](<../docs/long_term_acceptance.md>)。

## 历史候选：v2.3.0 行为保持型全局重构（2026-10-21）

- 候选分支 `codex/firmware-refactor-v2.3.0-20261021`，重构源码 `5c82f7ac3a4fa0acd5d4e2ae504a0fc5f5659fde`；行为基线v2.2.9 `f7412ec90c925487617a1e8d40dd886c310142a6`，基线源归档与完整host日志已保留。
- 当前专项/机械证明、68万事件旧新差分（含灯光日志与负例）、524288组PWM对照、6组合独立TU链接、完整host exit0和80配置/四层图同步均PASS。[Actions Run 39569977363](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39569977363>) regression/ARM成功，deliver按候选条件跳过；构建提交 `0526e2fea016fe48ab2a1f8cecd1e1db543397f3`。CI实际32配置CMake及9个拒绝负例PASS；本地缺CMake的SKIP不冒作本地运行成功。
- UF2边界/身份校验PASS：SHA `81bbd3bb45bac84733ccecc84d35cad423463efe1d6cd968212d919f8d992a45`，613376B/1198块、family `0xada52840`、`[0x1000,0x4be00)`。[候选UF2](</vol1/1000/agent/output/1021/155910_9981Pro全局重构_v2.3.0/09_云端构建产物/bbp9981-zmk.uf2>)、[校验JSON](</vol1/1000/agent/output/1021/155910_9981Pro全局重构_v2.3.0/09_云端构建产物/validation.json>)、[运行记录](</vol1/1000/agent/output/1021/155910_9981Pro全局重构_v2.3.0/10_Actions运行记录.json>)、[完整云端日志](</vol1/1000/agent/output/1021/155910_9981Pro全局重构_v2.3.0/11_Actions完整日志.zip>)。
- 最终云DTS全文（去日志时间）与v2.2.9逐字节相同，四层168绑定/所有behavior和硬件属性不变；1166项赋值Kconfig亦完全相同。[一致性核验JSON](</vol1/1000/agent/output/1021/155910_9981Pro全局重构_v2.3.0/14_云端基线一致性核验.json>)。实际mode/runtime/USB对象编译，FLASH306480B、RAM88862B。27页A5 PDF/DOCX/HTML导出通过；device_tested=false，未刷写/未全面实机验收。
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
