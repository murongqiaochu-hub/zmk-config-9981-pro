# MEDIA / Xbox 布局标准 HID 手柄：实现与验证说明

## 当前 v2.3.1 稳定性加固

2026-10-21，源码 `7d6ab94e57653deb8e20d142665eee351736f297`，候选 `codex/firmware-stability-v2.3.1-20261021`。手柄runtime/USB/BLE与描述符均未变；唯一生产变更是USB键鼠扩展的轻拍snapshot，修异步栈源寿命与等待期间BOOT切换。它与独立HID_1仍隔离，不能宣称整个HID_0全修；共享sem丢ready限制保留。两种BOOT各50000次USB周期、30000次gamepad路由/故障恢复及其他输入/保存连续模拟PASS，旧失败/新通过ASan保留；完整回归与本版ARM/UF2见[交付记录](<../firmware-delivery/XBOX_CURRENT.md>)。详情见[加固记录](<firmware_stability.md>)、[待执行实机清单](<long_term_acceptance.md>)。下列所有旧版本Run、资源和设备状态只属其历史版本。

## 历史 v2.3.0 行为保持型重构

2026-10-21。基线v2.2.9 `f7412ec90c925487617a1e8d40dd886c310142a6`，重构源码 `5c82f7ac3a4fa0acd5d4e2ae504a0fc5f5659fde`，分支 `codex/firmware-refactor-v2.3.0-20261021`。只将behavior参数适配与运行时分离、独立USB实现由头文件归位到C文件，另分离触板模式/滚轮核、共享灯光PWM及CMake职责；所有映射、原FSM、协议、队列/取消/引用和SDK前缀顺序不改。27个runtime函数、12个USB函数及整文件允许变换的机械证明PASS，原gamepad专项与基本类型API边界/非零opaque、6组合独立TU链接PASS，旧新68万事件输出/调度/引用对照一致。当前完整host exit0、80配置/四层图同步、Actions Run `39569977363` regression/ARM、CI实际32配置CMake及9负例、UF2校验与27页说明书导出均PASS；最终云DTS全文及1166项Kconfig与v2.2.9完全一致，FLASH306480B/RAM88862B。构建提交 `0526e2fea016fe48ab2a1f8cecd1e1db543397f3`，UF2 SHA `81bbd3bb45bac84733ccecc84d35cad423463efe1d6cd968212d919f8d992a45`；未刷写/实机验收。架构与其余范围见[全局架构](<firmware_architecture.md>)、[交付记录](<../firmware-delivery/XBOX_CURRENT.md>)。

## 历史说明：v2.2.9及更早

以下源码/Run/资源数字与DTS核对仅属于所标历史版本，不是v2.3.0 ARM或实机证据；行为合同除版本记录外继续保留。

2026-10-21，与[使用说明书 v2.2.9](<9981Pro_使用说明书.md>)同步。当前候选功能源码提交 `ec6e492ca452b19cf3fa1ec35e7a72f0774fc43e`，候选分支 `codex/xbox-layout-v2.2.9-20261021`；v2.2.9仅将UPPER五项灯控左移一物理键（C/V/B/N/M→X/C/V/B/N），并将鼠标翻转组合从F/P20迁至M/P34；F/P20单独无输出，其他行为不变。完整host、Actions ARM、UF2及说明书PDF/DOCX导出均PASS，未实机验收。用户已确认 Xbox 布局标准 HID，**不是原生 XInput，不冒充微软 VID/PID**。v2.2.8固定方向档/R-U菜单完整构建证据保留为历史（未刷写/未实机验收）：源码 `88edf7f323b643465fa1470fdc2e090dd3ee0ffa`、[Run39557844223](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39557844223>)、构建提交 `ffc41698f6972f3a26cc4ce459956a06fe8c3a46`、UF2 SHA `a7753c5e242f5e42a95d183f2802a8d189abc9f0311aedac8a690a94236e6593`。v2.2.7 用户于2026-10-21 14:33确认蓝牙按钮已恢复、触板三击已生效、手柄基本可用；不代表全场景硬件验收。更早v2.2.7源码 `88e744f51e386db853ae25c4438b33cb23557af5`、构建 `045a7a3a0da9ba372d99ea747e27a15ababb8694`、[Run39551522029](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39551522029>)及UF2 SHA `140d608be34b35c237fcc11db8c8bd56c5b55c453c9cae921b2c753750887bc6`均为历史，详见[交付记录](<../firmware-delivery/XBOX_CURRENT.md>)。

