# MEDIA / Xbox 布局标准 HID 手柄：实现与验证说明

2026-10-24，与[使用说明书 v2.2.1.1](<9981Pro_使用说明书.md>)同步。固件是保留全部原 v2.2.1 功能的 iPad 元数据兼容候选；标准 HID，**不是原生 XInput，不冒充微软 VID/PID**。本轮仅修 HID Information、PnP 来源及标准 DIS 软件版本，功能源 `f1d862ffc22ad7371b5a59471b11e086764cf5d6`；尚未证明 iPad 可用，详见[本轮对比与修正说明](<ipad_hid_metadata.md>)。以下保留原 v2.2.1 的实现与既有主机验证记录，不把旧 PASS 当成本轮结果；本轮新增真实 HID 16组合 UBSan＋ASan、特定 OFF 负例、旧范围闸与完整主机回归均 PASS（exit0）；ARM/UF2 待执行。

旧 GBA 功能快照 `abe65ea`、含文档提交 `89fbe9d` 已由用户撤回，仅保留本地历史，不是当前 Xbox 源码或可刷固件；不搬动、不覆盖旧产物。键鼠回退仍为 `28f6c86`，对应 UF2 SHA-256 见手册第 19 章。

## 范围与映射

第 3 层保留名称 MEDIA；使用独立 gamepad behavior，不让模拟器把字母当按钮。默认、SYM、UPPER 各 42 键绑定与既有操作参数保留，只改 MEDIA 及必要 gamepad 保护 / 路由分支。W/A/S/D → D-pad 上/左/下/右，**不改为左摇杆**；I/J/L/M → Y/X/B/A。通话/结束 → LB/RB，双肩 → LT/RT，实体 T/Y → View/Back（选择）/Menu/Start（菜单），空格 → Guide/Xbox。原 J/K＝B/A 已废止，K、N、Enter 等留空；方向、面键、肩与菜单不发字母。

[主键图](<../config/bbp9981.keymap>)与[板级镜像](<../config/boards/arm/bbp9981/bbp9981.keymap>)须一致：17 个 gamepad 绑定为 `P0=10, P1=11, P2=8, P3=17, P4=18, P6=9, P8=0, P11=12, P12=13, P14=7, P17=2, P18=1, P19=3, P23=6, P25=5, P34=4, P39=14`；`P35($) / P40(SYM) / P41(右aA)` 三出口都是回 DEFAULT，其余 22 个位置显式 None，不透明回落。

### 描述符与报告 ABI

[报告纯核](<../config/boards/arm/bbp9981/custom_driver/gamepad_report.h>)的 ID4 body 为 **9 bytes**，字段顺序固定为 `buttons_lo, buttons_hi, hat, x, y, z, rz, lt, rt`。USB 包含 ID4 共 **10 bytes**；BLE 只发 **9-byte body，不带 ID**。16-bit buttons 只有 13 个真实 Button usages：`1,2,4,5,7,8,9,10,11,12,13,14,15`，对应 A/B/X/Y、LB/RB、LT/RT、View/Menu/Guide、L3/R3；usages 3/6/16 为 Constant，不可宣传 16 个可用按钮。hat 有效 0..7、Null=8，相反方向相消、斜向合并。

四轴 X/Y/Z/Rz 是 signed `[-127,127]`，不再恒零：默认触板驱动左 X/Y，按住黑莓驱动右 Z/Rz。LT/RT 另用 Simulation Controls Brake `C5` / Accelerator `C4`，unsigned `0..255`；物理键只发 0 或 255（0/100%），同时有上述数字按钮位，**不是真实半按扳机**。无振动，未加 Share 截图键；Guide 可能被系统 / 客户端截获，不能保证到达远端。

### velocity 虚拟杆与捕获 owner

[虚拟杆纯核](<../config/boards/arm/bbp9981/custom_driver/gamepad_stick.h>)只用当前 raw 方向 / 速度，不累计虚拟位置。raw 右正、下正，与旧鼠标坐标一致。默认 gain=8、逻辑 deadzone=4，每分量 clamp `[-127,127]`；75% 新样本 + 25% 旧值，以对称舍入处理，反向先清旧分量。最大 raw 从归中起的第一帧滤波后为 ±95，后续连续最大同向样本为 ±119 / ±125 / ±127，不能写首帧满量程。

