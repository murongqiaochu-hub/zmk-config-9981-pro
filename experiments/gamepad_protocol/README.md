# 纯手柄协议实验（未接入固件）

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

## 明确未完成

- 不改变9981厂商/产品身份；没有复制Google/Microsoft VID/PID。
- 无广播/身份切换、BT回调注册、生产worker接线、模式选择及持久化。新增桥接的SDK/runtime仍在测试中替身化，尚非完整生产运行链。
- Control Point已接实验中的每peer挂起状态，但真实ATT派发、suspend抑制通知及重连行为仍待集成/实机验证。
- 未做ARM编译、空口测试、GCController枚举或Moonlight验收。
- 本目录不是可刷固件，不能称Stadia认证、Xbox原生兼容或iPad手柄已恢复。
- 原型没有用户功能变更，因此未修改正式使用说明书或生成新固件手册；进入生产候选时必须同批更新。

## 下一阶段闸门

1. 将已验证的实验桥接接到真实生产worker及BT生命周期回调，继续验证旧peer释放、断开/重连、订阅变化、suspend/resume及发送失败；不能用当前边界mock替代这一步。
2. 服务呈现只能在建立连接前确定，不能在同一Read Blob过程中或仅随活动槽切换；不自动清配对，不在后台更换身份。
3. 新模式保留现有键位、灯光、速度与键鼠回退，不以全局重构冒险。先确定纯手柄的固定构建/模式隔离，再做三合一。
4. 与参照协议一致的数据不保证系统驱动匹配；如需改变厂商身份，必须先说明原因并取得用户明确选择，不能偷偷替换。
5. 目标验收分开记录：服务/Map → 每peer CCC订阅 → GCController.extendedGamepad → 按钮与轴事件 → 官方Moonlight实玩。安卓同样区分系统输入与盖世游戏测试结果。
