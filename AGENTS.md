# 9981 Pro 固件：开发与说明书维护规则

## 当前移植合同：完整v2.3.3兼容修正（2026-10-25）

- 基线为原v2.3.3树 `432d6c8`；原分支不动，另建 `codex/ipad-v233-repair-20261025` 移植。手册v2.3.3.1；功能源 `c667531a0c22d6677b735119861a6d6c1ed4b881`（父已冻结），不得以旧048或858当新功能源。
- 上一候选 `79b7b7c` 是v2.2.1旧功能，用户仅确认键盘/鼠标正常，手柄未明确验收；不外推本轮。本轮完整host、ARM/UF2、原生GATT、实机全部待验证，最终源/运行号/hash见[移植记录](<docs/ipad_v233_repair.md>)。
- 85份旧config仅允许层判断/服务接线两处变更，其余字节不改；四层168绑定、五档记忆、指针滤波/削峰/BLE≤10ms积压合并、滚轮固定档/翻转、MEDIA慢呼吸、v2.3.1 USB修复全部保留，禁止恢复v2.2.1旧键图或手册。
- 生产修改限定[手柄runtime](<config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c>) INDEX→stable ID、[构建接线](<config/boards/arm/bbp9981/cmake/gamepad.cmake>)取消HOG注入，新增[独立BLE服务](<config/boards/arm/bbp9981/custom_driver/gamepad_ble_service.c>)逐字节移植自79b7b7c。index bug上一版反事实已证，设备实际层重排未知，不称实机根因已证。
- BLE stock196与GP141的Report Map分属两个HIDS，Input9无ID，原服务不拼接；手柄CP每peer独立suspend、无remote wake。身份/连接/cache仍共享，多HIDS不保证iPad手柄支持；不清配对/settings。
- 本次文档子任务只写手册、README、本文件及新增repair记录；不改manifest/tools/config/tests，不提交、不操作云端、不委派。父负责manifest、导出与QA；历史验证仅属于对应旧版本。

## 历史明确功能合同：v2.3.3（2026-10-22）

- 用户已刷v2.3.2，反馈“有提升，但是提升不大”，坏C-to-C线已由用户确认；随后明确授权“用z循环调节dpi吧，重启之后也要保留记忆，另外media层键盘灯还是闪烁，给我把它改成慢速呼吸灯”，并选择0.6→0.9→1.2→1.5→1.8→0.6、首次1.2。仅UPPER Z/P28为正常可映射behavior，DEFAULT字母Z/MEDIA Dpad↓不变；不改M鼠标翻转、Y/F留空或维护键，不加隐藏Z监听。
- “DPI”是固件相对位移增益，非已核实的sensor DPI寄存器；物理左下aA/Ctrl减半所选档。实际cursor和RAW tap影子共享单帧锁存：只有非重入A320 poll推进motion latch；选档/load在I2C/输出中交错时下一完整poll才应用，epoch清小数/滤波并interrupt旧tap/replay，五次回原档亦不混。后续若新增并行传感器消费者，须显式传frame context，不复用全局latch。
- 独立key `bbp9981_cursor/gain_v1`，两字节`[1,index]`，只首次commit加载、用户先改优先；静态delayable work、末次操作1000ms合并保存、最多3失败后等下一次用户cycle。无逐帧/动画/默认加载写flash；backend锁外，旧save不清新generation。正常已保存值重启恢复，中途断电或存储故障不保证，CONFIG_SETTINGS=n仅volatile测试。
- 仅MEDIA键盘背光由快闪改20ms/1%步进、10→100→10、标称3.6秒慢呼吸，仍乘用户亮度/开关并尊重idle/cancel/缓存和错误重试。SYM500ms闪、UPPER原5%/20ms呼吸、DEFAULT授权亮灯、触板灯及两灯保存器不变。
- [本轮范围闸](<tests/user_preferences_scope.py>)对8份受控修改/4新文件做明确历史投影，还原全部81份v2.3.2配置（73份原字节未动），再进入旧BASE证明；所有既有C oracle/SDK fixture冻结。Zephyr DTS先于Kconfig，绝不能用CONFIG宏gate键图；node与Z无条件，CMake始终编译adapter，feature OFF时为与&none输入等效的OPAQUE shim且不链接speed service。OFF不是DTS/固件字节相同，历史投影才exact。默认host含[新runner](<tests/run_user_preferences_tests.py>)的实际ON与独立不带service的OFF八组合/nm/ASan；云端必须实际查node、0cell、UPPER28和其余167绑定。首次Run39740546545缺node/Z已拒交隔离，编译成功不算键位正确。SDK_SETTINGS_TABLE首次注册新behavior可能另存内部local ID，不等于逐帧写速度key。
- 最终功能源 `048201f8caeedeb729df3ca22aa2186c50aea476`，构建 `e2c4966136b5e6af5db795c6c46048672758ba30`、Run39743108339回归/ARM和实际DTS/UF2校验通过；SHA `ef911f23e250c55204d9342dea0a6322161e3c4ee624379c8bbe7ed8467c401a`，618496B/1208块、FLASH309124/RAM89198B。旧1167项Kconfig不变，仅2新开关+1派生DT值，云DTS只新node/Z，其他167绑定不变；首次缺Z产物严禁刷写。新候选分支 `codex/cursor-presets-v2.3.3-20261022`，基线 `11218ad83292f5ccf9105e41010c1062073e474d`；实际构建/UF2结果见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)和[本轮说明](<docs/cursor_preferences.md>)。本批同步手册/四图/PDF/Word/HTML，沿用“以后都直接构建”授权，仅必要精确仓库候选推送/既有Actions/下载校验，不推稳定、发布/归档回推、刷写、清配对/settings或改主机；600KB不约束，不删功能。用户对v2.3.2有限反馈单列，不追改旧`device_tested=false`。

