# zmk-config-9981-pro
This page contains zmk firmware config for 9981 keyboard with trackpad(pro version)

## 当前使用说明书与维护约定

**2026-10-26 当前：v2.3.3.3 单一HID合成报告候选（手册v2.3.3.3）。** 分支 `codex/ipad-v233-mapfix-20261026`，基线原v2.3.3 `432d6c8`。BLE 不再向原 196B 键鼠 Map 尾部追加（iPad 拒绝、安卓接受），改为由本固件重建 238 字节合成 Map：键盘 ID1、媒体 ID2、鼠标 ID3、手柄 ID4，报告体 8/12/9/9 字节与 ZMK 完全一致，可单次读取。MEDIA 层判断同时改为 stable ID。iPad 键鼠已实测通过，手柄与各 App 待验；不刷写、不清配对。

**2026-10-22当前：v2.3.3指针五档记忆/MEDIA慢呼吸候选。** 用户已刷v2.3.2，反馈改善有限，随后明确要求Z循环调速、重启保留及MEDIA键盘灯改慢呼吸。仅UPPER Z/P28循环0.6→0.9→1.2→1.5→1.8，首次1.2、Ctrl为所选档半速；独立设置键在末次操作1秒后合并保存。普通Z/手柄Z↓不变，不冒称硬件DPI。MEDIA键盘背光改约3.6秒10..100..10%慢呼吸，乘用户上限且尊重关灯/idle；其他层、触板灯和两灯保存语义不改。保留v2.3.2防抖/削峰/BLE屏障及原轻拍资格结构；新帧锁存保证影子与实际运动同档，切档清旧状态。范围、软件验证及未验证项见[本轮说明](<docs/cursor_preferences.md>)、[当前交付](<firmware-delivery/XBOX_CURRENT.md>)。修正后Run39743108339回归/ARM与实际DTS、UF2校验通过（604KiB，未删功能）；首次缺Z构建已拒交隔离，最终件见交付记录；不刷写、不清配对/settings、不改主机、不推稳定/发布或归档，不以600KB删功能。v2.3.2 Run39725910232与下方旧版均是历史构建证据。

- **以下是v2.3.1加固阶段的构建证据**，源码 `7d6ab94e57653deb8e20d142665eee351736f297`，分支 `codex/firmware-stability-v2.3.1-20261021`。只修USB轻拍快照的异步栈源寿命、等待期间切BOOT协议两处已复现缺陷；79/80配置逐字节相同，键位/参数/灯效/存储均不变。旧失败/新通过ASan、连续模拟、完整host、Run39614649971 ARM/CI32配置9负例及UF2校验PASS，见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)；非实机长测认证。原生HID_0全局DMA/信号量限制未全修，具体范围见[稳定性说明](<docs/firmware_stability.md>)、[可选排错资料](<docs/long_term_acceptance.md>)。下方v2.3.0为历史重构记录，不是本版构建结果。

- **历史 v2.3.0 是行为保持型全局重构候选**，基线 v2.2.9 `f7412ec90c925487617a1e8d40dd886c310142a6`，重构源码 `5c82f7ac3a4fa0acd5d4e2ae504a0fc5f5659fde`。键位、手势时序、灯效/存储、协议/SDK门与保护限制不变；拆分模式服务、滚轮纯核、共享PWM后端、手柄runtime/adapter和板级构建职责。架构、验证与回退见[架构说明](<docs/firmware_architecture.md>)；专项、68万事件旧新差分与独立TU链接已PASS，当前完整host exit0、80配置/四层图同步、Actions Run `39569977363` regression/ARM、云端32配置CMake及9负例、UF2校验均PASS；最终云DTS全文和1166项Kconfig与v2.2.9一致。未刷写/未验收，不推稳定分支或发布/归档。下方标注v2.2.9及更早的构建结果均为历史，不能冒作重构版结果。

