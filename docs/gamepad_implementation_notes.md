# MEDIA / Xbox 布局标准 HID 手柄：实现与验证说明

2026-10-21，与[使用说明书 v2.2.6](<9981Pro_使用说明书.md>)同步。用户已确认 Xbox 布局标准 HID，**不是原生 XInput，不冒充微软 VID/PID**。本轮 v2.2.6 更新 MEDIA 映射、锁轮手势与独立鼠标翻转模式；候选分支构建、UF2 和设备状态以后续验证记录为准，未刷写，见[交付记录](<../firmware-delivery/XBOX_CURRENT.md>)。

旧 GBA 功能快照 `abe65ea`、含文档提交 `89fbe9d` 已由用户撤回，仅保留本地历史，不是当前 Xbox 源码或可刷固件；不搬动、不覆盖旧产物。键鼠回退仍为 `28f6c86`，对应 UF2 SHA-256 见手册第 19 章。

## 范围与映射

第 3 层保留名称 MEDIA；使用独立 gamepad behavior，不让模拟器把字母当按钮。v2.2.4 增加 UPPER 肩键条件 wrapper 与 P41 固定层：锁定时肩键只作半速 modifier 并屏蔽自身 wheel，按住 aA 暂时恢复原方向及原生肩键滚轮；SYM 退出。v2.2.5 锁轮默认不旋转并增加方向三击步进；v2.2.6 将三击从肩键移到物理 P3 黑莓键（左→上）与 P5 顶部返回键（右→上），每次完整三击只步进一步，解锁 / 重锁回正。锁定期间 P3 始终禁选蓝牙槽1；P5 非三击点按吞掉，物理右 aA 按住时 P5 双击转为鼠标模式切换且不选槽2。独立鼠标模式 volatile：普通鼠标指针输出 Y 反向、DEFAULT 肩键 LCLK/RCLK 角色互换；X、滚轮、手柄报告以及 SYM/UPPER/MEDIA 的键位与肩键绑定不变，重启复位。MEDIA 映射：E/X/A/F → D-pad 上/下/左/右；D/L → 独立 L3/R3，**不改为左摇杆**；O/K/Backspace/$ → Y/X/B/A。通话/结束 → LT/RT，双肩 → LB/RB，实体 T/Y → View/Menu。触板机械点击固定为 Guide/Xbox，空格继续为 Guide。仅 P40 SYM / P41 右 aA 退出；19 个 gamepad 绑定、2 个出口、21 个 None；方向、面键、肩与菜单不发字母。

[主键图](<../config/bbp9981.keymap>)与[板级镜像](<../config/boards/arm/bbp9981/bbp9981.keymap>)须一致：19 个 gamepad 绑定为 `P0=8, P1=9, P2=10, P3=17, P4=14, P6=11, P9(E)=0, P11(T)=12, P12(Y)=13, P15(O)=7, P17(A)=2, P19(D)=15, P20(F)=3, P24(K)=6, P25(L)=16, P26(Backspace)=5, P29(X)=1, P35($)=4, P39(Space)=14`；仅 `P40(SYM) / P41(右aA)` 两出口回 DEFAULT，其余 21 个位置显式 None，不透明回落。

### 描述符与报告 ABI

[报告纯核](<../config/boards/arm/bbp9981/custom_driver/gamepad_report.h>)的 ID4 body 为 **9 bytes**，字段顺序固定为 `buttons_lo, buttons_hi, hat, x, y, z, rz, lt, rt`。USB 包含 ID4 共 **10 bytes**；BLE 只发 **9-byte body，不带 ID**。16-bit buttons 只有 13 个真实 Button usages：`1,2,4,5,7,8,9,10,11,12,13,14,15`，对应 A/B/X/Y、LB/RB、LT/RT、View/Menu/Guide、L3/R3；usages 3/6/16 为 Constant，不可宣传 16 个可用按钮。hat 有效 0..7、Null=8，相反方向相消、斜向合并。

四轴 X/Y/Z/Rz 是 signed `[-127,127]`，不再恒零：默认触板驱动左 X/Y，按住黑莓驱动右 Z/Rz。LT/RT 另用 Simulation Controls Brake `C5` / Accelerator `C4`，unsigned `0..255`；物理键只发 0 或 255（0/100%），同时有上述数字按钮位，**不是真实半按扳机**。无振动，未加 Share 截图键；Guide 可能被系统 / 客户端截获，不能保证到达远端。

### velocity 虚拟杆与捕获 owner

[虚拟杆纯核](<../config/boards/arm/bbp9981/custom_driver/gamepad_stick.h>)只用当前 raw 方向 / 速度，不累计虚拟位置。raw 右正、下正，与旧鼠标坐标一致。默认 gain=8、逻辑 deadzone=4，每分量 clamp `[-127,127]`；75% 新样本 + 25% 旧值，以对称舍入处理，反向先清旧分量。最大 raw 从归中起的第一帧滤波后为 ±95，后续连续最大同向样本为 ±119 / ±125 / ±127，不能写首帧满量程。

