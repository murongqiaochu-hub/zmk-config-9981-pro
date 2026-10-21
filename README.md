# zmk-config-9981-pro
This page contains zmk firmware config for 9981 keyboard with trackpad(pro version)

## 当前使用说明书与维护约定

- 完整中文说明：[9981 Pro 使用说明书](<docs/9981Pro_使用说明书.md>)（v2.2.8，2026-10-21，**MEDIA / Xbox 布局标准 HID**，非原生 XInput；已构建未发布候选，完整host / ARM / UF2验证通过，未实机验收）。aA 双击锁轮默认0°，解锁/重锁回正；按住 aA 临时恢复原方向/原生肩轮，松开恢复所选固定档。沿用 v2.2.7 的临时/一次性/固定层 owner 仲裁，旧 sticky 的3秒超时/迟到UP不撤固定 UPPER；SYM退层不解 scroll latch，锁轮肩键半速/屏蔽自身Wheel不变。
- 仅 MEDIA：View 从 T/P11 移到 **R/P10（action12）**，Menu 从 Y/P12 移到 **U/P13（action13）**，原 T/Y 留空；其余保持 v2.2.7：W/P8↑、D/P19→、Z/P28↓、A/P17←，S/P18=L3、L/P25=R3（**非左杆**），肩P0/P1=数字LT/RT，Call/End P2/P6=LB/RB，O/K/Backspace/$=Y/X/B/A，P4/Space=Guide。19绑定/2出口/21None，仅SYM/右aA退出，$在本层是A。
- **固定档，不累加：** 松开aA且锁轮时，P3黑莓完整三击直接选左→上、右→下、上→右、下→左档；P5顶部返回完整三击直接选右→上、左→下、上→左、下→右档。同键重复幂等，换键直接选另一目标，不在原档继续转90°；其他点按吞掉，三击180/220ms、aA锁轮双击450/350ms不变。**按住物理右aA时P3/P5立即选槽1/2，即使已锁轮**；右aA按住+F/P20双击鼠标翻转不变：普通指针Y反向、DEFAULT肩键角色互换，其余不变，volatile、重启复位。
- 触板滑动默认左杆 X/Y，按住黑莓临时右杆 Z/Rz，松开不锁定，切换清旧轴且只用新样本。机械点击/空格固定Guide，S/L独立L3/R3。velocity不累计位置，停住手指不能保持倾斜；120ms无有效位移仅本地归零，非host ACK；单触板不能同时独立控两杆，见手册第26章。
- 游戏层 driver 独占触板并有晚发送 gate，不同时发鼠标、滚轮或轻拍；退出恢复原鼠标。轻拍原参数、开机 OFF、普通层三击开关不调优，游戏层仅临时阻断和取消旧候选，不自动改开关值。USB 独立 HID_1：ID4 + 9-byte body = 10 bytes；BLE HOG 发 9-byte body、无 ID。
- **当前 v2.2.8 功能源码 SHA `88edf7f323b643465fa1470fdc2e090dd3ee0ffa`，候选分支 `codex/xbox-layout-v2.2.8-20261021`，完整host exit0与70配置/四层42键图同步PASS；构建提交 `ffc41698f6972f3a26cc4ce459956a06fe8c3a46`，[Run39557844223](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39557844223>) regression/ARM与新UF2本地边界校验PASS，候选deliver跳过，未刷未验。** 本版定向strict UBSan（固定档绝对选择/幂等/互切、真实A320双顺序四向/aA覆盖、preset向量/饱和）及scope45旧配置/8集成+16新增PASS，非完整host。用户于2026-10-21 14:33确认 v2.2.7 蓝牙按钮已恢复、触板三击已生效、手柄基本可用；固定档方向语义和R/U菜单仍待新版，不代表全场景硬件验收。历史 v2.2.7 [Run 39551522029](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39551522029>)、构建 `045a7a3a0da9ba372d99ea747e27a15ababb8694` 与 UF2 SHA `140d608be34b35c237fcc11db8c8bd56c5b55c453c9cae921b2c753750887bc6` 仅证明旧件，不能用于 v2.2.8。v2.2.4仍是最后已发布/归档版本；详见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)与[实现说明](<docs/gamepad_implementation_notes.md>)。非XInput、数字扳机、无振动/Share、Guide截获与矩阵多键风险不变。
- 用户新增规则：**以后每次更新功能或默认键位，必须在同批次同步说明书、四层键图及版本/验证状态。** 维护流程见[维护规则](<AGENTS.md>)；[说明书同步检查](<tests/check_manual_sync.py>)已纳入主机回归，但本轮最终结果以实际日志为准，不用同步哈希代替语义或实机核对。
- 下文仅保留上游仓库历史构建引导，不能作为当前版本或后续任务的授权依据；本项目构建授权与范围按[维护规则](<AGENTS.md>)执行。例如旧 Caps Lock 滚动示例不适用于本版（普通键鼠层用右 aA 按住或双击锁轮）。


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
