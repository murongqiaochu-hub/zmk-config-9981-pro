# v2.3.3 完整功能＋手柄单HID服务修正（2026-10-25）

- 分支 `codex/ipad-v233-composite-20261026`；基线原 v2.3.3 `432d6c8`，原 `codex/cursor-presets-v2.3.3-20261022` 未动。
- 功能源 `15d0281625773f987363b649420d0d0a29aaeb37`；手册 v2.3.3.2。
- 构建提交 `0603a22b6dd5e1a78df8fe7d560b331f5a6d31ab`；Actions Run `40209283076`（regression / ARM / 合并产物成功，deliver 跳过）；artifact `12604804536`（ZIP SHA-256 `6172ed53c2fe1198e57ff8bdf48c941dcb8263c76411902e8f0b7b4544e48011`）。
- UF2：618496B / 1208 块，family `0xada52840`，连续 `[0x1000,0x4c800)`，SHA-256 `5f17b1d28a7ab88126faaddd1bce18a0cdc3f115187c87c099bda20d21cc2a0e`。

## 为什么改回单一 HID 服务

2026-10-25 晚用户实测两点：

1. 上一候选 `79b7b7c` 与 `11ee89e` 用**第二个独立 0x1812 服务**隔离手柄，iPad 键鼠正常，但 iPad 与安卓都不把手柄识别为控制器（Moonlight 无法选到手柄）。
2. 固件静态 GATT 表里第二个服务确实存在（7 个服务），所以不是没编译进去，而是主机只处理第一个 HID 服务实例。

因此恢复原 v2.3.3 的做法：**同一 HOG 服务**尾部追加手柄 Input / CCC / Report Reference，Report Map 由「stock 196B ＋ 手柄 141B」两段拼接读取，手柄 Report ID 4、body 9 字节无 ID。这也与 v2.2.7 时期「手柄基本可用」的实现路径一致。

## 代码改动（仅一处）

[gamepad_runtime.c](<../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c>)：MEDIA 判断由 `zmk_keymap_highest_layer_active() == BBP_GAMEPAD_LAYER` 改为先 `zmk_keymap_layer_index_to_id()` 再比较。SDK 返回的是层顺序 index，键图或 Studio 重排后 index 与稳定 ID 不再相等；上一版按 index 比较会在 MEDIA 上误判。

其余 84 份原配置（含两份键图 168 绑定、conf、Kconfig、DT、五档速度记忆、指针滤波/削峰、滚轮固定档/翻转、MEDIA 慢呼吸、v2.3.2/v2.3.1 修复）字节未动，由 `tests/user_preferences_scope.py` 与 `tests/pointer_steady_scope.py` 的历史投影逐项证明。

## 本轮验证（host）

| 项目 | 结果 |
| --- | --- |
| 完整 host | PASS（scope 投影、12 类历史证明、实际 runtime/driver/USB/HOG） |
| 层身份 | PASS：重排映射下 MEDIA 仍命中；旧 index 比较版本必败（反事实同批编译） |
| 合成 Report Map | PASS：真 196B＋141B，ID 1/2/3/4 唯一，键盘 8B、媒体 12B、鼠标 9B、手柄 9B，集合闭合 |
| 结构证明 | PASS：拆分函数逐字节等价（本处修正先投影回基线再比对） |

## 云端与原生核验

- 云端 regression、实际 CMake 32 组合、ARM 构建与产物合并均成功。
- 直接读取新 UF2 静态 GATT 表：6 个服务；`0x1812` 只有一个，属性 26 个（句柄 28–53）。前 22 个是原键鼠/媒体/控制点（28–49，与 v2.3.3 相同），后 4 个是手柄 Input / CCC / Report Reference（50–53）。末尾 128 位自定义服务 54–57 未变。
- 原生 1170 项 Kconfig 与 30639B DTS 和原 v2.3.3 **完全一致**：本版没有改任何配置项或键位。
- 固件镜像里 stock 196B 与手柄 141B 描述符各出现 2 份（USB / BLE 各一套），无诊断字符串残留。

## 尚未验证

- iPad / 安卓这次是否终于识别手柄；各 App 映射与摇杆手感。
- 未刷写、未清配对 / settings、未改主机。

## 回退

仍可用上一份 `11ee89e`（键鼠已验证、手柄不被识别）或原 v2.3.3 构建；两者都保持配对，不需要清设置。