旧 GBA 功能快照 `abe65ea`、含文档提交 `89fbe9d` 已由用户撤回，仅保留本地历史，不是当前 Xbox 源码或可刷固件；不搬动、不覆盖旧产物。键鼠回退仍为 `28f6c86`，对应 UF2 SHA-256 见手册第 19 章。

## v2.2.9 UPPER 快捷键调整

五项灯控各左移一个物理键：原C/P30键盘RGB_BRD→X/P29；原V/P31触板BL_DEC→C/P30；原B/P32键盘RGB_TOG→V/P31；原N/P33触板BL_INC→B/P32；原M/P34键盘RGB_BRI→N/P33。M/P34改为按住右aA时双击鼠标翻转快捷键；F/P20在UPPER无输出。除此之外没有行为变化。版本日期2026-10-21；完整host、Actions ARM、UF2与文档导出PASS，不代表硬件验收。

## 范围与映射

第 3 层保留名称 MEDIA，使用独立 gamepad behavior，不让模拟器把字母当按钮。v2.2.8 仅将 View 从 T/P11 移到 **R/P10（action12）**、Menu 从 Y/P12 移到 **U/P13（action13）**，原 T/Y 留空；其余保持 v2.2.7：W/P8 上、D/P19 右、Z/P28 下、A/P17 左；S/P18=L3、L/P25=R3，**不是左杆**；双肩 P0/P1=LT/RT，Call/End P2/P6=LB/RB；O/K/Backspace/$=Y/X/B/A、P4 触板机械点击与 P39 空格=Guide。E/P9、X/P29、F/P20 等仍在 MEDIA 留空。仅 P40/P41 两出口；19 个 gamepad 绑定、2 个出口、21 个显式 None，不发字母，非MEDIA层不随此改键。

锁轮默认0°、解锁/重锁回正及肩键半速/自身 wheel 屏蔽均保留；按住 aA 临时恢复原方向和原生肩轮，松开恢复所选固定档。v2.2.8 将累加步进改为**固定档选择**：只在 aA 松开且锁轮生效时，P3黑莓完整三击直接选 **左→上、右→下、上→右、下→左** 档；P5顶部返回完整三击直接选 **右→上、左→下、上→左、下→右** 档。第三次松开直接写入该目标，同键重复幂等，无论原档位都直接选目标；换另一键不是在原档继续转90°。其他点按吞掉，完整三击仍为按住≤180ms、释放至下次按下≤220ms，其他键取消未完成序列。**物理右 aA 按住时 P3/P5 立即 BT_SEL 1/2，即便 scroll latch 已锁定**，不参与固定档选择。

鼠标翻转组合由 v2.2.9 从 **aA 按住+F/P20 双击**迁至 **aA 按住+M/P34 双击**（180/220ms），再做切回；F/P20在UPPER无输出。同槽BT键也取消未完成双击候选，不依赖endpoint change。模式volatile：普通指针Y反向、DEFAULT两肩LCLK/RCLK角色互换；X、滚轮、手柄及其他层行为不变，重启关闭。

为避免第一次 aA tap 的旧 sticky 3秒计时撤销已固定 UPPER，v2.2.7 新增、v2.2.8 保留 `aa_layer` 仲裁 momentary/sticky/fixed owner；`aa_sl` 复用 SDK sticky 的 quick-release/3000ms，但 child 为 `aa_layer` 而非直接 `mo`；`aa_mo` 为450ms hold-preferred，children 为 `aa_layer / aa_sl`。DEFAULT/SYM 的 P41 为 `&aa_mo 0 1`，UPPER P41 为 `&aa_layer 2`；迟到 sticky UP 只释放自己的 owner，不撤 fixed。aA 双击仍每拍<450ms、释放间隔≤350ms；SYM 退出 UPPER 但不清 scroll latch。AA 参数行为须继承 `one_param.yaml` 的 `param1` cell，避免仲裁参数全读0，已有契约assert。pinned SDK sticky/mo/to 三份 Git blob 身份、owner 旧bug/新修复模型对照及 Run39551522029 的完整host/ARM PASS 均是 v2.2.7 历史证据；v2.2.8 完整主机回归现已exit0，真实RTOS/设备验收仍未完成。

