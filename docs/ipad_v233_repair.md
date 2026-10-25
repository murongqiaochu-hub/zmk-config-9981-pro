# 完整v2.3.3兼容修正移植记录

- 日期/手册：2026-10-25，v2.3.3.1。
- 基线：原v2.3.3树 `432d6c8`；原v2.3.3分支不动，另建 `codex/ipad-v233-repair-20261025`。
- 最终功能源码：`c667531a0c22d6677b735119861a6d6c1ed4b881`（父已冻结）。
- 构建提交：`11ee89eee92969a15f75a0d5848c38c6a2c2ce15`。
- Actions运行号：`40204700661`，regression/ARM/合并产物成功，deliver跳过；artifact `12603192572`，原ZIP SHA256 `ea49c66c342d0ee6ca39c295fdbdc27c79e4d3fe06806402d170fdf067804786`。
- UF2 SHA-256：`1e277b08428984a6120c42310c34a6ad40eaaecdf491cbfb1d706e58c3359264`；619520B/1210块，family0xada52840，连续应用区[0x1000,0x4ca00)，不含bootloader/NVS。
- 原生1170项配置、30639B完整DT及168键与原v2.3.3全同；DT SHA256 `77d47ac09693cfafe1f9c69717e5cd520341cbea59d5daf015eaac82cf007f99`。直接读取新旧UF2静态GATT表，7服务的UUID/属性权限/次序/绝对句柄与键鼠已通过的79b7b7c全同（不是与旧拼接式v233全表相比）；stock HOG28–49/22attrs、GP54–64/11attrs。stock196和GP141各有USB/BLE两份。

## 范围与保留

仅[手柄runtime](<../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c>)的INDEX→stable ID、[模块CMake](<../config/boards/arm/bbp9981/cmake/gamepad.cmake>)取消HOG注入，并新增[独立BLE服务](<../config/boards/arm/bbp9981/custom_driver/gamepad_ble_service.c>)。父确认85份旧config中另83份逐字节不变，168键、conf/Kconfig/DT全部不变；新service与上一 `79b7b7c` 逐字节相同。

完整保留五档记忆、指针滤波/削峰/BLE≤10ms积压合并、滚轮固定档/翻转、MEDIA慢呼吸、v2.3.1 USB修复及其余v2.3.3玩法参数，不恢复v2.2.1旧键图或手册。

BLE stock196与GP141的Report Map分属两个HIDS，Input9无ID，原服务不拼接。手柄CP每peer独立suspend，无remote wake；身份、连接与主机cache仍共享，多HIDS不保证iPad手柄支持，不清配对/settings。

上一版已反事实证明index bug，但设备实际层重排未知，不称实机根因已证。上一候选 `79b7b7c` 是v2.2.1旧功能，用户仅确认键盘/鼠标正常，手柄未明确验收；不能外推本轮。

## 本轮实际验证记录

| 项目 | 状态 |
| --- | --- |
| 完整host | 本地PASS；云端regression PASS，实际CMake32+9及反事实/独立HIDS均通过 |
| ARM/UF2 | Run40204700661 / `11ee89e` 成功；UF2边界与哈希PASS |
| 原生GATT | 新旧UF2静态表逐项比对PASS；非无线枚举 |
| 实机键盘/鼠标 | 本轮待用户验证；79b7b7c的键鼠通过不可外推 |
| 实机手柄/iPad/App | 本轮待用户验证 |
| manifest/导出QA | 本批同步并核验，见交付目录 |

仅候选，不发布/归档；不自动刷写、清配对/settings或改主机。源码、构建、交付、刷入和实机验收分别记载，旧048/858或旧运行号不作为本轮新功能源/结果。完整玩法见[使用说明书](<9981Pro_使用说明书.md>)。
