# zmk-config-9981-pro

ZMK firmware configuration for the 9981 Pro / P9981 keyboard with optical trackpad (`bbp9981`, not Air).

## 当前：纯键鼠日用收口

**2026-11-02 · v2.3.4-daily 纯键鼠日用收口版（待蓝牙实测）· 手册 v2.3.4.1。** 用户于14:23永久放弃手柄。本轮从完整 `11ee89eee92969a15f75a0d5848c38c6a2c2ce15` 收口，功能源码为 `bba36b607e0f90401f1840b6a2f897d347f5df52`：移除全部手柄生产实现与接线，仅将曾被占用的 MEDIA 恢复为原默认，不重置其他已认可键鼠、触板、灯控或保存设置。

- **MEDIA：39个 `&none`＋3个 `&to DEFAULT`。** $/P35、SYM/P40、右下aA/P41回普通层；其余无绑定输出，不透明回落。真源 `72c0cee8cb0dbd9e14e692a96d9ef3726bcc73e8` 的两份原键图和历史手册已交叉核实；完整42位置与来源见[日用说明](<docs/daily_keymouse.md>)。None不等于关闭传感器或P37/P41等物理监听。
- **其余三层126绑定保留。** Z五档记忆、相对指针滤波/削峰/BLE≤10ms积压合并、固定方向锁轮、aA仲裁、鼠标翻转、轻拍原参数、v2.3.1 USB轻拍快照修复不回退。
- **灯光仍用已认可的新玩法。** UPPER X/V/N控制键盘灯减/开关/加，C/B控制触板灯减/加；MEDIA约3.6秒慢呼吸，不恢复旧快闪。两路亮度与Z档位独立保存。
- **蓝牙键鼠是验收目标。** 当前软件回归、真实ARM/UF2、交付和实机分开记录；构建提交/运行号/UF2哈希仍待填写，见[本轮状态](<docs/daily_keymouse.md>)。未获本版蓝牙实机结论，不称“稳定已验”或全平台通过。
- 上一轮 `51fe33a` 候选在2026-11-02 14:23获用户“仍失败”反馈，具体失败范围未知，不猜成仅手柄或全部键鼠失败；不外推到本版。不再研发、测试或发布手柄路线。

## 使用与维护入口

- [完整中文使用说明书](<docs/9981Pro_使用说明书.md>)：四层、组合输入、滚轮、轻拍、灯光、维护键与风险。
- [纯键鼠日用说明](<docs/daily_keymouse.md>)：短步骤、完整MEDIA42键、精确历史来源、保留边界和实际验证状态。
- [主键图](<config/bbp9981.keymap>)与[板级键图](<config/boards/arm/bbp9981/bbp9981.keymap>)：编译默认；Studio保存的布局可能覆盖默认，不自动擦设置。
- [项目维护规则](<AGENTS.md>)、[变更记录](<CHANGELOG.txt>)、[说明书清单](<docs/manual_manifest.json>)：功能、正文、四图与三格式必须同批同步；哈希不替代语义或实机检查。

### 几个常用操作

普通层输入字母；黑莓为Cmd/左GUI，左下aA为Ctrl。按住右aA滑触板临时滚动；aA层双击锁轮，初始0°，再次双击解锁，SYM只退层不解锁。松aA且锁轮时黑莓/顶部返回三击选择两个固定方向档，同键重复不累加；按住aA时两键优先选蓝牙槽1/2。右aA+M双击翻鼠标（普通Y方向和肩键角色，重启关闭）；右aA+Z循环五档，末次调节后等至少1秒保存。

选蓝牙输出用右aA+Q；配合右aA+通话/黑莓/返回/结束选择槽0/1/2/3。轻拍默认OFF，普通层左键角色肩键三击开关，误触时关闭改用机械点击。UPPER的R重启、触板机械双击清当前配对、$单击进刷机引导，不作为普通排错操作。详见手册，本文不授权刷写、清配对、擦settings或改主机。

## 历史证据，不是当前使用指引

- 完整v2.3.3兼容移植基线、旧候选及其限制见[历史移植记录](<docs/ipad_v233_repair.md>)；`79b7b7c`只获历史键鼠正面反馈，且是旧功能版本，不代表当前126键及完整玩法已实机验收。
- 五档速度/MEDIA慢呼吸的来源见[历史指针设定说明](<docs/cursor_preferences.md>)；v2.3.2指针修正见[历史防抖说明](<docs/pointer_steady.md>)；USB快照修复与遗留限制见[历史稳定性说明](<docs/firmware_stability.md>)。
- 旧GBA/Xbox/虚拟杆、R/U菜单等已退出当前功能。历史提交、构建、UF2与当时反馈保留在[历史交付记录](<firmware-delivery/XBOX_CURRENT.md>)及[历史实现笔记](<docs/gamepad_implementation_notes.md>)；其中“当前”“通过”“待测”只属于当时版本，不能当本版结果，也不授权重开手柄路线。
- 原生HID_0可变报文DMA、abort丢ready/许可和矩阵多键等限制未因去手柄自动消失。旧模拟/构建通过不是本轮蓝牙实机稳定性证明。

## 上游历史构建引导

以下仅保留上游仓库使用方式，不代表本版构建已执行或对后续任务的授权。当前构建、交付与设备边界以[维护规则](<AGENTS.md>)及用户明确指令为准；旧Caps Lock滚动示例不适用于本版，当前是右aA按住或双击锁轮。

<p align="center">
<img height="1200" alt="Upstream historical keyboard image" src="https://github.com/user-attachments/assets/20568c06-c573-438d-8e5e-26394261aa20" />
</p>

### Generate your own ZMK keymap (historical upstream guide)

1. Register a GitHub account and fork the repository.
2. Edit the [keymap](<config/bbp9981.keymap>) directly, or use the [keymap editor](<https://nickcoutsos.github.io/keymap-editor/>).
3. Commit only within your authorized repository/branch and inspect the actual Actions result; a commit alone does not prove a valid firmware artifact.
4. Before any manual flashing, verify the target board, source commit, UF2 and recovery path. Current user instructions take precedence; this guide does not authorize automatic flashing.

Editor reference: [keymap-editor repository](<https://github.com/nickcoutsos/keymap-editor>).

### Historical driver-development example

Source lives in the [custom driver directory](<config/boards/arm/bbp9981/custom_driver/>). The old upstream [Caps Lock scroll example](<https://github.com/ZitaoTech/zmk-config-9981-pro/blob/e0c119fc6a126b8af9a3b8e19102eeef02d5e725/config/boards/arm/bbp9981/custom_driver/a320_0x57.c#L201>) belongs to a different historical layout; it is not an instruction to alter the current fixed scroll presets or reopen feature development.