[主键图](<../config/bbp9981.keymap>)与[板级镜像](<../config/boards/arm/bbp9981/bbp9981.keymap>)须按 v2.2.8 合同一致：19 个 gamepad 绑定为 `P0=10, P1=11, P2=8, P3=17, P4=14, P6=9, P8(W)=0, P10(R)=12, P13(U)=13, P15(O)=7, P17(A)=2, P18(S)=15, P19(D)=3, P24(K)=6, P25(L)=16, P26(Backspace)=5, P28(Z)=1, P35($)=4, P39(Space)=14`；`P11(T)/P12(Y)=None`，仅 `P40(SYM) / P41(右aA)` 两出口回 DEFAULT，其余 21 个位置显式 None，不透明回落。本版最终云端DTS已逐项核验上述42个MEDIA位置；与v2.2.7相比仅P10–P13变化，其余三个层全部绑定一致；固定档和手势对象实际编译。FLASH306576B/844KB、RAM88862B/256KB，核对JSON及原始日志见本次交付目录，仍非实机验收。

历史 v2.2.7 云端最终 DTS 曾逐项核验其42个MEDIA位置（含当时 T/Y=View/Menu），DEFAULT/SYM右aA为`aa_mo 0 1`、UPPER为`aa_layer 2`且P3/P5/F指向对应手势；aa_sl的child/3000ms quick-release与aa_mo的450ms/children均正确，日志确认新`behavior_aa_layer.c`对象编译。旧 FLASH 306592 B/844 KB、RAM 88862 B/256 KB。原始日志和逐位置核对JSON保留在v2.2.7交付目录；这不是v2.2.8 DTS/资源核验，也非设备全面验收。

### 描述符与报告 ABI

[报告纯核](<../config/boards/arm/bbp9981/custom_driver/gamepad_report.h>)的 ID4 body 为 **9 bytes**，字段顺序固定为 `buttons_lo, buttons_hi, hat, x, y, z, rz, lt, rt`。USB 包含 ID4 共 **10 bytes**；BLE 只发 **9-byte body，不带 ID**。16-bit buttons 只有 13 个真实 Button usages：`1,2,4,5,7,8,9,10,11,12,13,14,15`，对应 A/B/X/Y、LB/RB、LT/RT、View/Menu/Guide、L3/R3；usages 3/6/16 为 Constant，不可宣传 16 个可用按钮。hat 有效 0..7、Null=8，相反方向相消、斜向合并。

四轴 X/Y/Z/Rz 是 signed `[-127,127]`，不再恒零：默认触板驱动左 X/Y，按住黑莓驱动右 Z/Rz。LT/RT 另用 Simulation Controls Brake `C5` / Accelerator `C4`，unsigned `0..255`；物理键只发 0 或 255（0/100%），同时有上述数字按钮位，**不是真实半按扳机**。无振动，未加 Share 截图键；Guide 可能被系统 / 客户端截获，不能保证到达远端。

### velocity 虚拟杆与捕获 owner

[虚拟杆纯核](<../config/boards/arm/bbp9981/custom_driver/gamepad_stick.h>)只用当前 raw 方向 / 速度，不累计虚拟位置。raw 右正、下正，与旧鼠标坐标一致。默认 gain=8、逻辑 deadzone=4，每分量 clamp `[-127,127]`；75% 新样本 + 25% 旧值，以对称舍入处理，反向先清旧分量。最大 raw 从归中起的第一帧滤波后为 ±95，后续连续最大同向样本为 ±119 / ±125 / ±127，不能写首帧满量程。

