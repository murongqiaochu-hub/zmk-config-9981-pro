# zmk-config-9981-pro
This page contains zmk firmware config for 9981 keyboard with trackpad(pro version)

## 当前使用说明书与维护约定

- 完整中文说明：[9981 Pro 使用说明书](<docs/9981Pro_使用说明书.md>)（手册 v2.2.1.1，2026-10-24；固件 **v2.2.1 iPad 元数据兼容候选**，非原生 XInput，沿用原 v2.2.1 键图）。用户已撤回未发布 GBA 方案；旧 `abe65ea` / `89fbe9d` 仅为本地撤回历史，不是可刷当前 Xbox 固件。默认键图、物理监听与设备实际 Studio 覆盖须分别核对。
- 仅 MEDIA 层：W/A/S/D 保持 D-pad，**不是左摇杆**；I/J/L/M＝Y/X/B/A；通话/结束＝LB/RB，双肩＝数字 LT/RT；实体 T/Y＝View/Back、Menu/Start，空格＝Guide/Xbox。K、N、Enter 等留空；$ / SYM / 右 aA 三出口仍回普通层，方向、面键、肩与菜单不发字母。
- 触板默认滑动控制左杆 X/Y；按住物理黑莓才控制右杆 Z/Rz，松开不锁定，切换先清旧轴且只用新样本。机械按压在 DOWN 时捕获 L3/R3，直至 UP 释放原按钮。velocity 虚拟杆不累计位置；停住手指不能保持倾斜，120ms 无有效位移只保证本地归零策略，不是主机收包时限。单触板不能同时独立控制两杆 / 同时按 L3、R3；详见手册第 26 章。
- 游戏层 driver 独占触板并有晚发送 gate，不同时发鼠标、滚轮或轻拍；退出恢复原鼠标。轻拍原参数、开机 OFF、普通层三击开关不调优，游戏层仅临时阻断和取消旧候选，不自动改开关值。USB 独立 HID_1：ID4 + 9-byte body = 10 bytes；BLE HOG 发 9-byte body、无 ID。
- 本轮功能源 `f1d862f`，仅改 BLE HID Information、PnP 来源和标准 DIS 软件版本；USB/BLE 手柄、键鼠、168 绑定及配对/安全策略不删改。两项旧元数据问题也存在于 iPad 正常的 v2.0，**尚未证实是故障根因**。本轮新增真实 HID 16组合 UBSan＋ASan、特定 OFF 负例及原完整回归均 PASS（exit0）；[Run 40082379873](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/40082379873>) ARM 构建及605184B单份UF2校验 PASS，构建 `17192e6`；原生DT全文不变、配置仅预期4项差异。未刷写或证明 iPad 可用。先交单份 v2.2.1 候选，用户确认 iPad 通过才移植原 v2.3.3；不推稳定/发布、不归档回推、不清配对/settings或改主机。详见[对比与修正说明](<docs/ipad_hid_metadata.md>)与手册第 19、23–25 章。标准 HID 不是 XInput，各系统/App、Guide、矩阵组合仍须实测；LT/RT 仅 0/255，无振动/Share。
- 用户新增规则：**以后每次更新功能或默认键位，必须在同批次同步说明书、四层键图及版本/验证状态。** 维护流程见[维护规则](<AGENTS.md>)；[说明书同步检查](<tests/check_manual_sync.py>)已纳入主机回归，但本轮最终结果以实际日志为准，不用同步哈希代替语义或实机核对。
- 下文仅保留上游仓库历史构建引导，**不是本轮推送、Actions 或刷写授权**；例如旧 Caps Lock 滚动示例不适用于本版（普通键鼠层用右 aA 按住或双击锁轮）。


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

**2. Open the [keymap](<config/bbp9981.keymap>) and edit it to your liking.**<br>
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
You can change the source code under the [custom driver directory](<config/boards/arm/bbp9981/custom_driver/>), subject to the current project authorization and maintenance rules above.

For example if you want to change the scroll direction when capslock is activated:
Go to this [line](https://github.com/ZitaoTech/zmk-config-9981-pro/blob/e0c119fc6a126b8af9a3b8e19102eeef02d5e725/config/boards/arm/bbp9981/custom_driver/a320_0x57.c#L201) and change ```scroll_y``` to ```-scroll_y```
