# 纯手柄协议实验（未接入固件）

更新：2026-10-26。目标设备：iPad Air 6 / iPadOS 26.2 / 官方 Moonlight。用户接受先分模式验证，再推进键鼠共存。

## 当前能证明什么

- 现有9981逻辑手柄状态可以确定地转换为参照协议的10字节输入载荷。
- 182字节描述符只有一个Game Pad顶层集合；输入ID3为80bit，输出ID5为32bit。
- 实验GATT表有16个属性，提供相应输入、CCC、两个Report Reference、输出、HID信息、Report Map、Report Protocol及Control Point；描述符/实际引用一一对应。
- Map不可变，不根据活动蓝牙槽切换。BLE input只返回10字节body，ID由Report Reference传递。
- 四字节输出报告接受但忽略，没有物理振动实现，也不向另一peer回显之前的输出。
- 捕获状态失败、非当前peer、无效hat/轴输入时返回归中报告；实际conn/epoch判断由未实现的集成层负责。
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

## 明确未完成

- 不改变9981厂商/产品身份；没有复制Google/Microsoft VID/PID。
- 无广播、配对、连接状态机、通知worker、生产运行层桥接、模式选择及持久化。
- Control Point只是调用外部接口，尚无真实挂起/重连生命周期实现。
- 未做ARM编译、空口测试、GCController枚举或Moonlight验收。
- 本目录不是可刷固件，不能称Stadia认证、Xbox原生兼容或iPad手柄已恢复。
- 原型没有用户功能变更，因此未修改正式使用说明书或生成新固件手册；进入生产候选时必须同批更新。

## 下一阶段闸门

1. 接入前先实现有界、按连接/epoch隔离的状态与通知生命周期，并验证旧peer释放、断开/重连、订阅变化、suspend/resume以及发送失败。
2. 服务呈现只能在建立连接前确定，不能在同一Read Blob过程中或仅随活动槽切换；不自动清配对，不在后台更换身份。
3. 新模式保留现有键位、灯光、速度与键鼠回退，不以全局重构冒险。先确定纯手柄的固定构建/模式隔离，再做三合一。
4. 与参照协议一致的数据不保证系统驱动匹配；如需改变厂商身份，必须先说明原因并取得用户明确选择，不能偷偷替换。
5. 目标验收分开记录：服务/Map → 每peer CCC订阅 → GCController.extendedGamepad → 按钮与轴事件 → 官方Moonlight实玩。安卓同样区分系统输入与盖世游戏测试结果。