连续有效位移即使输出值未变也刷新 last-motion；零 / 死区噪声不刷新。最后有效位移后 120ms 本地归零；GPIO / I2C 故障立即清虚拟轴，**不代替任何按钮 UP**。120ms 是本地策略，不是 host ACK、USB / BLE 到达时限或可靠抬指识别。触板物理只有相对光学位移，停住手指不能维持实体杆倾斜：默认左杆要持续前进就须继续滑。用户已明确保留此设计，不得擅改为键盘左杆。

物理黑莓 P3 是按住右杆 modifier，松开不锁定。模式切换先清旧轴，只用切换后的新样本，不转移此前运动。P4 机械触板点击固定输出 Guide，不受黑莓模式影响；空格 P39 也输出 Guide，两者由独立 owner 聚合，松开任一方不误释放另一方。S/L 分别独立输出 L3/R3。独立鼠标翻转标志不参与游戏轴计算。单触板仍不能同时独立控制两杆；触板点击不映射到 L3/R3。

## 接入与隔离

- USB：独立 HID_1，原 HID_0 键鼠的 descriptor、ops、sem、发送源码及 USB epoch 前缀保持隔离；不据此宣称整个触板 driver 未改。配置生成 2 个 HID 实例，原 2 个 CDC 保留，总普通 IN6/OUT2，符合 nRF52840 的 7IN/7OUT 预算；仍待真实枚举验证。POST_KERNEL80 在 device 默认初始化后、USB descriptor fixup 前，仅修改自身公开 interface descriptor 为 non-Boot0/0 并 register/init 一次，不调用第二次 usb_enable。
- USB raw lifecycle：[新前缀](<../config/boards/arm/bbp9981/custom_driver/gamepad_usb_lifecycle.h>)在原epoch前缀之后链接其enable别名，仍只有一次真正enable，原status callback及参数原样传递。HID_1私有flag区分CONFIGURED(0)与非零配置；reset同步清协议状态，SOF不取消输入。未修改原epoch前缀或原键鼠ready判据。
- BLE：仅向固定HOG TU加[手柄前缀](<../config/boards/arm/bbp9981/custom_driver/gamepad_ble_extension.h>)。原Report Map完整前缀后追加Game Pad TLC；独立Input/CCC/Ref尾4attr保持原索引5/9/13。notify使用READ_ENCRYPT的value属性，显式检查captured peer的CCC；不广播、不转投新active peer。读取前后复验peer、路由与epoch，非当前peer只能读neutral。

## RAM与生命周期

USB 私有对齐持久 staging 为 **12 bytes**，但发送长度严格为 **10 bytes**，另有独立 EP0 snapshot；不能把对齐容量当报告长度。IDLE/SUBMITTING/INFLIGHT 覆盖整个 SDK 写入及内部重试，SUBMITTING 期间旧 ready 无权解锁。只在线程上下文、短 IRQ 锁内、保存的真实 IN 端点 nrfx busy=false 时退休 RAM；不猜 0x81，不用 timeout 强解，也不永久 quarantine 键鼠或手柄。busy=false 只证明固定平台的 RAM 不再被读取，不证明 host ACK；已进入 SDK 的包无法撤回，不能承诺切层或路由变化后绝不卡键。

实际[gamepad 运行时](<../config/boards/arm/bbp9981/custom_driver/behavior_gamepad.c>)原有 32 帧 FIFO 与每位置 action owner；release 用原 owner。入 / 退出、输出路由变化清本地 owner、取消旧队列代际并尝试发 neutral；旧目标由 worker 持有 captured conn 引用释放。新目标先 neutral，旧按住键不能因另一键或重连而重播。帧寿命 250ms，旧目标释放最多 40 次 / 400ms；旧目标拒绝释放期间在新主机立即按的首键可能过期，须重按。断线或预算耗尽不能保证旧主机收到 UP。

**历史版本 runtime 主机专项已通过，不作为 v2.2.8 回归结果：** 10000 次 motion 不占离散 FIFO；只在 FIFO 空时捕获最新轴快照，发送前仅刷新四轴，保留按钮 / 帽 / 触发器边沿。测试覆盖同值运动续 quiet、119/120ms、按钮仍按住时归轴零、GPIO/I2C 故障只清轴、BB/R3 owner、旧主机 neutral / 新主机初始化、同 profile 换 conn、producer wake0 不被 retry 覆盖。采样已老于 quiet 时拒绝，锁内 fresh clock 提交避免 getter 先观察新 tick 后把新样本误判倒退；不将此 mock 定时证明扩写为真实 RTOS / 主机到达保证。