## 历史明确修正合同：v2.3.2（2026-10-22）

- 用户已刷v2.3.1，并反馈蓝牙指针小幅往返抖动与速度不匀；重新明确授权“小位移抖动、输出节奏不均匀、旧削峰算法这些一起处理”，不以先关轻拍为前提。此授权取代此前“不再迭代”的限制，仅限本次鼠标修正；600KB仍不是约束，不删功能。
- 普通鼠标在原1.2/0.6小数增益前新增一计数逐轴滞回与历史四慢帧削峰；仅对已提交cursor帧生效，不改轻拍RAW影子判据、滚轮或游戏杆。无惯性/补偿尾巴，但起步/反向/空闲会丢弃边界计数，故不称完全无损或绝对线性。
- BLE仅首发前合并既有积压中首入队窗≤10ms、同captured conn/buttons、严格同向共线且不溢出的纯XY；零帧/按钮快照/全部滚轮/拐角/反向/peer均是屏障。无新增定时等待、线程或重试策略；并非固定10ms发送或host ACK。
- 四份既有配置仅追加显式feature块，新增纯核；[新范围闸](<tests/pointer_steady_scope.py>)精确还原全部80份封版配置，旧BASE/全部既有C oracle与SDK fixture不换（仅mock新增官方peek声明）；旧验证跑OFF，新[专项](<tests/run_cursor_tests.py>)必须跑实际ON。
- 候选分支 `codex/pointer-steady-v2.3.2-20261022`；功能源 `1124252fcd43375a27b826224747121ba78e06a2`，构建 `3d7078e65bbc1e0c30cb4a8da272c1f578aaf343`。完整host、Run39725910232 regression/ARM及UF2校验通过；DTS全文与1166旧Kconfig不变，仅新增本开关，FLASH307692B/RAM89102B（+924/+224），UF2 SHA `dbfc480a7da14ec8932b92083a60aea13986f98847320ba854577b838eda940c`。按既有“以后都直接构建”授权完成必要候选推送、既有Actions、UF2校验与同批手册；不刷写、清配对/settings、改主机、推稳定分支或发布/归档。源码/构建/交付/实机反馈分开记录，具体进度见[指针修正说明](<docs/pointer_steady.md>)和[交付记录](<firmware-delivery/XBOX_CURRENT.md>)。

## 历史研发封版合同：v2.3.1（2026-10-22，后被上方明确修正授权取代）

- 用户明确这是最后一个固件版本，不再投入研发；600KB不是约束。最终推荐原v2.3.1二进制，不削功能、不再主动调参/重构；除非用户重新明确授权，不继续版本迭代或安排多日长测。
- 最终固件仍是源码 `7d6ab94e57653deb8e20d142665eee351736f297`、构建 `d607df8b55ab676aa8910e787fb0a18e9e221389`、Run39614649971，UF2 SHA `ab9c0c10cb46e5c957ae4f55ecd71dac0817e345353e7f7777776a2abf66f50a`。本轮仅测试/文档，全部80配置与实际ARM构建原字节相同，冻结由[封版runner](<tests/run_final_review_tests.py>)执行。无实现变化，不制造新ARM/UF2版本。
- 两名独立只读审查与父侧固定SDK/HAL核查未发现应阻挡封版的新增必要修复。额外4500周期模拟验证仅证明“后续stock报告被接纳且完成后可恢复许可”，不是自动自愈；不直接补sem、不伪造late-ready，许可恢复不代表FAULT轻拍自动开启。
- [封版说明](<docs/final_release.md>)是单一推荐与日常三项注意；[手册](<docs/9981Pro_使用说明书.md>)v2.3.1.1只是文档修订，固件仍v2.3.1。旧24/72小时/7天清单改为可选参考，不是用户待办。研发封版不等于已实机认证、刷写或稳定发布。
- 仍不刷写、不清配对/settings、不改主机、不推稳定/发布分支或自动归档；既有构建授权只在新的明确必要实现改动时使用，不因文档更新重复构建。

