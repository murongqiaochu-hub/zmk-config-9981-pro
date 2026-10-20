# zmk-config-9981-pro
This page contains zmk firmware config for 9981 keyboard with trackpad(pro version)

## 当前使用说明书与维护约定

- 完整中文说明：[9981 Pro 使用说明书](<docs/9981Pro_使用说明书.md>)（v2.2.4，2026-10-20，当前为 **MEDIA / Xbox 布局标准 HID** 本地键位候选，非原生 XInput）。aA 双击锁轮逆时针旋转 90°并固定 UPPER；锁定时任一肩键使触板滚轮半速且屏蔽自身滚轮；长按 aA 暂时恢复原方向与原生肩键滚轮。用户已撤回未发布 GBA 方案；旧 `abe65ea` / `89fbe9d` 仅为本地撤回历史，不是可刷当前 Xbox 固件。默认键图、物理监听与设备实际 Studio 覆盖须分别核对。
- 仅 MEDIA 层：E/X/S/F＝D-pad 上/下/左/右，D/K＝L3/R3（独立键，**非左杆**）；触板机械点击与空格都发 Guide/Xbox；I/J/L/M＝Y/X/B/A；通话/结束＝LB/RB，双肩＝数字 LT/RT；实体 T/Y＝View/Back、Menu/Start。N、Enter 留空；$ / SYM / 右 aA 三出口仍回普通层，方向、面键、肩与菜单不发字母。
- 触板滑动默认控制左杆 X/Y；按住物理黑莓才控制右杆 Z/Rz，松开不锁定，切换先清旧轴且只用新样本。机械按压按 Guide/Xbox 键处理，**不作 L3/R3**；空格也发 Guide。velocity 虚拟杆不累计位置；停住手指不能保持倾斜，120ms 无有效位移只保证本地归零策略，不是主机收包时限。D/K 分别独立发 L3/R3；触板也不能独立控制两杆，详见手册第 26 章。
- 游戏层 driver 独占触板并有晚发送 gate，不同时发鼠标、滚轮或轻拍；退出恢复原鼠标。轻拍原参数、开机 OFF、普通层三击开关不调优，游戏层仅临时阻断和取消旧候选，不自动改开关值。USB 独立 HID_1：ID4 + 9-byte body = 10 bytes；BLE HOG 发 9-byte body、无 ID。
- **v2.2.4 新映射已推送并 ARM 构建：GitHub Actions Run 39428313318 与 UF2 边界校验通过；当前固件见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)。** 旧 Run 39361865259 的 UF2 不含 E/X/S/F、D/K、触板 Guide 与锁轮更新，勿用。滚轮/肩键专项、完整 `run_host_tests.py`（含键图与说明书同步）均 PASS；未 USB / BT 实机枚举、刷写或 App 实玩。Windows XInput-only、Xbox 主机、安卓模拟器 / Moonlight / 盖世 / Droidsteam 各版本均不保证兼容；LT/RT 仅 0/255，非半按，无振动或 Share，Guide 可能被系统截获。移动 + 黑莓 modifier + 开火等多键仍须实测矩阵鬼键风险。状态与回退见手册第 19、23–25 章，技术边界见[实现与验证说明](<docs/gamepad_implementation_notes.md>)。
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
