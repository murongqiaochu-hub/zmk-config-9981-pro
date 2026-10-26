# 纯手柄协议实验与显式诊断候选（ARM静态产物已复核，未实机）

**最新状态：** Run 40300798791 整体success：完整host回归、固定SDK真实ARM编译/链接及后置ELF/UF2验收全部通过，下载后复核一致。其UF2与第2次产物逐字节相同；此前失败来自验收器的SDK回调命名及填充区假设，固件代码无需修复。唯一HIDS/16属性、Map/ReportRef、回调iterable和三TU编译命令均符合预期，UF2实际文件节载荷与ELF一致。已用尽3/3次授权，未刷写、未空口/系统/App验收。以下前四个里程碑保留**各阶段当时**的证据范围。

更新：2026-10-26。目标设备：iPad Air 6 / iPadOS 26.2 / 官方 Moonlight。用户接受先分模式验证，再推进键鼠共存。

## 当前能证明什么

- 现有9981逻辑手柄状态可以确定地转换为参照协议的10字节输入载荷。
- 182字节描述符只有一个Game Pad顶层集合；输入ID3为80bit，输出ID5为32bit。
- 实验GATT表有16个属性，提供相应输入、CCC、两个Report Reference、输出、HID信息、Report Map、Report Protocol及Control Point；描述符/实际引用一一对应。
- Map不可变，不根据活动蓝牙槽切换。BLE input只返回10字节body，ID由Report Reference传递。
- 四字节输出报告接受但忽略，没有物理振动实现，也不向另一peer回显之前的输出。
- 捕获状态失败、非当前peer、无效hat/轴输入时返回归中报告；新增连接/epoch桥接已在mock SDK/runtime边界验证，但尚未接入生产worker。
- protocol只接受Report模式1；控制点只接受0/1，按调用连接传给外部挂起接口；PREPARE/EXECUTE/未知写flags拒绝，普通Write/Write Command允许。
- 诊断接口只传操作类型、偏移、返回值；不传按键载荷、MAC或配对资料。CCC回调值是聚合状态，不能冒充某个peer已订阅。

## 文件与来源

- [数据转换](<stadia_codec.h>)：独立字节编码，不依赖C bitfield布局。
- [描述符](<stadia_descriptor.h>)：来自固定上游main.c的原始字节，SHA-256 `4317e34b66d9c247b0fb12327516c38c74deea04619db54a05cf190444482a3f`。
- [实验GATT](<pure_hids.h>)：本地隔离实现，未接入生产CMake。
- 上游：<https://github.com/jfedor2/portable-gamepad-firmware/tree/aa09d1d05939ab0025107f6df359e0b8a3b714c0>。
- 上游MIT许可完整保存在 [UPSTREAM_LICENSE](<UPSTREAM_LICENSE>)。

上游纯Zephyr HIDS实现只提供输入报告，而所参考Stadia描述符还声明ID5输出。这里显式提供输出特性，不用“其他项目这么写”替代Map/GATT一致性检查。这不证明该差异是iOS识别的决定因素。