连续有效位移即使输出值未变也刷新 last-motion；零 / 死区噪声不刷新。最后有效位移后 120ms 本地归零；GPIO / I2C 故障立即清虚拟轴，**不代替任何按钮 UP**。120ms 是本地策略，不是 host ACK、USB / BLE 到达时限或可靠抬指识别。触板物理只有相对光学位移，停住手指不能维持实体杆倾斜：默认左杆要持续前进就须继续滑。用户已明确保留此设计，不得擅改为 WASD 左杆。

物理黑莓 P3 是按住右杆 modifier，松开不锁定。模式切换先清旧轴，只用切换后的新样本，不转移此前运动。P4 机械触板按压在 DOWN 时按当前模式捕获 L3 或 R3，按住中途切模式不换按钮，UP 释放原 owner；重复 DOWN 去重。单触板不能同时独立控制两杆，共享机械按键也不能同时按 L3/R3，若确有需求须另行改键。

## 接入与隔离

- USB：独立 HID_1，原 HID_0 键鼠的 descriptor、ops、sem、发送源码及 USB epoch 前缀保持隔离；不据此宣称整个触板 driver 未改。配置生成 2 个 HID 实例，原 2 个 CDC 保留，总普通 IN6/OUT2，符合 nRF52840 的 7IN/7OUT 预算；仍待真实枚举验证。POST_KERNEL80 在 device 默认初始化后、USB descriptor fixup 前，仅修改自身公开 interface descriptor 为 non-Boot0/0 并 register/init 一次，不调用第二次 usb_enable。
- USB raw lifecycle：[新前缀](<../config/boards/arm/bbp9981/custom_driver/gamepad_usb_lifecycle.h>)在原epoch前缀之后链接其enable别名，仍只有一次真正enable，原status callback及参数原样传递。HID_1私有flag区分CONFIGURED(0)与非零配置；reset同步清协议状态，SOF不取消输入。未修改原epoch前缀或原键鼠ready判据。
- BLE：仅向固定HOG TU加[手柄前缀](<../config/boards/arm/bbp9981/custom_driver/gamepad_ble_extension.h>)。原Report Map完整前缀后追加Game Pad TLC；独立Input/CCC/Ref尾4attr保持原索引5/9/13。notify使用READ_ENCRYPT的value属性，显式检查captured peer的CCC；不广播、不转投新active peer。读取前后复验peer、路由与epoch，非当前peer只能读neutral。

## RAM与生命周期

USB 私有对齐持久 staging 为 **12 bytes**，但发送长度严格为 **10 bytes**，另有独立 EP0 snapshot；不能把对齐容量当报告长度。IDLE/SUBMITTING/INFLIGHT 覆盖整个 SDK 写入及内部重试，SUBMITTING 期间旧 ready 无权解锁。只在线程上下文、短 IRQ 锁内、保存的真实 IN 端点 nrfx busy=false 时退休 RAM；不猜 0x81，不用 timeout 强解，也不永久 quarantine 键鼠或手柄。busy=false 只证明固定平台的 RAM 不再被读取，不证明 host ACK；已进入 SDK 的包无法撤回，不能承诺切层或路由变化后绝不卡键。

实际[gamepad 运行时](<../config/boards/arm/bbp9981/custom_driver/behavior_gamepad.c>)原有 32 帧 FIFO 与每位置 action owner；release 用原 owner。入 / 退出、输出路由变化清本地 owner、取消旧队列代际并尝试发 neutral；旧目标由 worker 持有 captured conn 引用释放。新目标先 neutral，旧按住键不能因另一键或重连而重播。帧寿命 250ms，旧目标释放最多 40 次 / 400ms；旧目标拒绝释放期间在新主机立即按的首键可能过期，须重按。断线或预算耗尽不能保证旧主机收到 UP。

