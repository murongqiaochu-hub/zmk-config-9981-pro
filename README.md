# zmk-config-9981-pro
This page contains zmk firmware config for 9981 keyboard with trackpad(pro version)

## 当前：互补二分测试（2026-10-25）

第一刀用户实测仍失败。本轮以正常v2.0为底，仅接回BLE手柄描述/服务面（337B Map），不含USB手柄、MEDIA手柄绑定和触板接管。BLE手柄报告只读中立值，不主动发游戏输入；不是可玩手柄版。先测iPad键鼠可用性，确认后才继续。同步[说明书](<docs/9981Pro_使用说明书.md>)v2.2.1.4及[维护规则](<AGENTS.md>)。

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