- 完整中文说明：[9981 Pro 使用说明书](<docs/9981Pro_使用说明书.md>)（手册v2.3.3，2026-10-22，对应固件v2.3.3候选，**MEDIA / Xbox 布局标准 HID**，非原生 XInput；本轮构建/校验状态见交付记录，未实机长测）。aA 双击锁轮默认0°，解锁/重锁回正；按住 aA 临时恢复原方向/原生肩轮，松开恢复所选固定档。沿用 v2.2.7 的临时/一次性/固定层 owner 仲裁，旧 sticky 的3秒超时/迟到UP不撤固定 UPPER；SYM退层不解 scroll latch，锁轮肩键半速/屏蔽自身Wheel不变。
- 仅 MEDIA：View 从 T/P11 移到 **R/P10（action12）**，Menu 从 Y/P12 移到 **U/P13（action13）**，原 T/Y 留空；其余保持 v2.2.7：W/P8↑、D/P19→、Z/P28↓、A/P17←，S/P18=L3、L/P25=R3（**非左杆**），肩P0/P1=数字LT/RT，Call/End P2/P6=LB/RB，O/K/Backspace/$=Y/X/B/A，P4/Space=Guide。19绑定/2出口/21None，仅SYM/右aA退出，$在本层是A。
- **固定档，不累加：** 松开aA且锁轮时，P3黑莓完整三击直接选左→上、右→下、上→右、下→左档；P5顶部返回完整三击直接选右→上、左→下、上→左、下→右档。同键重复幂等，换键直接选另一目标，不在原档继续转90°；其他点按吞掉，三击180/220ms、aA锁轮双击450/350ms不变。**按住物理右aA时P3/P5立即选槽1/2，即使已锁轮**；右aA按住+F/P20双击鼠标翻转为v2.2.8时的旧键位，现已迁至M/P34：普通指针Y反向、DEFAULT肩键角色互换，其余不变，volatile、重启复位。
- 触板滑动默认左杆 X/Y，按住黑莓临时右杆 Z/Rz，松开不锁定，切换清旧轴且只用新样本。机械点击/空格固定Guide，S/L独立L3/R3。velocity不累计位置，停住手指不能保持倾斜；120ms无有效位移仅本地归零，非host ACK；单触板不能同时独立控两杆，见手册第26章。
- 游戏层 driver 独占触板并有晚发送 gate，不同时发鼠标、滚轮或轻拍；退出恢复原鼠标。轻拍原参数、开机 OFF、普通层三击开关不调优，游戏层仅临时阻断和取消旧候选，不自动改开关值。USB 独立 HID_1：ID4 + 9-byte body = 10 bytes；BLE HOG 发 9-byte body、无 ID。
- **v2.2.9仅调整 UPPER 灯光快捷键与鼠标翻转键：** 五项灯光控制各向左移一个物理键，X/P29=键盘 RGB_BRD，C/P30=触板 BL_DEC，V/P31=键盘 RGB_TOG，B/P32=触板 BL_INC，N/P33=键盘 RGB_BRI；右 aA 按住时 M/P34 双击切换鼠标翻转，F/P20 在 UPPER 无输出。其余功能与 v2.2.8 固定档/R-U菜单候选相同。功能源码 `ec6e492ca452b19cf3fa1ec35e7a72f0774fc43e`；Actions Run `39563315080` regression/ARM、完整host、UF2与PDF/DOCX均通过；UF2 SHA `8a3506d9ad47b0ca033fea07f7e2bf8724d1e4ac434b2a789a2826ff859e907e`。未刷写、未实机验收。
- **历史 v2.2.8 功能源码 SHA `88edf7f323b643465fa1470fdc2e090dd3ee0ffa`，候选分支 `codex/xbox-layout-v2.2.8-20261021`，完整host exit0与70配置/四层42键图同步PASS；构建提交 `ffc41698f6972f3a26cc4ce459956a06fe8c3a46`，[Run39557844223](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39557844223>) regression/ARM与新UF2本地边界校验PASS，候选deliver跳过，未刷未验。** 本版定向strict UBSan（固定档绝对选择/幂等/互切、真实A320双顺序四向/aA覆盖、preset向量/饱和）及scope45旧配置/8集成+16新增PASS，非完整host。用户于2026-10-21 14:33确认 v2.2.7 蓝牙按钮已恢复、触板三击已生效、手柄基本可用；固定档方向语义和R/U菜单仍待新版，不代表全场景硬件验收。历史 v2.2.7 [Run 39551522029](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39551522029>)、构建 `045a7a3a0da9ba372d99ea747e27a15ababb8694` 与 UF2 SHA `140d608be34b35c237fcc11db8c8bd56c5b55c453c9cae921b2c753750887bc6` 仅证明旧件，不能用于 v2.2.8。v2.2.4仍是最后已发布/归档版本；详见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)与[实现说明](<docs/gamepad_implementation_notes.md>)。非XInput、数字扳机、无振动/Share、Guide截获与矩阵多键风险不变。
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