连续有效位移即使输出值未变也刷新 last-motion；零 / 死区噪声不刷新。最后有效位移后 120ms 本地归零；GPIO / I2C 故障立即清虚拟轴，**不代替任何按钮 UP**。120ms 是本地策略，不是 host ACK、USB / BLE 到达时限或可靠抬指识别。触板物理只有相对光学位移，停住手指不能维持实体杆倾斜：默认左杆要持续前进就须继续滑。用户已明确保留此设计，不得擅改为键盘左杆。

物理黑莓 P3 是按住右杆 modifier，松开不锁定。模式切换先清旧轴，只用切换后的新样本，不转移此前运动。P4 机械触板点击固定输出 Guide，不受黑莓模式影响；空格 P39 也输出 Guide，两者由独立 owner 聚合，松开任一方不误释放另一方。D/L 分别独立输出 L3/R3。独立鼠标翻转标志不参与游戏轴计算。单触板仍不能同时独立控制两杆；触板点击不映射到 L3/R3。

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

已加入真实 driver 的游戏层独占分支与鼠标晚发送 gate：MEDIA 时将 raw 运动交给虚拟杆，不同时产鼠标、滚轮或轻拍；入 / 退出或路由变化时取消不能跨上下文的旧队列 / 候选，退出恢复原鼠标路径。原物理 P37 slow、P41 aA 按住滚轮 / 双击锁轮监听仍存在，但游戏层不因此生成鼠标 / 滚轮；P37 不是摇杆加速键。v2.2.4 的 UPPER P41 改为固定当前层，SYM（P40）是 UPPER 层返回普通层入口；MEDIA 仅 P40/P41 两个 DEFAULT 出口，P35($) 在 MEDIA 是 A 按钮。

轻拍仅新增 GP 临时阻断与旧候选取消，不调优原 gesture 参数、开机 OFF 或普通层三击开关，不自动改 ON/OFF。触板 driver、microtap runtime、输出 bridge 有 GP 门；A320 的 v2.2.5 锁轮方向步进与 v2.2.6 鼠标朝向状态并存，切换朝向时仅重置鼠标指针小数并取消旧 microtap 候选；滚轮 remainder 与手柄状态不变。肩键 LCLK/RCLK 角色路由保留按下/抬起 owner，受鼠标模式约束仅 DEFAULT 行为变化。范围闸已验证 **45 份旧配置字节相同**；对 A320 精确剥离滚轮锁定、鼠标翻转与 GP 门并归一其历史 CRLF 后，剩余源码逐字节还原基线。总计 8 个受限集成文件、14 个新增 gamepad/scroll 配置文件；其余指针/轻拍数学、bridge I/O / 必要 UP、肩键 Ctrl 基线、灯光、GPIO / 矩阵保留。

独立只读复核发现并修复一项 P1：已 submit、尚在 QUEUED 的旧轻拍 worker 可早于下次 sensor poll 发 DOWN。现在 GP 生命周期立即 cancel，bridge 分别保存策略 context 与 request 的 pointer epoch，submit / QUEUED / DOWN 最终门拒 active 或旧代际；UP 仍归还自己的一份引用。micro poll 起点捕获 pointer epoch，晚门也识别同 poll enter+exit。新增[真实异步 bridge 组合测试](<../tests/test_gamepad_tap_bridge.c>)已实际通过旧 22 cases（GP 编译但 OFF）及 QUEUED/IDLE gap/最终拒 DOWN/必要 UP/foreign ref 保留用例；[真实 driver 组合测试](<../tests/test_gamepad_driver.c>)覆盖同 poll 往返、原普通模式和退出恢复。独立只读复验确认该 P1 收口；最终检查→SDK hook 仍非原子，已 admitted / 已进入 SDK 的旧包不保证撤回。

## 固定源与构建闸

- ZMK v0.3.0：`edf5c0814fd3ea202e43aad2d68fd32e882a518c`。
- Zephyr：`dacab4875df72109b96cc8977547a0dc04875bcd`。上游v3.5.0+zmk-fixes是可移动分支，本实现不能静默跟随新API。
- hal_nordic：`884c4d61746bc35fbd379c169fc87ddb56c6461d`。
- CMake拒绝不符的Zephyr/HAL版本、相关USB/GATT/nrfx dirty源码，且核验四份ZMK实际文件SHA；真实ARM构建还须核对对应日志与.config。