## 历史稳定性合同：v2.3.1（2026-10-21）

- 用户要求向稳定/长期使用推进；冻结产品用法，只修有证据的缺陷。基线v2.3.0 `2b2c7dc3cacfc64e5ec4a2b64c306f1caf2a1d6e`；源码 `7d6ab94e57653deb8e20d142665eee351736f297`，候选分支 `codex/firmware-stability-v2.3.1-20261021`。
- 唯一生产变化是[USB轻拍快照](<config/boards/arm/bbp9981/custom_driver/trackpad_usb_snapshot.h>)：修复异步栈源寿命和等待中REPORT→BOOT窗口。79/80配置逐字节不变；SDK fixture与原行为oracle保留。原重构BASE仍f7412ec，新增[稳定范围闸](<tests/check_stability_scope.py>)作为精确单文件例外，不放宽全局保护。
- 专项strict UBSan、旧失败/新通过ASan、两种BOOT各50000次USB连续周期及手柄/输入/轻拍/两灯连续模拟均PASS；完整host exit0，Run39614649971 regression/ARM、CI32配置9负例及UF2校验PASS，构建 `d607df8b55ab676aa8910e787fb0a18e9e221389`。云DTS全文/1166项Kconfig和v2.3.0相同，FLASH306768B/RAM88878B（+288B/+16B）；完整SHA和边界见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)。没有设备长测/刷写，不将模拟天数、poll数或SDK接受当真实续航/host ACK。
- 未全局修复stock HID_0 ignored-timeout/mutable DMA；abort丢ready可能缺许可，RAM退休不能补共享sem。详情与待用户执行的实机清单见[加固说明](<docs/firmware_stability.md>)、[实机验收](<docs/long_term_acceptance.md>)。
- 沿用下方“以后都直接构建”授权，仅必要的精确仓库、新版本候选推送、既有Actions和UF2校验；不推稳定分支、不发布/归档、刷写、清配对/settings或改主机。

## 历史重构合同：v2.3.0（2026-10-21）

- 用户明确要求全局重构且现有功能完全一致；行为基线是已确认的v2.2.9源码 `f7412ec90c925487617a1e8d40dd886c310142a6`，不是更早键图。候选分支 `codex/firmware-refactor-v2.3.0-20261021`，重构源码 `5c82f7ac3a4fa0acd5d4e2ae504a0fc5f5659fde`；基线源归档与完整host日志在当日重构交付目录保留。
- 已拆分模式发布服务/滚轮纯核、手柄runtime与薄behavior、USB实现归位、共享PWM后端与板级CMake职责；两套不同语义的亮度保存器、协议/SDK前缀、轻拍核、所有键位/阈值/GPIO原样保留。详见[全局架构](<docs/firmware_architecture.md>)。本版只改内部结构，不顺手修正旧限制/bug，不新增功能。
- 基线完整host、当前专项、68万事件旧新差分、524288组PWM对照与6组合独立TU链接已PASS；当前完整host exit0、80配置/四层图同步、Actions Run `39569977363` regression/ARM及27页PDF/DOCX导出PASS；构建提交 `0526e2fea016fe48ab2a1f8cecd1e1db543397f3`，UF2 SHA `81bbd3bb45bac84733ccecc84d35cad423463efe1d6cd968212d919f8d992a45`。CMake32组合和9负例已在CI实际执行通过；本地无CMake仍明确SKIP。最终云DTS全文与1166项Kconfig跟v2.2.9完全一致，FLASH306480B/RAM88862B。不把软件结果冒作实机验收。
- `tests/check_tap_successor_scope.py` 历史字节恢复闸未放宽，在冻结参考树执行；当前精确变更范围/机械证明/差分由 `tests/run_refactor_tests.py` 管。BASE固定f7412ec，不能为了过测试随意改基线。
- 沿用用户直接构建授权：仅精确原仓库的版本专属新候选推送、既有Actions、下载/校验UF2；不刷写、不清配对/settings、不发布/归档、不推稳定分支。下方v2.2.x均为对应版本历史，不能当当前验证证据。

## 当前说明书入口

