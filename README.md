# zmk-config-9981-pro
This page contains zmk firmware config for 9981 keyboard with trackpad(pro version)

## 当前：完整修复候选（2026-10-25）

原 v2、USB-only `266c27b`、runtime-only `7eeca50` 已获用户实机通过。当前分支 `codex/ipad-full-repair-20261025`，功能源 `85893971ddecfa32bd21a2309ff16e7925940c9b`；恢复二分前原 `1c61b81` 完整功能及 v2.2.1 旧映射，不混 v2.3.3。普通三层 / 原参数保持，MEDIA 恢复 WASD、黑莓切杆和 L3/R3 捕获，可完整验收，不再限普通层。

- runtime 先由最高活动层 INDEX 转 stable ID 再判 MEDIA；反事实重排测试旧代码失败、新代码通过，但设备实际重排未证，不能称已确定 iPad 故障根因。
- BLE 使用第二独立 0x1812 HIDS，stock HOG 不改；196B 键鼠与 141B GP Map 分开，Input9 无 ID。CP 每 peer 独立，Suspend 只 gate 该 peer GP，Exit Suspend 先 neutral，无 remote wake。同身份仍共享 cache / 连接，多 HIDS 不保证 iPad / 游戏兼容。

原完整 gamepad suite、独立真 196+141 服务、重排反事实及 12 负向专项 PASS；本轮完整 host / ARM / UF2 / 实机待验，不预报新原生 GATT 核对结果。[手册](<docs/9981Pro_使用说明书.md>) v2.2.1.7 已恢复 27 章。必要定位构建已授权；不自动刷写，绝不清配对 / settings 或改主机配置。以下保留历史记录，不作为当前测试限制。

## 已通过运行时单变量（历史，2026-10-25）

USB-only 266c27b已获用户实测通过。本轮在其上仅接回原手柄runtime/worker及BLE ENOTSUP出口，Kconfig/默认键图/触板/轻拍源码不改，不加BLE手柄描述或服务。原neutral shim不再编译；线程和USB初始中立报告恢复，但无游戏输入绑定，仍非可玩手柄。只测普通层且启动后不进MEDIA；runtime保留原进出MEDIA取消轻拍回调。当前[手册](<docs/9981Pro_使用说明书.md>)v2.2.1.6；必要构建已授权，无自动刷写或清配对。

## 已通过USB枚举单变量（历史，2026-10-25）

原始v2.0用户复测正常，当前构建链原样重编与旧UF2逐字节相同。本轮从该正常底座只增加USB HID_1枚举与中立GET_REPORT响应；无BLE手柄扩展、无游戏worker、无MEDIA手柄键图、无触板接管。BLE仍旧196B键鼠Map；本轮不是可玩手柄版。必要定位构建已授权无需逐次确认，不自动刷机或清配对。当前[手册](<docs/9981Pro_使用说明书.md>)v2.2.1.5，完整host/ARM及用户刷测分别记录。

## v2.0基线说明与维护约定（历史）

- 完整中文说明：[9981 Pro 使用说明书](<docs/9981Pro_使用说明书.md>)（v2.0，2026-10-19，基于功能源 `28f6c86`）。默认键图、物理监听与用户实际 Studio 覆盖须分别核对。
- 用户新增规则：**以后每次更新功能或默认键位，必须在同批次同步说明书、四层键图及版本/验证状态。** 维护流程见 [AGENTS.md](<AGENTS.md>)；完整主机回归已包含[说明书同步检查](<tests/check_manual_sync.py>)。
- 下文是上游仓库原有构建引导，不代表当前操作说明；例如旧 Caps Lock 滚动示例不适用于本版（本版用右 aA 按住或双击锁轮）。


<p align="center">
<img  height="1200" alt="image" src="https://github.com/user-attachments/assets/20568c06-c573-438d-8e5e-26394261aa20" />
</p>

### Use this repo to generate your own ZMK keymap for the 9981 Pro keyboard.  
## Get started  
```Option1```   
**0. Register a github account if you don't have one.**  
**1. Fork this repo.**  
<p align="center">
<img height="100" alt="image" src="https://github.com/user-attachments/assets/73fbcded-25c4-4208-91d0-c07d19170656" />  
</p>

**2. Open up `config/bbp9981.keymap` and edit the keymap to your liking.**  
**3. After editing the keymap, choose commit changes.**  
**and then check the Github Actions section.**  
**Your new firmware file should be available for download.**  
**5. Unzip the firmware.zip file. You should see one files: `bbp9981-zmk.uf2`.**  
**6. Flash the keyboard with your new firmware.**  

```Option2```  
**0. Register a github account if you don't have one.**  
**1. Fork this repo.**  
**2. Access the [keymap editor web](https://nickcoutsos.github.io/keymap-editor/)**  
**3. Login with your Github Account on the web**  
**4. Choose the right repository and you can edit the keymap more intuitivly**  
<p align="center">
<img height="800" alt="image" src="https://github.com/user-attachments/assets/50e5df6c-b703-48af-808c-8b864ba243a0" />  
</p>
**5. After editting the keymap there will be another github action compiling and you will have the firmware.zip file**  
**6. Flash the keyboard with your new firmware.**  

**More Info about the web app please access this [github page](https://github.com/nickcoutsos/keymap-editor)**  

## Build your own driver
You can change the source code under the ```/config/boards/arm/bbp9981/custom_driver```

For example if you want to change the scroll direction when capslock is activated:
Go to this [line](https://github.com/ZitaoTech/zmk-config-9981-pro/blob/e0c119fc6a126b8af9a3b8e19102eeef02d5e725/config/boards/arm/bbp9981/custom_driver/a320_0x57.c#L201) and change ```scroll_y``` to ```-scroll_y```