普通层也进行首次neutral初始化。安全成功通过原子readiness标志重启短重试预算；安全降级清上下文。CCC聚合值不一定随新peer订阅改变，因此40次短重试后仅以2Hz探测当前peer的全新neutral，不保留旧帧/引用，不反复请求L2，也不恢复取消的held actions。BLE HOG Control Point suspend仍继承stock限制，不宣称本轮新增了完整处理。

## 触板独占与轻拍保护

已加入真实 driver 的游戏层独占分支与鼠标晚发送 gate：MEDIA 时将 raw 运动交给虚拟杆，不同时产鼠标、滚轮或轻拍；入 / 退出或路由变化时取消不能跨上下文的旧队列 / 候选，退出恢复原鼠标路径。原物理 P37 slow、P41 aA 按住滚轮 / 双击锁轮监听仍存在，但游戏层不因此生成鼠标 / 滚轮；P37 不是摇杆加速键。v2.2.7 的 UPPER 由 `aa_layer` 分离临时/一次性/固定持有，SYM（P40）退普通层但不解 scroll latch；MEDIA 仅 P40/P41 两个 DEFAULT 出口，P35($) 在 MEDIA 是 A 按钮。

轻拍仅有 GP 临时阻断与旧候选取消，不调优原 gesture 参数、开机 OFF 或普通层三击开关，不自动改 ON/OFF。触板 driver、microtap runtime、输出 bridge 有 GP 门；A320 的锁轮方向在 v2.2.5–v2.2.7 是累加步进，v2.2.8 改为上文固定档选择，鼠标朝向状态仍独立。切换鼠标朝向时仅重置指针小数并取消旧 microtap 候选；滚轮 remainder 与手柄状态不变。肩键 LCLK/RCLK 角色路由保留按下/抬起 owner，受鼠标模式约束仅 DEFAULT 行为变化。历史 v2.2.6 范围闸曾验证 **45 份旧配置字节相同**；对 A320 精确剥离滚轮锁定、鼠标翻转与 GP 门并归一其历史 CRLF 后，剩余源码还原基线，当时为8个集成、14个新增配置。v2.2.7 新增 aA owner 仲裁后曾重验范围（45旧配置/8集成+16新增）；v2.2.8 已独立重验scope并PASS（45旧配置/8集成+16新增），完整host亦已exit0，70配置/四层图同步PASS，不冒用旧版成功。指针/轻拍数学、bridge I/O / 必要 UP、肩键 Ctrl、灯光、GPIO / 矩阵仍须保留。

历史版本独立只读复核发现并修复一项 P1：已 submit、尚在 QUEUED 的旧轻拍 worker 可早于下次 sensor poll 发 DOWN。现在 GP 生命周期立即 cancel，bridge 分别保存策略 context 与 request 的 pointer epoch，submit / QUEUED / DOWN 最终门拒 active 或旧代际；UP 仍归还自己的一份引用。micro poll 起点捕获 pointer epoch，晚门也识别同 poll enter+exit。新增[真实异步 bridge 组合测试](<../tests/test_gamepad_tap_bridge.c>)已实际通过旧 22 cases（GP 编译但 OFF）及 QUEUED/IDLE gap/最终拒 DOWN/必要 UP/foreign ref 保留用例；[真实 driver 组合测试](<../tests/test_gamepad_driver.c>)覆盖同 poll 往返、原普通模式和退出恢复。独立只读复验确认该 P1 收口；最终检查→SDK hook 仍非原子，已 admitted / 已进入 SDK 的旧包不保证撤回。

## 固定源与构建闸

