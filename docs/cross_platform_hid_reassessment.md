# 全端键鼠与手柄：证据重建及候选边界

日期：2026-10-26。仅本地研究与回归，未交付新固件、未刷写或实机验收。

## 当前候选

- 分支 `codex/cross-platform-hid-20261026`，基于 `0603a22`。
- 工作树 `/vol1/1000/dev/9981-firmware/repo/worktrees/cross-platform-hid-20261026`。
- 生产源码暂不修改。选此基线是为了隔离之后的描述符/槽位实验，并非宣称这个版本已恢复安卓或兼容iPad。
- 原工作树、所有已交付UF2及可用iPad键鼠回退保持不变。
- 原版v2.3.3源码构建提交是 `e2c4966136b5e6af5db795c6c46048672758ba30`；`ef911f2` 是UF2 SHA-256前缀，不是源码提交。

## 已重建的事实

1. `git diff 432d6c8 0603a22 -- config` 仅修改 `gamepad_runtime.c` 中MEDIA层判断：最高层order index转换为stable ID。BLE extension、Map、CMake没有变化；workflow、west配置、build.yaml也没有变化。不能继续沿用交接里“原版preinclude与0603a22 readhook不同”的解释。
2. 固定ZMK `edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/keymap.c` 确实返回最高层order index。转换合理；默认未重排时前后等价，不能仅凭改动存在就归罪于它。
3. 本地实际UF2检查：原件618496B SHA `ef911f23e250c55204d9342dea0a6322161e3c4ee624379c8bbe7ed8467c401a`；0603a22同为618496B，SHA `5f17b1d28a7ab88126faaddd1bce18a0cdc3f115187c87c099bda20d21cc2a0e`。二者静态服务序列均1801/1800/180f/180a/1812/Studio，HID26属性，推导句柄28–53。静态一致不能证明空口读出一致，也不证明主机缓存已刷新。
4. iPad键鼠回退11ee89e为619520B，SHA `1e277b08428984a6120c42310c34a6ad40eaaecdf491cbfb1d706e58c3359264`；键鼠HID22属性28–49，Studio50–53，额外HID11属性54–64。该结构与原版确实不同。缓存是待验证因素，不是已定位原因。
5. 丢弃的3d5dd36按全局active profile选Map，每个Read Blob片段重选；请求conn不同于active peer或读取中切槽，会破坏连接的描述符契约。新增独立oracle在当前固定Map上通过，使用历史真实extension替换后在字节比较断言失败。此缺陷不能解释此前0603a22失败。
6. 丢弃的safe Map无LED Output，而配置启用HID_INDICATORS、GATT保留LED Output。属于静态契约不一致；尚未证明是iPad失败根因。

## 测试策略

- 不更改冻结的 `tests/test_gamepad_ble.c` 或SDK fixture；首次尝试在旧oracle加测试被冻结闸拒绝，已完整撤回该文件改动。
- 新增 `tests/test_gamepad_map_stability.c`：复用未改的旧测试TU，三个peer、22字节分片、分片间切active profile/connection，核对整段Map不变。
- `tests/run_gamepad_tests.py` 加入新oracle。
- `tests/run_map_stability_counterfactual.py`：当前实现必须通过，Git历史3d5dd36的真实按槽实现必须在指定字节一致性断言失败，临时隔离构建、不改源码、不访问设备。
- 旧手柄专项strict UBSan通过；新增oracle与历史反事实通过。独立oracle初次拆出时漏初始化mock peer引用计数，已补齐与原main一致的初始化，固定版通过、历史版在指定Map字节断言失败。
- 在dev挂载上完整回归的私有文件权限测试失败（0 != 0600）；直接copytree又继承了不可用权限。未改系统或挂载权限，改为Git archive在临时标准权限目录提取并覆盖本轮测试文件，完整host最终exit 0，日志 `05_Git归档快照完整回归.txt`。之后含协议codec的最终完整回归也exit 0，见 `06_含协议转换原型完整回归.txt`（Map新测试第253行、codec第254行）。本地CMake执行矩阵因工具不可用仍SKIP（第382行），扩展token静态闸运行；未安装工具、未ARM构建。不能用host PASS代替设备验收。

## 已开始的隔离协议原型

新增 `experiments/gamepad_protocol/stadia_codec.h`，只在host测试中编译，不进入生产CMake、不改变设备身份/广播/GATT。它把现有9981九字节逻辑状态转换为参照项目的十字节Stadia格式：方向帽空值15、摇杆中心128且范围1–255、触发器保持0–255、显式字节打包避免C bitfield布局依赖。保留既有按钮语义，未实现的capture/assistant/extra始终为零，异常输入归中并返回失败。