**新增 runtime 主机专项已通过：** 10000 次 motion 不占离散 FIFO；只在 FIFO 空时捕获最新轴快照，发送前仅刷新四轴，保留按钮 / 帽 / 触发器边沿。测试覆盖同值运动续 quiet、119/120ms、按钮仍按住时归轴零、GPIO/I2C 故障只清轴、BB/R3 owner、旧主机 neutral / 新主机初始化、同 profile 换 conn、producer wake0 不被 retry 覆盖。采样已老于 quiet 时拒绝，锁内 fresh clock 提交避免 getter 先观察新 tick 后把新样本误判倒退；不将此 mock 定时证明扩写为真实 RTOS / 主机到达保证。

普通层也进行首次neutral初始化。安全成功通过原子readiness标志重启短重试预算；安全降级清上下文。CCC聚合值不一定随新peer订阅改变，因此40次短重试后仅以2Hz探测当前peer的全新neutral，不保留旧帧/引用，不反复请求L2，也不恢复取消的held actions。BLE HOG Control Point suspend仍继承stock限制，不宣称本轮新增了完整处理。

## 触板独占与轻拍保护

已加入真实 driver 的游戏层独占分支与鼠标晚发送 gate：MEDIA 时将 raw 运动交给虚拟杆，不同时产鼠标、滚轮或轻拍；入 / 退出或路由变化时取消不能跨上下文的旧队列 / 候选，退出恢复原鼠标路径。原物理 P37 slow、P41 按住滚轮 / 双击锁轮监听仍存在，但游戏层不因此生成鼠标 / 滚轮；P37 不是摇杆加速键。P35 / P40 / P41 的三个层出口仍回 DEFAULT。

轻拍仅新增 GP 临时阻断与旧候选取消，不调优原 gesture 参数、开机 OFF 或普通层三击开关，不自动改 ON/OFF。触板 driver、microtap runtime 和真实输出 bridge 均有必要的 GP 门，不能称整文件未改。范围闸已验证 **46 份旧配置字节相同**，这三份接入源去掉 GP-only 块并归一 A320 历史 CRLF 后与基线精确相同；原桥接 I/O / UP、肩键 / Ctrl、灯光、GPIO / 矩阵与指针数学保留。

独立只读复核发现并修复一项 P1：已 submit、尚在 QUEUED 的旧轻拍 worker 可早于下次 sensor poll 发 DOWN。现在 GP 生命周期立即 cancel，bridge 分别保存策略 context 与 request 的 pointer epoch，submit / QUEUED / DOWN 最终门拒 active 或旧代际；UP 仍归还自己的一份引用。micro poll 起点捕获 pointer epoch，晚门也识别同 poll enter+exit。新增[真实异步 bridge 组合测试](<../tests/test_gamepad_tap_bridge.c>)已实际通过旧 22 cases（GP 编译但 OFF）及 QUEUED/IDLE gap/最终拒 DOWN/必要 UP/foreign ref 保留用例；[真实 driver 组合测试](<../tests/test_gamepad_driver.c>)覆盖同 poll 往返、原普通模式和退出恢复。独立只读复验确认该 P1 收口；最终检查→SDK hook 仍非原子，已 admitted / 已进入 SDK 的旧包不保证撤回。

## 固定源与构建闸

- ZMK v0.3.0：`edf5c0814fd3ea202e43aad2d68fd32e882a518c`。
- Zephyr：`dacab4875df72109b96cc8977547a0dc04875bcd`。上游v3.5.0+zmk-fixes是可移动分支，本实现不能静默跟随新API。
- hal_nordic：`884c4d61746bc35fbd379c169fc87ddb56c6461d`。
- CMake拒绝不符的Zephyr/HAL版本、相关USB/GATT/nrfx dirty源码，且核验四份ZMK实际文件SHA；真实ARM构建还须核对对应日志与.config。