- 面向用户的可维护正文：[9981 Pro 使用说明书](<docs/9981Pro_使用说明书.md>)。
- 默认键位真源：[主键图](<config/bbp9981.keymap>)和[板级键图](<config/boards/arm/bbp9981/bbp9981.keymap>)，两份实际绑定必须一致；C 驱动中的物理位置监听也必须核对。
- 版本与源码核对清单：[manual_manifest.json](<docs/manual_manifest.json>)。
- 导出工具：[build_manual.py](<tools/build_manual.py>)与[print_manual.mjs](<tools/print_manual.mjs>)。历史 PDF / Word / HTML 留存，不覆盖旧版。

## 用户明确新增的设定（2026-10-19）

**以后每次更新功能或默认键位，必须在同一批次同步更新说明书；文档同步是交付验收的一部分，不是事后待办。**

1. 范围包括：键位、各层/透明或禁用键、组合键、物理监听、短按/长按/单双三击时间、触控板/滚轮/轻拍、灯光、亮度与其他记忆、连接/输出、电源/空闲、维护键及重要限制。
2. 修改实现时同时维护正文的操作、示例、排错、限制与版本记录；若相关配置只作非功能修改，也须明确审阅并刷新核对清单，不能盲刷哈希掩盖文档过时。
3. 保留“源码提交、构建、已交付、用户刷入、实机反馈”的区分。最新候选不能自动称为用户已安装或已验收。运动启发式不是可靠触点检测，历史回放候选数不是命中率。
4. 若改键，重建四层各 42 键的图，检查默认键图与驱动物理位置监听是否仍一致。若改功能，正文先写清新旧差异与适用范围，再刷新清单。
5. 文档版本至少递增修订号，并记录日期、功能源码版本、验证状态、已知问题。导出 PDF、可编辑 Word、离线 HTML 供用户阅读；文字、图示与所有导出都应来自同一正文和键图。
6. 每批次都运行 `python3 tests/check_manual_sync.py`；完整主机回归已经包含此检查。`config/` 任一受跟踪文件增加、删除或内容变化而未同步清单会失败。同步检查只是防遗漏，不替代人工核对说明是否真实。
7. 仅写说明书不授权改固件功能、自动触发新 ARM 构建、推送、刷写、清配对或主机设置。2026-10-20 用户已撤回未发布 GBA 候选，另明确选择 **MEDIA / Xbox 布局标准 HID**，不是原生 XInput，不冒充微软 VID/PID；不得继续发布 GBA 或称 Xbox 已交付。轻拍继续暂停调优。2026-10-20 10:44 用户曾精确批准一次 Xbox 分支推送 / Actions 云编译 / UF2 校验交付；该授权不自动延伸到后续版本。2026-10-20 用户当前又明确要求“出固件吧”，针对 v2.2.4 新增 aA 锁轮功能，本轮按最终源码推送目标分支、运行 Actions ARM 构建、校验并交付 UF2；不含刷写、清配对或擦设置。

## v2.2.5 未发布锁轮方向候选（2026-10-20）

- 用户尚未安装 v2.2.4，要求调整触控板方向。功能源码提交 `3c5235b21f8452630850053620635d4aea7cb47d`；2026-10-21 已按明确授权推到新候选分支 `codex/xbox-layout-v2.2.5-20261021`，构建提交 `dcfabda69c9ed81a7fb92aaf0df29e74566be763` 的 Actions Run `39520450915` 主机回归与 ARM 构建通过，UF2 已下载并本地边界校验。v2.2.4 仍是最近已发布 / 归档版本，旧旋转行为不符合新需求；v2.2.5 未发布、未刷写。
- aA 双击锁轮并固定 UPPER 时默认方向 0°；右肩完整三击映射左→上、上→右，左肩完整三击映射右→上、上→左。每组三击仅推进一个90°步进，可继续累积；解锁再锁定归零。按住 aA 临时显示原方向并恢复原生肩键滚轮，不清除本次锁定方向。
- 锁定时任一肩键仍使触板滚轮半速并屏蔽该肩键自身 Wheel。肩键按下到松开 ≤180ms、松开至下一次按下 ≤220ms；另一侧肩键或其他按键会取消未完成三击。不得改写为模糊顺/逆时针描述，方向以箭头映射为准。
- 2026-10-21 用户随后明确授权 v2.2.5 推送到新候选分支并运行 Actions，且要求今后明确提出“构建”时直接执行。当前操作限定为推送到 `codex/xbox-layout-v2.2.5-20261021`、运行构建、下载并本地校验 UF2；不推已发布目标分支、不触发归档回推，不刷写、清配对或擦设置。

## 用户直接构建授权（2026-10-21）