## 验证

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -B tests/run_pure_hids_tests.py
PYTHONDONTWRITEBYTECODE=1 python3 -B tests/run_gamepad_tests.py
PYTHONDONTWRITEBYTECODE=1 python3 -B tests/run_map_stability_counterfactual.py
```

从工作树根目录运行。纯HIDS测试以固定Zephyr GATT宏mock编译真实实验表；独立HID解析器核对应用集合、ID/类型、位长、usage/offset/range。实际GATT读取一个已知按钮/轴向量，再由描述符独立解码，避免仅用同一encoder生成expected而自证。

反例必须失败：缺ID5 Output引用、多余LED引用、错误Input ID、重复引用、hat位宽变化、output位宽变化、button usage变化。额外覆盖全部write flags、空buffer合法/非法长度、读取长度与偏移边界、五类无效状态归中，以及诊断回调的偏移/结果。

主机测试中CCC读写明确返回ENOTSUP，不假装实现完整ATT协议栈。加密权限是静态配置断言，未证明实际未加密请求被拒绝。

## 连接、发送与诊断桥接（第三个本地里程碑）

新增 [pure_transport.h](<pure_transport.h>)，接口对齐既有生产worker的 `captured_conn/body/context_epoch`，没有新增队列或重试策略：

- 每个已登记连接持有一份引用，容量受 `CONFIG_BT_MAX_CONN` 限制；重复连接、满表、断开、登记时交错断开均验证引用平衡。失败登记只清理本次registration，不误删同指针的新代。
- 本地generation跨槽唯一递增，suspend/resume与每peer CCC有效写均取消旧状态；同值CCC写也保守取消。32位代次不是无穷期防回绕保证。
- GATT用managed CCC的 `cfg_write`，在SDK提交前观察每个peer的0/1写；它只取消、不宣称已订阅。实际发送仍对captured conn调用 `bt_gatt_is_subscribed`。两peer的A关闭/重开、B保持订阅场景已覆盖；改回仅aggregate hook后，反事实必须在旧epoch未变化断言失败。
- 非neutral发送要求当前peer/路由/runtime epoch与本地ticket均有效；neutral可释放旧captured peer，不能广播到新peer，但仍受连接、挂起和订阅闸门限制。无法向断开的主机保证松键。
- 最终闸门之后的取消可能仍有**一帧已进入SDK的在途提交**。测试明确覆盖这个边界；不在spinlock内调用SDK，不声称检查与空口提交完全原子。现有worker后续取消/归中仍需真实集成验证。
- notify回传SDK错误，不新增无限重试。固定SDK在函数返回前将载荷复制进ATT buffer，局部10字节编码缓冲区不会作为异步借用指针保存；源码证据见下方链接。
- 诊断实现为固定RAM数组、饱和计数、最近offset/result；不写flash/settings、日志或文件，不存按钮载荷或设备标识。诊断导出尚未实现。

验证：strict UBSan专项覆盖10000次生命周期、同指针跨槽重连、旧peer释放、订阅/取状态/最后info检查时取消，及明确允许的一帧在途边界。完整主机回归exit0（任务日志09），仍无ARM/真实SDK派发/空口结果。

固定SDK证据：<https://github.com/zephyrproject-rtos/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/subsys/bluetooth/host/gatt.c>。`gatt_ccc_changed`仅在聚合最大值变化时通知；`cfg_write`返回1/2才提交；`gatt_notify`先检查链路加密，再复制载荷到PDU；再次强制订阅取决 `CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION`，不能默认假定始终开启。

## 显式候选接线（第四个本地里程碑）

默认 `config` 仍未改。新增 [候选生成器](<../../tools/stage_pure_gamepad.py>)，仅在新目录复制源码后机械修改两份CMake：

1. tap的stock HOG prefix替换为 [pure_hog_guard.h](<pure_hog_guard.h>)：旧HID表变为普通const对象，不进入GATT iterable表；蓝牙键盘/consumer/鼠标公共发送与mouse snapshot API返回ENOTSUP，不排队、不清除调用方鼠标数据。旧SDK TU保持原字节、旧实现改名但不从公共入口调用。
2. gamepad BLE extension替换为独立TU [pure_runtime.c](<pure_runtime.c>)：注册纯HIDS和连接/断开回调，导出既有 `bbp_gamepad_ble_send` ABI接到实验transport，并导出有限诊断快照getter。

生成候选不改变USB源码/配置、设备身份、键图、设置项或板定义；不是一个自动切换模式的新UI。必须使用已有控制选择BLE输出与MEDIA层。这个诊断profile的**蓝牙键鼠不可用**，USB源码/接口保留但仍需ARM和设备回归；不可直接作为全端三合一交付。

```bash
python3 -B tools/stage_pure_gamepad.py --out /tmp/9981-pure-stage --accept-ble-keymouse-disabled
```

目标必须是源码树之外的新目录，已有目录拒绝覆盖。生成前验证精确源片段和182字节描述符哈希；生成后manifest记录每个文件哈希、两项修改前后hash、source commit/dirty状态，build/flashed/hardware均为false。不运行west、Actions或设备命令。

新增验证：未修改的固定SDK hog.c在5种host配置下编译，公共键鼠接口均拒发；native ELF iterable marker证实旧service不注册、新service使用注册宏，恢复旧service注册的反例可检测。实际wrapper/callback函数已执行，但生产worker本体在该测试中仍是mock边界，**不是三个真实TU独立链接证明**。

本轮临时准备CMake 3.31.6（仅 `/dev/shm` 测试目录，没有全局安装或PATH配置）。真实CMake解释器执行生成后board逻辑：16组前置条件、唯一guard/source接线、USB属性轨迹不变、5类SDK身份与4类SHA失败闸；原32组回归矩阵也执行而非SKIP。这里Zephyr函数与git身份探测仍为recording/stub harness，**不是完整Zephyr configure/ARM构建**。完整host日志12 exit0。

## 明确未完成

- 不改变9981厂商/产品身份；没有复制Google/Microsoft VID/PID。
- 显式候选已有BT回调和生产send ABI接线，且真实ARM三TU编译/链接、最终ELF已复核；尚无实际广播/连接验收、身份切换、运行时模式选择及持久化。host竞态测试仍使用SDK/runtime替身，不等于运行链设备验收。
- Control Point已接实验中的每peer挂起状态，但真实ATT派发、suspend抑制通知及重连行为仍待集成/实机验证。
- 未做空口测试、GCController枚举或Moonlight验收；ARM成功不能替代它们。
- 本目录不是可刷固件，不能称Stadia认证、Xbox原生兼容或iPad手柄已恢复。
- 已生成诊断UF2并完成静态产物复核，尚未刷写/实机验收。[P1补充说明](<../../docs/9981Pro_纯BLE诊断候选说明.md>)同批维护和导出，原默认手册不能冒充纯BLE模式说明；任何设备动作仍需用户明确决定。

## 下一阶段闸门

1. 本轮3次授权构建额度已用尽，最后一次为Run 40300798791（结果以[构建记录](<../../docs/cross_platform_hid_reassessment.md>)为准），不可自行再次dispatch或rerun。之后须经设备授权验证真实worker的旧peer释放、断开/重连、订阅变化、suspend/resume及发送失败；不能用host模型或静态链接替代。
2. 服务呈现只能在建立连接前确定，不能在同一Read Blob过程中或仅随活动槽切换；不自动清配对，不在后台更换身份。
3. 新模式保留现有键位、灯光、速度与键鼠回退，不以全局重构冒险。先确定纯手柄的固定构建/模式隔离，再做三合一。
4. 与参照协议一致的数据不保证系统驱动匹配；如需改变厂商身份，必须先说明原因并取得用户明确选择，不能偷偷替换。
5. 目标验收分开记录：服务/Map → 每peer CCC订阅 → GCController.extendedGamepad → 按钮与轴事件 → 官方Moonlight实玩。安卓同样区分系统输入与盖世游戏测试结果。