`tests/test_gamepad_stadia_codec.c`覆盖65536按钮字、255个有符号轴值、9个帽状态、256档触发器与异常/空值。这只验证协议数据转换，不证明iOS识别；下一阶段必须补足对应描述符、GATT、订阅与系统验收。参考实现同时改变Google/Microsoft厂商身份，当前没有复制这些身份设置，不能把本原型称Stadia认证或Xbox原生兼容固件。

## iPadOS门槛与候选路线

Moonlight iOS实际枚举 `GCController.controllers` 并要求 `extendedGamepad != nil`。至少区分：

1. 蓝牙配对/服务发现；
2. Map读取与报告订阅；
3. 系统GCController及extendedGamepad；
4. 按钮/摇杆事件与Moonlight实玩。

nRF Connect看到1812只证明服务存在，不等于系统或Moonlight接受，也不能证明第二服务一定被忽略。

优先研究候选：`jfedor2/portable-gamepad-firmware`，固定树 `aa09d1d05939ab0025107f6df359e0b8a3b714c0`。有nRF52840、纯Zephyr GATT、Stadia与PC BLE完整协议实现，README声称iOS兼容，但未找到带iPadOS版本/GCController/Moonlight的独立实测。它是实验参照而非成功保证，不直接移植整套固件到9981，也不擅自冒充其他厂商身份。

推荐技术顺序：纯手柄协议先通过系统接口，再逐项加键盘、鼠标；复合失败时应评估明确独立模式，而非再次按活动槽在同一读取中切Map。现有键位、轻拍、灯光、速度记忆不动。USB与BLE分开验收，Windows标准HID与XInput-only游戏分开验收。

## 第二个本地里程碑：纯手柄声明与GATT原型

已补齐[实验说明](<../experiments/gamepad_protocol/README.md>)、固定182字节参照描述符与16属性纯手柄GATT表，输入ID3/10字节、输出ID5/4字节匹配。未接入生产CMake、广播/身份/模式选择或notify worker；源码config仍与0603a22相同。

独立审查指出NULL合法长度、write flags边界及测试自证问题，已加固：公共写检查拒绝PREPARE/EXECUTE/未知flags和NULL；read输入无效快照归中；全部read长度/offset边界与诊断实参断言；实际GATT样本按HID字段独立解码。缺Output、多LED、错ID、重复ref、hat/output位宽和button usage共7种契约反例均被拒绝。

新增原型后的完整host在标准权限Git归档快照exit0（07日志），审查补强专项exit0（08日志）。CMake执行矩阵仍SKIP；没有ARM/空口/GCController/Moonlight结果。上游仅输入GATT与其含Output描述符不一致的细节被记录，不推定它是苹果不识别的根因。

下一步是实际conn/epoch/suspend/订阅/notify生命周期桥接，不能把当前外部snapshot/suspend接口的mock当作已实现的设备功能。

## 下一道决策与验收

用户本轮已确认：iPad Air 6、iPadOS 26.2、官方Moonlight（具体App版本未提供）；接受先验证明确键鼠模式／纯手柄模式，再推进合并。此方案不是已完成的三合一，也不等于授权更改厂商身份、刷写或清配对。

安卓原验证入口是盖世游戏的手柄测试，iOS修复之前测试成功；目前不能把修复后盖世测试失败写成Android系统不枚举。安卓回归需区分“系统根本不枚举”和“枚举但MEDIA不发事件”。优先观测完整Map、ReportRef、ID4 CCC及按键notify，不能要求无目的重复刷机；任何清配对、刷写或控制设备另取精确授权。

本阶段不推送、不运行Actions、不发布。后续构建按项目已核验授权边界执行；本次研究不自动授权厂商身份改动或移植第三方协议。

## 原始来源

- https://raw.githubusercontent.com/zmkfirmware/zmk/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/keymap.c
- https://github.com/moonlight-stream/moonlight-ios/blob/master/Limelight/Input/ControllerSupport.m
- https://github.com/jfedor2/portable-gamepad-firmware/tree/aa09d1d05939ab0025107f6df359e0b8a3b714c0
- https://raw.githubusercontent.com/jfedor2/portable-gamepad-firmware/aa09d1d05939ab0025107f6df359e0b8a3b714c0/app/src/hids_zephyr.c
- https://learn.adafruit.com/adafruit-bluefruit-le-shield/ble-services#at-plus-blehidgamepaden-2435778