- 用户明确要求“构建，以后都直接构建”，并选择授权当前及未来本项目明确构建请求中必要的源码推送与构建：若没有本地 ARM 工具链，则核验仓库后将该版本必要源码推送到新的、版本专属候选分支，运行既有 Actions 构建，下载并本地验证 UF2；不再为同样的必要构建流程重复询问。
- 此授权**不包括**推送到已发布 / 稳定目标分支、云端自动归档 UF2、发布版本、刷写设备、清除配对、擦除 settings 或修改主机配置。目标仓库、候选分支或写入范围若与此流程不同，先确认。

## v2.2.5 Xbox 标准 HID 合同与构建状态（2026-10-21）

- 当前正文版本为 v2.2.5 未发布候选（Xbox布局标准HID，非XInput）；v2.2.4 仍是最近已发布 / 归档版本。v2.2.5 在新候选分支的 ARM 构建与 UF2 校验已通过（Run `39520450915`），未刷写或实机验收。锁轮初始不旋转，左右肩三击按上方精确箭头映射步进；解锁 / 重锁回正。肩键半速 / 屏蔽自身Wheel、aA按住临时恢复方向与原生肩键滚轮均保留。v2.2.4 交付件是旧默认旋转，勿当成该候选；第26–27章说明velocity虚拟摇杆。
- 仅 MEDIA：E/X/S/F＝D-pad 上/下/左/右，不是左杆；D/K 独立输出 L3/R3；触板机械 DOWN 固定为 Guide/Xbox，空格亦为 Guide；I/J/L/M＝Y/X/B/A；通话/结束＝LB/RB；双肩＝数字 LT/RT（0/255）；T/Y＝View/Menu。黑莓按住换右杆、松开不锁定，按钮不因切杆重映射。19 个 gamepad 位置、3 个回 DEFAULT 出口、20 个 None；N/Enter 留空。v2.2.4 在此映射上新增 aA 锁轮功能，已推送并由 Run 39428313318 成功 ARM 构建、UF2 边界验证；旧 Run 39361865259 不匹配，当前件见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)。未刷写。
- 触板只有相对光学位移：默认左 X/Y、按住黑莓右 Z/Rz，raw 右正下正；velocity 不累计位置。默认 gain8 / 逻辑 deadzone4 / 75%新+25%旧对称舍入，最大输入首帧为 ±95 而非满量程；有效位移续时，120ms 无有效位移本地归零，GPIO/I2C 故障清轴但不代替按钮 UP。不能把本地计时写成 host ACK 或收包时限；停住手指不能保持杆倾斜；触板机械点击只发 Guide/Xbox、不作 L3/R3；D/K 是独立 L3/R3 按键。
- 游戏层新增 driver 独占分支、晚发送 gate 及轻拍临时保护 / 取消旧候选，退出恢复原鼠标；v2.2.4 新增 A320 锁轮 / 半速 / aA 覆盖，v2.2.5 加入明确方向步进与锁定边沿回正。原 gesture 参数、开机 OFF、三击开关、bridge I/O / 必要 UP、灯光与 GPIO / 矩阵保留。范围闸验证 46 份旧配置字节相同；精确剥离 GP 门和 v2.2.4/v2.2.5 scroll block 后，A320 / microtap / bridge 源恢复基线（A320 仅归一历史 CRLF）；8 个集成文件 + 12 个新增 gamepad/scroll 配置文件。P37 slow、P41 滚轮物理监听仍存在，但游戏层不发鼠标 / 滚轮，P37 不是摇杆加速。
- 标准 HID ID4：body9，USB10（aligned staging12 但只发10），BLE9 无 ID；13 个真实 Button usages、四轴、数字 LT/RT。它不是 Xbox 原生 XInput，不保证 Windows XInput-only、Xbox 主机或各版本目标 App 兼容；无振动 / Share，Guide 可能被系统截获。矩阵三键鬼键风险未消除，移动 + modifier + 开火等组合必须实测。
- 旧 `28f6c86` 与手册第 19 章的 UF2 SHA 保留作键鼠回退；旧 GBA `abe65ea` / `89fbe9d` 仅是本地撤回历史，不搬动旧产物。旧版 ARM / UF2 构建源 `1c61b81`、Run `39361865259` 仅对应旧键图（S=↓，Z/K空），见[历史交付记录](<firmware-delivery/XBOX_CURRENT.md>)，不能用于 v2.2.4；v2.2.3既有测试历史PASS；本轮v2.2.4滚轮/肩键专项及完整run_host_tests.py（strict UBSan、66个配置文件/四层键图与说明书同步）PASS。源码 `9f41197` 已推送；Run `39428313318` ARM 构建及 UF2 验证成功，归档提交 `89311aa`。未 USB / BT 实机枚举、刷写或 App 实玩；host / 云构建都不是实机验收，旧键图的兼容结果不能外推。v2.2.5 肩键三击、方向映射、默认方向与 aA 临时覆盖专项 strict UBSan 已通过；本批说明书 / 清单同步后的完整 `python3 -B tests/run_host_tests.py` exit 0；候选分支 Actions Run `39520450915` 的主机回归、ARM 构建及本地 UF2 边界校验 PASS；仍非实机测试。
- [使用说明书](<docs/9981Pro_使用说明书.md>)、[实现笔记](<docs/gamepad_implementation_notes.md>)、[README](<README.md>)、[核对清单](<docs/manual_manifest.json>)与四层图稿须同批同步；导出 PDF / Word / HTML 并核对排版。维护命令本身不构成推送、Actions、刷写或清设置授权；如需构建，按上方 2026-10-21 的用户明确授权范围执行。