固定证据：[ZMK HID](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/include/zmk/hid.h>)、[HOG](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/hog.c>)、[Zephyr HID core](<https://github.com/zmkfirmware/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/subsys/usb/device/class/hid/core.c>)、[nRF USB driver](<https://github.com/zmkfirmware/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/drivers/usb/device/usb_dc_nrfx.c>)、[nrfx](<https://github.com/zephyrproject-rtos/hal_nordic/blob/884c4d61746bc35fbd379c169fc87ddb56c6461d/nrfx/drivers/src/nrfx_usbd.c>)。

## 已执行与未执行

以下区分已发布 v2.2.4、未发布 v2.2.5 历史候选与当前未发布 v2.2.6 候选的主机 / ARM / UF2 验证；v2.2.6 主机与 Actions ARM / UF2 本地边界校验已通过，均不代替真实设备验收：

- [专项 runner](<../tests/run_gamepad_tests.py>)纯核以 strict Wall/Wextra/Werror + UBSan 通过 **13 buttons 的 8192 × 16 方向全组合**、250000 owner 随机边沿、32768 motion 压力；覆盖动作捕获、四轴 / 数字 trigger、虚拟杆过滤与本地归零纯核。不能把纯核 quiet 测试等同于 RTOS 定时发送已验收。
- 新报告 ABI 在 **5 个真实 HOG 配置**及 **USB BOOT0/1 × 10 模式**均已通过。覆盖 BLE 9-byte offsets、USB ID4 共 10 bytes、trigger-only 旧 context 拒发；USB 延迟 DMA 测试消费完整 10 bytes，EP0 snapshot 与中断 TX 隔离，不用 12-byte staging 容量作发送长度。
- USB 既有专项覆盖旧 ready、SUBMITTING 间隙、early completion、reset/abort、非 0x81 端点、原 HID_0 隔离、最终 IRQ 提交前协议 / context 失效、raw CONFIGURED(0) 与复位协议恢复；固定 HOG/USB/USB_HID/HID fixtures 与对应上游 raw 核对证据沿用同批记录。
- v2.2.3 的 runtime、A320/micro 所有权、异步 bridge 与完整主机回归此前以 strict UBSan 通过；v2.2.5 的肩键三击状态机、旋转向量、180/220ms 时序、aA 临时覆盖及默认方向专项 strict UBSan 通过。当前 v2.2.6 的 `python3 -B tests/run_host_tests.py` 在手册同步后 exit 0；包含完整 host suite、68 份配置文件/四层42键图同步、MEDIA 键位契约、UPPER P3/P5 手势和范围闸。以上均为主机软件模型验证，不是 ARM、真实 IRQ/RTOS 或设备验收。
- [范围闸](<../tests/check_tap_successor_scope.py>)实际通过：45 份旧配置字节不动；精确剥离 GP 门与 v2.2.4–v2.2.6 scroll/mouse blocks 后，A320 / microtap / bridge 剩余源码恢复基线；8 个集成文件、14 个新增 gamepad/scroll 配置文件。原 gesture 参数、桥接 I/O / 必要 UP、灯光、GPIO / 矩阵保留。矩阵三键鬼键风险未消除，移动 + modifier + 开火仍需实机测试。
- v2.2.4 E/X/S/F、D/K、P4 Guide、旧锁轮默认方向与肩键逻辑的专项、完整 `run_host_tests.py`、GitHub Actions ARM 构建及 UF2 边界验证均 PASS；它仍是最后已发布 / 归档的 Xbox UF2，但不含 v2.2.5 方向步进。v2.2.5 候选分支 Run 39520450915 的完整主机回归与 `bbp9981` ARM 构建 PASS；本地校验 UF2 为 608256 bytes、1188 blocks、SHA-256 `6b562773e714d564da1b4351db185e63adeefbf3ad6f2139be9311249fde5b5f`。它未归档至发布分支、未刷写，且 v2.2.4 也未 USB / BT 实机枚举、刷写或 App 实玩。旧 Run 39361865259 仅对应 S=↓、Z/K 空的映射，勿用。v2.2.6 候选分支 `codex/xbox-layout-v2.2.6-20261021` 已推送；构建提交 `b05dee65dc929855c809036c539b14488cd7d4b0` 的 [Run 39540020475](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39540020475>) host regression / `bbp9981` ARM PASS。UF2 本地校验 PASS：611328 bytes / 1194 blocks / `[0x1000,0x4ba00)` / SHA-256 `9f4daf7b22ab833c01ed5322115b1a8977cbd4aff8701c04e29ddd73b76d155a`；候选归档 job 跳过，未归档、未刷写，无清配对或擦设置。

## 明确不承诺

标准 HID 的 Xbox 布局不等于微软 Xbox 原生 XInput，不保证 Windows XInput-only 游戏或 Xbox 主机可用；不替换用户已选协议。安卓模拟器、Moonlight、盖世、Droidsteam 每个版本 / 平台的识别、映射和串流远端都待实机验证，Droidsteam 准确平台尚未确认。Guide 可能被系统或客户端截获，不能保证传到远端；无 Share、振动或真实模拟半按。

USB 新接口可能改变主机 COM/MI 缓存，BLE Report Map 升级可能受配对缓存影响，不能据此自动清配对 / 擦设置。三键以上物理组合、Studio 持久覆盖、游戏层独占后退出恢复与旧 HID_0 的既有 DMA/sem 风险须分别验收，不能用本轮私有 TX 保证替代。已进入 SDK 的包无法撤回，neutral 只是后续补救；不承诺绝不卡键、零丢键或 120ms 内主机已归中。
