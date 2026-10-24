# v2.0 与 v2.2.1 对比、iPad 元数据兼容候选

2026-10-24。功能源 `f1d862ffc22ad7371b5a59471b11e086764cf5d6`；候选分支 `codex/ipad-hid-metadata-v2.2.1-20261024`。固件仍是 v2.2.1 的功能集，手册修订为 v2.2.1.1。

## 结论与证据强度

- 用户确认 v2.0 的 iPad 键鼠正常，原 v2.2.1 失败；对照固定为 `28f6c860c73dfdcb06c55c2ca0f9643d6f0d920f` 与 `1c61b817f7e2d52fd2bbea888d1867f354e4c2a8`，不以更晚版本替代。
- 已定位的主要协议差异是新增 Game Pad：BLE 同一个 HIDS 的 Report Map 由 196B 扩为 337B，并增加独立报告/CCC/Reference；USB 新增独立 HID_1。并非把原键盘/鼠标报告改长。
- 两版均存在 HID Information bcdHID=0000 与 USB VID 配 Bluetooth SIG PnP Source=1 的旧元数据问题。它们不是 v2.2.1 新增的差异，**不能据此宣布已定位回归根因**。
- 本次选择不删功能的最小标准化候选：修正上述元数据，增加标准 DIS 软件版本使 GATT 数据库结构真实改变。假设是加入 Game Pad 后主机分类/枚举可能更严格；没有 iPad 抓包或实测支持前，仍只是候选，不保证修好。
- v2.3.4 的单主机策略失败反馈不作本方案的成功证据；本轮不继续叠加该策略。

原源码、两次原构建日志、原 UF2 描述符交叉核对及完整差异保留在[本轮证据目录](</vol1/1000/projects/output/1024/164259_iPad兼容修正_v2.2.1/>)；其中[真实 HID 核对](</vol1/1000/projects/output/1024/164259_iPad兼容修正_v2.2.1/07_真实HID与原固件核对.json>)记录实际构建配置和字节，不使用精简 mock 的长度冒充真实设备。

## 实际差异

| 项目 | 正常 v2.0 | 失败 v2.2.1 | 本候选 |
| --- | --- | --- | --- |
| BLE HID 服务 | 单 HIDS，键盘/消费者/鼠标 | 仍单 HIDS，尾部增加 Game Pad | 不增加第二个 HIDS |
| BLE Report Map | 196B | 196B 原前缀 + 141B 手柄 = 337B | 337B 逐字节不变 |
| Input bodies | ID1=8B，ID2=12B，ID3=9B | 原三份不变，ID4=9B | 四份均不变 |
| USB | 原键鼠 HID_0 | 原 HID_0 + 独立手柄 HID_1 | 完全保留 |
| HID Information | 版本 0000 | 版本 0000 | 版本 0111，读取字节 `11 01`；country/flags 不改 |
| DIS PnP | Source1，VID/PID=1D50:615E | 同左 | Source2，VID/PID/产品版本不改 |
| DIS 软件版本 | 未启用 | 未启用 | 标准 0x2A28=`2.2.1-ipad-hid1` |
| GATT 数据库 | 原结构 | HOG 增加手柄尾部 | DIS 新增 declaration/value 两属性；HOG 内容不动 |
| 默认按键 | 键鼠 | MEDIA 新增 17 个手柄绑定 | 沿用原 v2.2.1 四层 168 绑定 |
| 键鼠保护 | 原路径 | MEDIA 独占触板，必要生命周期/代际门 | 原样保留，不重写发送/路由 |

实际配置固定 HKRO6、完整消费者 usages6、pointing 开启、smooth scrolling 关闭；因此四个 Input report 的 body 位数为 64/96/72/72。BLE Input value 不带 Report ID；USB 手柄仍为 ID4+body9，共10B。

## 只改哪些地方

[精确范围闸](<../tests/hid_metadata_scope.py>)以完整 `1c61b81` 为基准，冻结下列四份生产配置的字面补丁，其余59份配置及全部键图字节相同：

1. [产品配置](<../config/bbp9981.conf>)：开启兼容开关、Source2 和标准 DIS 软件版本。
2. [板级 Kconfig](<../config/boards/arm/bbp9981/Kconfig.board>)：新增默认关闭的兼容选项，依赖原 gamepad/BLE/DIS/PnP。
3. [HOG 专属前缀](<../config/boards/arm/bbp9981/custom_driver/gamepad_ble_extension.h>)：只在非空 attribute/UUID/value、长度4且 UUID=HID Information 时修正两字节；country/flags 透传。调用原生同步 `bt_gatt_attr_read` 处理 offsets、EOF 和错误，不保存栈指针，不修改原 info 对象。
4. [板级 CMake](<../config/boards/arm/bbp9981/CMakeLists.txt>)：核验固定 Zephyr DIS 源没有 dirty 修改，并输出构建标记；原 SDK/HAL/四份 ZMK 源 SHA 闸门保留。

不修改 USB/键鼠/手柄报告、原 CCC、路由、epoch、加密权限、安全升级、配对、MAC、settings 或设备名称；不新增自定义 GATT marker，不用禁用 BLE 手柄换取兼容。

**句柄边界：** HOG 仍26属性，原发送所用相对索引5/9/13不变；DIS由7变9属性，位于其后的服务的绝对句柄可能移动。相对索引不变不等于绝对句柄不变。原 GATT caching/Service Changed 配置仍开，由固定原生栈处理；增加标准特征不是已证明清空 Apple 缓存，也不是擅自删除 bond。