- 本次 NAS 工作树的 `origin` 是旧本地工作树，不是 GitHub；本次使用已核实的仓库 HTTPS 地址和单命令凭证 helper 发布，未改全局 Git / 代理。后续须先核对 remote，不把本机路径推断成云端。

## v2.2.6 MEDIA remap / scroll gesture / mouse-flip candidate (2026-10-21)

- 用户确认 v2.2.6 MEDIA 映射：P0/P1 肩键=LB/RB；P2 Call=LT；P6 Endcall=RT；D-pad 左从 S/P18 移到 A/P17；O/P15=Y、K/P24=X、Backspace/P26=B、$/P35=A；D/P19=L3、L/P25=R3；仅 SYM/P40 与右 aA/P41 退出 MEDIA。其他层不随重映射改变。
- 锁轮手势按用户原话实现：锁定时黑莓 P3 三击令左向变上向，顶部物理返回 P5 三击令右向变上向；短按/未完成序列消费，不产生蓝牙槽副作用；锁定期间 P3 永不选槽1，P5 非三击不选槽2。按住物理右 aA 并双击 P5 切换鼠标模式，双击不发 BT_SEL 2。
- 鼠标翻转是独立 volatile DEFAULT-mouse 功能：反转普通指针 Y，并交换默认层两肩的 LCLK/RCLK 角色；不改 X、滚轮、手柄轴或 SYM/UPPER/MEDIA 肩键，不持久化，重启关闭。
- 候选分支 `codex/xbox-layout-v2.2.6-20261021` 已推送，功能源码提交 `7a956e9b1a7fb2dc465bfdb9212cdf7f27977e2b`，构建提交 `b05dee65dc929855c809036c539b14488cd7d4b0`。完整 `python3 -B tests/run_host_tests.py` exit 0（68 份配置、四层42键图/手册同步、strict UBSan host regressions），范围闸 45 份旧配置逐字节相同、8 个集成 +14 个新增配置；回归基线为实际父提交 `51c5efd`。Actions Run `39540020475` 的 regression 与 `bbp9981` ARM build PASS；UF2 本地验证 SHA `9f4daf7b22ab833c01ed5322115b1a8977cbd4aff8701c04e29ddd73b76d155a`，611328 B / 1194 blocks / `[0x1000,0x4ba00)`。仅候选分支，不归档、不刷写、不清配对、不擦设置、不改主机；v2.2.4 仍是最后已发布 / 归档件，host / cloud build 不等于实机验收。

## v2.2.7 历史修正候选与当时合同（2026-10-21）