固定证据：[ZMK HID](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/include/zmk/hid.h>)、[HOG](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/hog.c>)、[Zephyr HID core](<https://github.com/zmkfirmware/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/subsys/usb/device/class/hid/core.c>)、[nRF USB driver](<https://github.com/zmkfirmware/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/drivers/usb/device/usb_dc_nrfx.c>)、[nrfx](<https://github.com/zephyrproject-rtos/hal_nordic/blob/884c4d61746bc35fbd379c169fc87ddb56c6461d/nrfx/drivers/src/nrfx_usbd.c>)。

## 已执行与未执行

以下为本任务已实际执行的主机软件验证；完整回归亦已通过，均不代替 ARM / 设备验收：

- [专项 runner](<../tests/run_gamepad_tests.py>)纯核以 strict Wall/Wextra/Werror + UBSan 通过 **13 buttons 的 8192 × 16 方向全组合**、250000 owner 随机边沿、32768 motion 压力；覆盖动作捕获、四轴 / 数字 trigger、虚拟杆过滤与本地归零纯核。不能把纯核 quiet 测试等同于 RTOS 定时发送已验收。
- 新报告 ABI 在 **5 个真实 HOG 配置**及 **USB BOOT0/1 × 10 模式**均已通过。覆盖 BLE 9-byte offsets、USB ID4 共 10 bytes、trigger-only 旧 context 拒发；USB 延迟 DMA 测试消费完整 10 bytes，EP0 snapshot 与中断 TX 隔离，不用 12-byte staging 容量作发送长度。
- USB 既有专项覆盖旧 ready、SUBMITTING 间隙、early completion、reset/abort、非 0x81 端点、原 HID_0 隔离、最终 IRQ 提交前协议 / context 失效、raw CONFIGURED(0) 与复位协议恢复；固定 HOG/USB/USB_HID/HID fixtures 与对应上游 raw 核对证据沿用同批记录。
- 实际 runtime、新 A320/micro 所有权与真实异步 bridge 专项均以 strict UBSan 实际通过；原 micro / pointer / wheel / bridge suite 还在 GP 已编译但普通模式下执行通过。它们是软件模型验证，不是 ARM、真实 IRQ/RTOS 或设备验收。完整主机回归已实际通过，覆盖旧键鼠 / 灯光 / 持久化 / SDK 原源回放 / 矩阵 / 文档同步及新手柄全部专项。
- [范围闸](<../tests/check_tap_successor_scope.py>)实际通过：46 份旧配置字节不动、3 份接入源恢复核对、8 个既有配置接入点及 9 份新增 GP 配置源；旧 gesture 参数、桥接 I/O / UP、灯光、GPIO / 矩阵均保留。矩阵三键鬼键风险未消除，移动 + modifier + 开火仍需实机测试。
- 原始 v2.2.1 完整主机回归和[同步检查](<../tests/check_manual_sync.py>)当时已通过；其后原版构建并获用户反馈 iPad 失败，不能继续沿用“尚未构建”的历史状态。本轮必须单独重跑完整回归、真实 map 元数据专项与 ARM/UF2 校验；最新结果见[本轮记录](<ipad_hid_metadata.md>)。未获刷写、清配对、擦 NVS 或改主机设置授权。

## 明确不承诺

标准 HID 的 Xbox 布局不等于微软 Xbox 原生 XInput，不保证 Windows XInput-only 游戏或 Xbox 主机可用；不替换用户已选协议。安卓模拟器、Moonlight、盖世、Droidsteam 每个版本 / 平台的识别、映射和串流远端都待实机验证，Droidsteam 准确平台尚未确认。Guide 可能被系统或客户端截获，不能保证传到远端；无 Share、振动或真实模拟半按。

USB 新接口可能改变主机 COM/MI 缓存，BLE Report Map 升级可能受配对缓存影响，不能据此自动清配对 / 擦设置。三键以上物理组合、Studio 持久覆盖、游戏层独占后退出恢复与旧 HID_0 的既有 DMA/sem 风险须分别验收，不能用本轮私有 TX 保证替代。已进入 SDK 的包无法撤回，neutral 只是后续补救；不承诺绝不卡键、零丢键或 120ms 内主机已归中。