- ZMK v0.3.0：`edf5c0814fd3ea202e43aad2d68fd32e882a518c`。
- Zephyr：`dacab4875df72109b96cc8977547a0dc04875bcd`。上游v3.5.0+zmk-fixes是可移动分支，本实现不能静默跟随新API。
- hal_nordic：`884c4d61746bc35fbd379c169fc87ddb56c6461d`。
- CMake拒绝不符的Zephyr/HAL版本、相关USB/GATT/nrfx dirty源码，且核验四份ZMK实际文件SHA；真实ARM构建还须核对对应日志与.config。

固定证据：[ZMK HID](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/include/zmk/hid.h>)、[HOG](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/hog.c>)、[Zephyr HID core](<https://github.com/zmkfirmware/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/subsys/usb/device/class/hid/core.c>)、[nRF USB driver](<https://github.com/zmkfirmware/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/drivers/usb/device/usb_dc_nrfx.c>)、[nrfx](<https://github.com/zephyrproject-rtos/hal_nordic/blob/884c4d61746bc35fbd379c169fc87ddb56c6461d/nrfx/drivers/src/nrfx_usbd.c>)。

## 已执行与未执行

**当前 v2.2.8：** 功能源码 SHA `88edf7f323b643465fa1470fdc2e090dd3ee0ffa`、构建提交 `ffc41698f6972f3a26cc4ce459956a06fe8c3a46`，候选分支 `codex/xbox-layout-v2.2.8-20261021`。主任务已回传定向strict UBSan PASS：gesture从全部旧档位绝对选1/3档、同键重复/互切；真实A320联动在两种listener顺序验证两档各四个raw方向、同键重复/交替、aA临时原方向/松手回档且不触BT；真实shoulder preset API覆盖两档×四旧档×16次重复全向量及饱和。scope gate PASS（45旧配置/8集成+16新增）。完整host exit0、70配置/两键图/四层图/手册清单同步PASS；Run39557844223 regression/ARM成功，最终DTS全42个MEDIA位置及其余三层已核对，新UF2边界/SHA验证PASS，不能将这些软件/构建证据当设备验收。未发布/归档、未刷写/实机验收。用户14:33对v2.2.7的有限正面反馈见开头，不等于上述新增语义通过。

**历史 v2.2.7：** 功能快照 `88e744f51e386db853ae25c4438b33cb23557af5`。pinned SDK sticky/mo/to 三份 Git blob 身份对照一致；owner12case含旧bug复现/新修复对照exit0；真实A320/owner/gesture/rotation pipeline两种listener顺序PASS，覆盖超3秒固定层、按住aA切槽的两种松手顺序、解锁重锁回正与F鼠标模式；gamepad专项及scope gate PASS（45份旧配置、8集成+16新增）。完整主机回归exit0，70配置/四层42键图/手册manifest同步PASS。Actions Run39551522029的regression/ARM通过；旧UF2为613376 B/1198 blocks/`[0x1000,0x4be00)`、family `0xada52840`、SHA `140d608be34b35c237fcc11db8c8bd56c5b55c453c9cae921b2c753750887bc6`，本地边界校验PASS，deliver job跳过。该版仍是累加方向/T/Y菜单，不满足v2.2.8合同；交付时未刷未验，14:33后续反馈不追改旧validation记录。

以下为 v2.2.3–v2.2.6 历史验证记录，不作为 v2.2.8 的完整回归、云构建或硬件通过结果；v2.2.6 用户后续实机失败反馈须与历史PASS并列保留：

- [专项 runner](<../tests/run_gamepad_tests.py>)纯核以 strict Wall/Wextra/Werror + UBSan 通过 **13 buttons 的 8192 × 16 方向全组合**、250000 owner 随机边沿、32768 motion 压力；覆盖动作捕获、四轴 / 数字 trigger、虚拟杆过滤与本地归零纯核。不能把纯核 quiet 测试等同于 RTOS 定时发送已验收。
- 新报告 ABI 在 **5 个真实 HOG 配置**及 **USB BOOT0/1 × 10 模式**均已通过。覆盖 BLE 9-byte offsets、USB ID4 共 10 bytes、trigger-only 旧 context 拒发；USB 延迟 DMA 测试消费完整 10 bytes，EP0 snapshot 与中断 TX 隔离，不用 12-byte staging 容量作发送长度。
- USB 既有专项覆盖旧 ready、SUBMITTING 间隙、early completion、reset/abort、非 0x81 端点、原 HID_0 隔离、最终 IRQ 提交前协议 / context 失效、raw CONFIGURED(0) 与复位协议恢复；固定 HOG/USB/USB_HID/HID fixtures 与对应上游 raw 核对证据沿用同批记录。
- v2.2.3 的 runtime、A320/micro 所有权、异步 bridge 与完整主机回归此前以 strict UBSan 通过；v2.2.5 的肩键三击状态机、旋转向量、180/220ms 时序、aA 临时覆盖及默认方向专项 strict UBSan 通过。历史 v2.2.6 的 `python3 -B tests/run_host_tests.py` 在手册同步后 exit 0；包含完整 host suite、68 份配置文件/四层42键图同步、MEDIA 键位契约、UPPER P3/P5 手势和范围闸。以上均为主机软件模型验证，不是 ARM、真实 IRQ/RTOS 或设备验收。
- [范围闸](<../tests/check_tap_successor_scope.py>)实际通过：45 份旧配置字节不动；精确剥离 GP 门与 v2.2.4–v2.2.6 scroll/mouse blocks 后，A320 / microtap / bridge 剩余源码恢复基线；8 个集成文件、14 个新增 gamepad/scroll 配置文件。原 gesture 参数、桥接 I/O / 必要 UP、灯光、GPIO / 矩阵保留。矩阵三键鬼键风险未消除，移动 + modifier + 开火仍需实机测试。
- v2.2.4 E/X/S/F、D/K、P4 Guide、旧锁轮默认方向与肩键逻辑的专项、完整 `run_host_tests.py`、GitHub Actions ARM 构建及 UF2 边界验证均 PASS；它仍是最后已发布 / 归档的 Xbox UF2，但不含 v2.2.5 方向步进。v2.2.5 候选分支 Run 39520450915 的完整主机回归与 `bbp9981` ARM 构建 PASS；本地校验 UF2 为 608256 bytes、1188 blocks、SHA-256 `6b562773e714d564da1b4351db185e63adeefbf3ad6f2139be9311249fde5b5f`。它未归档至发布分支、未刷写，且 v2.2.4 也未 USB / BT 实机枚举、刷写或 App 实玩。旧 Run 39361865259 仅对应 S=↓、Z/K 空的映射，勿用。v2.2.6 候选分支 `codex/xbox-layout-v2.2.6-20261021` 已推送；构建提交 `b05dee65dc929855c809036c539b14488cd7d4b0` 的 [Run 39540020475](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39540020475>) host regression / `bbp9981` ARM PASS。UF2 本地校验 PASS：611328 bytes / 1194 blocks / `[0x1000,0x4ba00)` / SHA-256 `9f4daf7b22ab833c01ed5322115b1a8977cbd4aff8701c04e29ddd73b76d155a`；候选归档 job 跳过，未归档。交付时记录未刷写；用户此后确认已刷v2.2.6且未Studio保存，反馈映射、手势和第三槽入口失灵，不属于硬件验收通过。保留旧校验文件的历史`device_tested=false`，不以清配对/擦设置作为修复。

## 明确不承诺

标准 HID 的 Xbox 布局不等于微软 Xbox 原生 XInput，不保证 Windows XInput-only 游戏或 Xbox 主机可用；不替换用户已选协议。安卓模拟器、Moonlight、盖世、Droidsteam 每个版本 / 平台的识别、映射和串流远端都待实机验证，Droidsteam 准确平台尚未确认。Guide 可能被系统或客户端截获，不能保证传到远端；无 Share、振动或真实模拟半按。

USB 新接口可能改变主机 COM/MI 缓存，BLE Report Map 升级可能受配对缓存影响，不能据此自动清配对 / 擦设置。三键以上物理组合、Studio 持久覆盖、游戏层独占后退出恢复与旧 HID_0 的既有 DMA/sem 风险须分别验收，不能用本轮私有 TX 保证替代。已进入 SDK 的包无法撤回，neutral 只是后续补救；不承诺绝不卡键、零丢键或 120ms 内主机已归中。