- 本节仅适用于 v2.2.7 历史候选，方向仍为累加步进、MEDIA 菜单仍在 T/Y；当前合同见下方 v2.2.8，不自动沿用旧映射或验收状态。此前用户确认已刷入 v2.2.6、未用 Studio 保存过键图，反馈方向/肩键与期望不符、手势和“右aA+P5第三蓝牙槽”入口失灵；不将这些反馈归因 Studio 覆盖，不改写历史产物的 `device_tested=false` 为新版硬件通过。
- 最终 MEDIA：W/P8=上、D/P19=右、Z/P28=下、A/P17=左；S/P18=L3、L/P25=R3；P0/P1 肩键=数字 LT/RT，P2 Call/P6 End=LB/RB。O/K/Backspace/$=Y/X/B/A，P4/Space=Guide，T/Y=View/Menu；E/P9、X/P29、F/P20 在 MEDIA 留空。19绑定/2出口/21None，仅 P40/P41 回 DEFAULT。
- 恢复槽优先：物理右 aA 按住时 P3/P5 立即 `BT_SEL 1/2`，即使 scroll latch 已锁定；只有 aA 松开且锁轮时，P3/P5 完整三击分别左→上/右→上，其余点按吞掉。三击按住≤180ms、释放至下次按下≤220ms；aA 双击仍每拍<450ms、间隔≤350ms，解锁/重锁回正。
- 鼠标翻转快捷键移到物理右 aA 按住+F/P20 双击（180/220ms），再做切回；UPPER F 未满足组合时无输出。普通指针Y反向、DEFAULT肩键LCLK/RCLK互换；X、滚轮、手柄/其他层不变，volatile、重启关闭；P5 不再被鼠标双击占用。
- 修复3秒退层：`aa_layer` 仲裁 momentary/sticky/fixed owner；`aa_sl` 复用 SDK sticky quick-release/3000ms、child改为 `aa_layer`；`aa_mo` 为450ms hold-preferred、children `aa_layer/aa_sl`。DEFAULT/SYM P41=`&aa_mo 0 1`，UPPER P41=`&aa_layer 2`；sticky迟到UP仅清自己的owner，不撤fixed。SYM退层但不清 scroll latch。
- 功能快照 `88e744f51e386db853ae25c4438b33cb23557af5`，候选分支 `codex/xbox-layout-v2.2.7-20261021`。pinned SDK sticky/mo/to 三份Git blob身份对照、owner12case旧bug/新修复对照、真实A320/owner/gesture/rotation两种listener顺序联动（超3秒、按住aA切槽双松手顺序、解锁重锁回正）、gamepad专项与scope gate均PASS；范围45份旧配置/8集成+16新增。完整 `python3 -B tests/run_host_tests.py` exit 0，70配置/四层42键图与手册清单同步PASS；初次完整运行只因矩阵测试仍断言旧aA绑定而失败，修正为精确`aa_mo/aa_layer`契约后全部通过，未放宽GPIO/鬼键断言。**Actions Run `39551522029`（构建提交 `045a7a3a0da9ba372d99ea747e27a15ababb8694`）的regression与bbp9981 ARM均PASS，deliver job按候选条件跳过。新UF2本地验证PASS：SHA `140d608be34b35c237fcc11db8c8bd56c5b55c453c9cae921b2c753750887bc6`，613376 B/1198 blocks/family `0xada52840`/`[0x1000,0x4be00)`；交付时未刷写/未实机验收，用户14:33后续反馈见下方v2.2.8节，不追改旧校验记录。**
- 用户在本轮被问是否构建时再次直接要求“以后都直接构建”：**本轮 v2.2.7 及今后本项目同范围固件修正，直接执行必要的版本专属新候选分支推送、既有 Actions ARM 构建、下载与UF2校验，不为同流程重复询问。** 仍须核验精确仓库；不推稳定/已发布分支，不发布、不自动归档回推，不刷写、清配对、擦设置或改主机。范围/目标不同仍先确认；本文档不把构建成功当实机验收。

## v2.2.8 固定方向档与 R/U 菜单当前合同（2026-10-21）

