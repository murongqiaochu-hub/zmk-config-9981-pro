# 完整v2.3.3兼容修正移植记录

- 日期/手册：2026-10-25，v2.3.3.1。
- 基线：原v2.3.3树 `432d6c8`；原v2.3.3分支不动，另建 `codex/ipad-v233-repair-20261025`。
- 最终功能源码：`c667531a0c22d6677b735119861a6d6c1ed4b881`（父已冻结）。
- 构建提交：`PLACEHOLDER_PARENT_BUILD`（待父填写，非有效hash）。
- Actions运行号：`PLACEHOLDER_PARENT_RUN`（待父填写）。
- UF2 SHA-256：`PLACEHOLDER_PARENT_UF2_SHA256`（待父填写，非有效hash）。

## 范围与保留

仅[手柄runtime](<../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c>)的INDEX→stable ID、[模块CMake](<../config/boards/arm/bbp9981/cmake/gamepad.cmake>)取消HOG注入，并新增[独立BLE服务](<../config/boards/arm/bbp9981/custom_driver/gamepad_ble_service.c>)。父确认85份旧config中另83份逐字节不变，168键、conf/Kconfig/DT全部不变；新service与上一 `79b7b7c` 逐字节相同。

完整保留五档记忆、指针滤波/削峰/BLE≤10ms积压合并、滚轮固定档/翻转、MEDIA慢呼吸、v2.3.1 USB修复及其余v2.3.3玩法参数，不恢复v2.2.1旧键图或手册。

BLE stock196与GP141的Report Map分属两个HIDS，Input9无ID，原服务不拼接。手柄CP每peer独立suspend，无remote wake；身份、连接与主机cache仍共享，多HIDS不保证iPad手柄支持，不清配对/settings。

上一版已反事实证明index bug，但设备实际层重排未知，不称实机根因已证。上一候选 `79b7b7c` 是v2.2.1旧功能，用户仅确认键盘/鼠标正常，手柄未明确验收；不能外推本轮。

## 本轮验证记录（待父据实际结果更新）

| 项目 | 状态 |
| --- | --- |
| 完整host | 本地PASS；实际CMake32+9本地缺工具明确SKIP，CI必须执行 |
| ARM/UF2 | 待构建与校验 |
| 原生GATT | 待验证，不以模拟替代 |
| 实机键盘/鼠标 | 待验证 |
| 实机手柄/iPad/App | 待验证 |
| manifest/导出QA | 父负责，待验证 |

仅候选，不发布/归档；不自动刷写、清配对/settings或改主机。源码、构建、交付、刷入和实机验收分别记载，旧048/858或旧运行号不作为本轮新功能源/结果。完整玩法见[使用说明书](<9981Pro_使用说明书.md>)。