原生依据：[固定 HOG](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/hog.c>)、[固定 DIS 实现](<https://raw.githubusercontent.com/zmkfirmware/zephyr/dacab4875df72109b96cc8977547a0dc04875bcd/subsys/bluetooth/services/dis.c>)、[固定 DIS Kconfig 的 Source1/2 定义](<https://raw.githubusercontent.com/zmkfirmware/zephyr/dacab4875df72109b96cc8977547a0dc04875bcd/subsys/bluetooth/services/Kconfig.dis>)。

## 软件验证与已发现的测试问题

本轮新增专项、旧范围闸及完整主机回归均实际 PASS（exit0），记录见[专项日志](</vol1/1000/projects/output/1024/164259_iPad兼容修正_v2.2.1/12_HID元数据专项_最终.log>)和[完整回归日志](</vol1/1000/projects/output/1024/164259_iPad兼容修正_v2.2.1/13_完整主机回归.log>)；不是原版 PASS 的重复引用，也不是 iPad 验收。验证入口：

```bash
python3 -B tests/hid_metadata_scope.py --selftest
python3 -B tests/run_hid_metadata_tests.py
python3 -B tests/run_host_tests.py
```

- 新增专项复用固定真实 HID 头及 HOG 源；HKRO/NKRO、smooth、snapshot、兼容 ON/OFF 共16组合严格 UBSan，另有完整目标配置 ASan+UBSan。
- 覆盖 HID Information len0..8、offset0..5、前后 canary、原对象不变、country/flags透传、UUID/长度门；Report Map 所有 offset 和 MTU23/65/185/247 的读取片段，解析验证四份报告位数。
- OFF 正向仍通过原语义；指定要求修正 Info 的独立负例必须在相应断言失败，不把任意崩溃算负例成功。
- 旧 SDK fixtures、旧 BLE oracle 和 USB suite 保持原文件字节不改。旧范围闸的 BASE/原断言不变，先验证整个四文件精确差异，再仅投影已审核的配置插入，不能泛化豁免其他改动。
- 初次静态范围锚点多一个空格，已修正；真实196B描述符暴露旧未调用组合测试的128B临时缓冲过小，仅在受控临时副本精确扩大为512B，旧断言逐字保留、反向替换证明其他字节不变。
- 本机 GCC12 的 ASan+PIE 空程序也会随机在 main 前崩溃（20次中7次）；非PIE空程序20/20通过。ASan测试改为 `-fno-pie -no-pie`，仍保留全部 sanitizer、严格警告和失败即退出，不重试掩盖固件测试失败。证据见[环境探针](</vol1/1000/projects/output/1024/164259_iPad兼容修正_v2.2.1/11_ASan环境探针.json>)。

**本轮 ARM/UF2 实际 PASS：** 构建 `17192e63fdc94e8080d61f89cd97724339bd64c7`，[Run 40082379873](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/40082379873>)，artifact `12554904750`。原生旧1163个正向Kconfig项仅PnP Source改变，另增3项；最终DT全文29300B与原v2.2.1逐字节相同。原生HOG修正前缀、固定SDK/HAL标记均命中，最终UF2中原196B与手柄141B描述符各有2份，标准软件版本及Source2的PnP数据亦存在。

唯一交付UF2为605184B、1182块，nRF52840 family `0xADA52840`，连续应用地址 `[0x1000,0x4AE00)`，SHA256 `9dd7bbf5e33ef2a414e7725994c710131c9036e751f322f7009783e08e4691fe`。云端ZIP digest与API吻合；旧auto-deliver归档回推作业实际SKIP。详见[可重放核验报告](</vol1/1000/projects/output/1024/164259_iPad兼容修正_v2.2.1/23_ARM与UF2核验.json>)。这些是构建/文件证据，**不是已刷写、无线收包、Apple缓存刷新或iPad实机验收**。

## iPad 验收及下一阶段

候选构建完成并校验后只交一份 UF2，不自动刷写、不清配对、不擦设置、不改主机或网络身份。

1. 用户确认准备好原固件及引导/回退方法，再自行决定是否刷入；v2.0键鼠回退SHA见[手册第19章](<9981Pro_使用说明书.md>)。
2. iPad 不同时插入可成为当前输出的 USB 数据主机；使用既有 iPad 槽，普通层选择 BLE 输出（右 aA+Q）。不强制忘记设备/重新配对。
3. 测试普通文字、触板指针、左右点击、滚轮；然后断开/重连再试。记录 iPadOS 版本及“是否显示连接、能否输入、指针是否出现”。不要先进入 MEDIA，以免把正常独占行为当失败。
4. 普通键鼠通过后，再检查 USB/BLE 手柄、退出恢复键鼠、组合键/灯光等原 v2.2.1 功能。标准 HID 不是 XInput，具体 App 与系统手柄识别仍需单独验收。
5. 若依然失败，停止本候选扩展，保留失败现象继续定位；不宣称方案已验证、不自动移植或发多份试验固件。
6. **仅用户确认本版 iPad 实测通过后，才将同样修正移植原 v2.3.3 `432d6c857382769963afd65de0f803a303a67e59`，保留其全部后期功能，重新回归、同步手册、构建校验并交付。**