- 本节为当前合同；前述v2.2.5–v2.2.7为各自历史，不能沿用累加方向、T/Y菜单或验收。v2.2.8候选分支 `codex/xbox-layout-v2.2.8-20261021`，功能源码 `88edf7f323b643465fa1470fdc2e090dd3ee0ffa`、构建提交 `ffc41698f6972f3a26cc4ce459956a06fe8c3a46`。本版定向strict UBSan（全部旧档绝对选择/幂等/互切、两种listener顺序实际A320双档四向/aA覆盖、preset全向量与饱和）及完整host exit0；scope45旧配置/8集成+16新增、70配置/四层42键图/manifest同步PASS。Actions Run `39557844223` 的regression和bbp9981 ARM均成功，deliver job跳过；最终云DTS逐位置核对R/U、T/Y空且其他层绑定与v2.2.7相同。UF2本地校验PASS：SHA `a7753c5e242f5e42a95d183f2802a8d189abc9f0311aedac8a690a94236e6593`，613376 B/1198块/family `0xada52840`/`[0x1000,0x4be00)`；未发布/归档，未刷写/实机验收。
- 用户于2026-10-21 14:33确认 **v2.2.7 蓝牙按钮已恢复、触板三击已生效、手柄基本可用**；固定方向语义与R/U菜单仍待新版。这是有限用户反馈，不是全部USB/BT、App、多键、保护场景全面硬件验收；不改旧校验记录的 `device_tested=false`。
- **固定档而非累加：** 仅 aA 松开且锁轮生效时，P3黑莓完整三击直接选 **左→上、右→下、上→右、下→左** 档；P5顶部返回完整三击直接选 **右→上、左→下、上→左、下→右** 档。同键重复幂等，无论原档位都直接选目标；切换另一键直接选其目标，不在原档继续转90°。默认锁轮0°，解锁/重锁回正；按住aA临时原方向和原生肩轮，松开恢复所选固定档；锁轮肩键半速与屏蔽自身Wheel保留。
- 按住物理右aA时 P3/P5 立即 `BT_SEL 1/2`，已锁轮亦优先选槽；aA+F/P20双击鼠标翻转不改。完整三击每拍按住≤180ms、释放至下次按下≤220ms，其他键取消未完成序列，其余点按吞掉；aA锁轮双击仍每拍<450ms、间隔≤350ms。aA owner仲裁、SYM退层不解锁轮与volatile鼠标模式限制均继承v2.2.7。
- **仅MEDIA移动菜单两键：** View 从 T/P11 移到 **R/P10（action12）**，Menu 从 Y/P12 移到 **U/P13（action13）**，原 T/Y 在MEDIA留空；其余保持v2.2.7：W↑D→Z↓A←、S/L=L3/R3、肩LT/RT、Call/End LB/RB、O/K/BSPC/$=Y/X/B/A、P4/Space=Guide，19绑定/2出口/21None，仅P40/P41回DEFAULT。非MEDIA层不随此改键，触板虚拟杆、协议、轻拍、灯光、GPIO/矩阵及保护限制不变。
- v2.2.7 历史构建提交 `045a7a3a0da9ba372d99ea747e27a15ababb8694`、Run `39551522029`、UF2 SHA `140d608be34b35c237fcc11db8c8bd56c5b55c453c9cae921b2c753750887bc6` 保留，不能当v2.2.8构建。v2.2.4仍为最后发布/归档版。
- **继续保留用户“以后都直接构建”授权：今后本项目同范围固件修正，直接执行必要的版本专属新候选分支推送、既有Actions ARM构建、下载与UF2校验，不为同流程重复询问。** 仍须核验精确仓库；不推稳定/已发布分支，不发布、不自动归档回推，不刷写、清配对、擦settings或改主机。范围/目标不同仍先确认，构建成功不等于实机验收。

## v2.2.9 UPPER 快捷键调整（2026-10-21）

- 当前候选分支由父任务创建：`codex/xbox-layout-v2.2.9-20261021`；功能源码提交 `ec6e492ca452b19cf3fa1ec35e7a72f0774fc43e`。五项灯控全部左移一个物理键：C/P30键盘 RGB_BRD→X/P29；V/P31触板 BL_DEC→C/P30；B/P32键盘 RGB_TOG→V/P31；N/P33触板 BL_INC→B/P32；M/P34键盘 RGB_BRI→N/P33。M/P34接管右aA按住时的鼠标翻转双击；F/P20在UPPER无输出。其余行为不变。
- v2.2.8固定方向档/R-U菜单合同与其完整构建、Actions Run `39557844223`、UF2 SHA `a7753c5e242f5e42a95d183f2802a8d189abc9f0311aedac8a690a94236e6593` 等历史证据完整保留在下方历史正文及交付档案；v2.2.8未刷写/未实机验收。v2.2.9完整host、Actions Run `39563315080` regression/ARM、UF2边界校验及27页A5 PDF/DOCX导出均PASS；未刷写/未实机验收。UF2 SHA `8a3506d9ad47b0ca033fea07f7e2bf8724d1e4ac434b2a789a2826ff859e907e`，构建提交 `bd66b98890f56efaec2bcd7a0361236afd4688aa`。
- 保留用户“以后都直接构建”既有授权范围：版本专属候选分支和必要Actions/UF2流程；不刷写、清配对、擦设置、发布/归档或推稳定分支。候选Actions构建与本地UF2验证均完成；仍不发布、归档或实机刷写。

## 更新和导出的最短流程

```bash
# 先改实现（如已授权）及 docs/9981Pro_使用说明书.md，再填写实际版本：
python3 tools/build_manual.py --source-commit <功能源码提交> --refresh-manifest --review-note '本轮功能/键位与说明书同步说明' --output-dir <当天同任务交付目录>
python3 tests/check_manual_sync.py
# 导出 PDF 和键位 PNG：只使用本人新建的打印标签页，结束关闭自己的标签，不操作用户标签。
node tools/print_manual.mjs --output-dir <当天同任务交付目录>
# 再导出带键图的 Word，并核验三个格式：
python3 tools/build_manual.py --output-dir <同一交付目录> --word-only
```

未执行或不可用的验证要如实说明；不能用“同步清单已更新”冒充实机测试。说明书、导出工具与本规则归项目所有，不把历史旧手册或某个全局记忆自动当当前版本。
