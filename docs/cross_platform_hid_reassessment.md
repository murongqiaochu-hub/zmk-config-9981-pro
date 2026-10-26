# 全端键鼠与手柄：证据重建及候选边界

日期：2026-10-26。仅本地研究与回归，未交付新固件、未刷写或实机验收。

## 当前候选

- 分支 `codex/cross-platform-hid-20261026`，基于 `0603a22`。
- 工作树 `/vol1/1000/dev/9981-firmware/repo/worktrees/cross-platform-hid-20261026`。
- 生产源码暂不修改。选此基线是为了隔离之后的描述符/槽位实验，并非宣称这个版本已恢复安卓或兼容iPad。
- 原工作树、所有已交付UF2及可用iPad键鼠回退保持不变。
- 原版v2.3.3源码构建提交是 `e2c4966136b5e6af5db795c6c46048672758ba30`；`ef911f2` 是UF2 SHA-256前缀，不是源码提交。

## 已重建的事实

1. `git diff 432d6c8 0603a22 -- config` 仅修改 `gamepad_runtime.c` 中MEDIA层判断：最高层order index转换为stable ID。BLE extension、Map、CMake没有变化；workflow、west配置、build.yaml也没有变化。不能继续沿用交接里“原版preinclude与0603a22 readhook不同”的解释。
2. 固定ZMK `edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/keymap.c` 确实返回最高层order index。转换合理；默认未重排时前后等价，不能仅凭改动存在就归罪于它。
3. 本地实际UF2检查：原件618496B SHA `ef911f23e250c55204d9342dea0a6322161e3c4ee624379c8bbe7ed8467c401a`；0603a22同为618496B，SHA `5f17b1d28a7ab88126faaddd1bce18a0cdc3f115187c87c099bda20d21cc2a0e`。二者静态服务序列均1801/1800/180f/180a/1812/Studio，HID26属性，推导句柄28–53。静态一致不能证明空口读出一致，也不证明主机缓存已刷新。
4. iPad键鼠回退11ee89e为619520B，SHA `1e277b08428984a6120c42310c34a6ad40eaaecdf491cbfb1d706e58c3359264`；键鼠HID22属性28–49，Studio50–53，额外HID11属性54–64。该结构与原版确实不同。缓存是待验证因素，不是已定位原因。
5. 丢弃的3d5dd36按全局active profile选Map，每个Read Blob片段重选；请求conn不同于active peer或读取中切槽，会破坏连接的描述符契约。新增独立oracle在当前固定Map上通过，使用历史真实extension替换后在字节比较断言失败。此缺陷不能解释此前0603a22失败。
6. 丢弃的safe Map无LED Output，而配置启用HID_INDICATORS、GATT保留LED Output。属于静态契约不一致；尚未证明是iPad失败根因。

## 测试策略

- 不更改冻结的 `tests/test_gamepad_ble.c` 或SDK fixture；首次尝试在旧oracle加测试被冻结闸拒绝，已完整撤回该文件改动。
- 新增 `tests/test_gamepad_map_stability.c`：复用未改的旧测试TU，三个peer、22字节分片、分片间切active profile/connection，核对整段Map不变。
- `tests/run_gamepad_tests.py` 加入新oracle。
- `tests/run_map_stability_counterfactual.py`：当前实现必须通过，Git历史3d5dd36的真实按槽实现必须在指定字节一致性断言失败，临时隔离构建、不改源码、不访问设备。
- 旧手柄专项strict UBSan通过；新增oracle与历史反事实通过。独立oracle初次拆出时漏初始化mock peer引用计数，已补齐与原main一致的初始化，固定版通过、历史版在指定Map字节断言失败。
- 在dev挂载上完整回归的私有文件权限测试失败（0 != 0600）；直接copytree又继承了不可用权限。未改系统或挂载权限，改为Git archive在临时标准权限目录提取并覆盖本轮测试文件，完整host最终exit 0，日志 `05_Git归档快照完整回归.txt`。之后含协议codec的最终完整回归也exit 0，见 `06_含协议转换原型完整回归.txt`（Map新测试第253行、codec第254行）。本地CMake执行矩阵因工具不可用仍SKIP（第382行），扩展token静态闸运行；未安装工具、未ARM构建。不能用host PASS代替设备验收。

## 已开始的隔离协议原型

新增 `experiments/gamepad_protocol/stadia_codec.h`，只在host测试中编译，不进入生产CMake、不改变设备身份/广播/GATT。它把现有9981九字节逻辑状态转换为参照项目的十字节Stadia格式：方向帽空值15、摇杆中心128且范围1–255、触发器保持0–255、显式字节打包避免C bitfield布局依赖。保留既有按钮语义，未实现的capture/assistant/extra始终为零，异常输入归中并返回失败。

`tests/test_gamepad_stadia_codec.c`覆盖65536按钮字、255个有符号轴值、9个帽状态、256档触发器与异常/空值。这只验证协议数据转换，不证明iOS识别；下一阶段必须补足对应描述符、GATT、订阅与系统验收。参考实现同时改变Google/Microsoft厂商身份，当前没有复制这些身份设置，不能把本原型称Stadia认证或Xbox原生兼容固件。

## iPadOS门槛与候选路线

Moonlight iOS实际枚举 `GCController.controllers` 并要求 `extendedGamepad != nil`。至少区分：

1. 蓝牙配对/服务发现；
2. Map读取与报告订阅；
3. 系统GCController及extendedGamepad；
4. 按钮/摇杆事件与Moonlight实玩。

nRF Connect看到1812只证明服务存在，不等于系统或Moonlight接受，也不能证明第二服务一定被忽略。

优先研究候选：`jfedor2/portable-gamepad-firmware`，固定树 `aa09d1d05939ab0025107f6df359e0b8a3b714c0`。有nRF52840、纯Zephyr GATT、Stadia与PC BLE完整协议实现，README声称iOS兼容，但未找到带iPadOS版本/GCController/Moonlight的独立实测。它是实验参照而非成功保证，不直接移植整套固件到9981，也不擅自冒充其他厂商身份。

推荐技术顺序：纯手柄协议先通过系统接口，再逐项加键盘、鼠标；复合失败时应评估明确独立模式，而非再次按活动槽在同一读取中切Map。现有键位、轻拍、灯光、速度记忆不动。USB与BLE分开验收，Windows标准HID与XInput-only游戏分开验收。

## 第二个本地里程碑：纯手柄声明与GATT原型

已补齐[实验说明](<../experiments/gamepad_protocol/README.md>)、固定182字节参照描述符与16属性纯手柄GATT表，输入ID3/10字节、输出ID5/4字节匹配。未接入生产CMake、广播/身份/模式选择或notify worker；源码config仍与0603a22相同。

独立审查指出NULL合法长度、write flags边界及测试自证问题，已加固：公共写检查拒绝PREPARE/EXECUTE/未知flags和NULL；read输入无效快照归中；全部read长度/offset边界与诊断实参断言；实际GATT样本按HID字段独立解码。缺Output、多LED、错ID、重复ref、hat/output位宽和button usage共7种契约反例均被拒绝。

新增原型后的完整host在标准权限Git归档快照exit0（07日志），审查补强专项exit0（08日志）。CMake执行矩阵仍SKIP；没有ARM/空口/GCController/Moonlight结果。上游仅输入GATT与其含Output描述符不一致的细节被记录，不推定它是苹果不识别的根因。

### 第三个本地里程碑：连接/发送桥接

新增实验 `pure_transport.h`，对齐既有worker的captured_conn/body/context_epoch接口，不新建队列。完成按连接持有引用、有界状态表、挂起与重连代次、当前peer/路由/runtime epoch检查、旧peer neutral释放、10字节编码notify调用及固定RAM诊断。尚未在生产CMake中链接，也未将生命周期接口注册到实际BT回调；SDK/runtime在该专项中仍是替身。

独立审查找到并修复两项真实模型缺口：

- 聚合CCC回调不足以观察某个peer的off/on，改为managed cfg_write逐peer推进代次并取消旧epoch；双peer聚合保持1时的反例必须失败。同值写也保守取消，未知连接映射为有效ATT错误而不泄漏内部errno。
- connect检查与登记间插入disconnect可能保留引用，改为登记后复核、仅按本次registration清理；回调内注入断开证明refs回到初值。

此外将最后info检查后的ticket/runtime epoch/route再次核对，并明确最终闸门之后允许单worker的一帧在途SDK提交，不宣称检查与空口原子。10000次生命周期、跨槽重连、旧peer释放、挂起/订阅取消、有限诊断与反事实已验证。完整host回归exit0见09日志；最后ATT错误映射补强后的手柄专项另见10日志。复核者已只读确认上述三处落实，不将其当作独立运行结果。

固定Zephyr源码还核实notify载荷在函数返回前复制进ATT PDU，但加密/订阅/缓存仍需真实协议栈验收。下一步是把实验adapter与真实生产worker、GATT服务选择及BT回调接线，并补真实连接层验证；不能把已写实验代码直接称作可刷模式。

### 第四个本地里程碑：可生成的显式诊断profile

新增 `tools/stage_pure_gamepad.py`，默认源码不变，目标为新的外部目录；候选只机械替换两份CMake。stock hog.c保持原字节，prefix改为不注册旧HID、公共蓝牙键鼠API拒发的guard；独立pure_runtime TU提供既有send ABI及BT连接/断开回调。除两项CMake外所有config字节一致，不更改USB、键位或身份设置。此profile是**纯BLE手柄诊断候选，蓝牙键鼠不可用**，不是最终三合一。

生成器拒绝覆盖/源树目标/源子项symlink/未知片段；解析并校验描述符实际字节哈希后才生成manifest，避免写死哈希漂移。host验证完整固定SDK hog.c的5种配置、公共键鼠拒发、native iterable注册marker及恢复旧服务注册的反事实；实际wrapper/callback函数也被测试。独立只读审查未发现确定的宏顺序或双注册问题，明确指出还缺真实三TU链接及ARM iterable表验收。

本机初无CMake；核对记忆/进程/已知路径后，仅在临时测试目录准备3.31.6，无全局安装/配置变更。实际CMake解释器运行候选16组前置条件、唯一prefix/source轨迹、USB轨迹比较和SDK失败闸；同时原32组CMake矩阵不再SKIP。Zephyr API与身份探测仍为测试harness，不冒充完整SDK configure。完整host日志11和含CMake矩阵日志12均exit0。

下一步为真实固定SDK/ARM候选构建，检查compile_commands、符号唯一、最终ELF iterable表只有一个HIDS/16属性、DTS/config/UF2与manifest，然后才讨论受控实机验证。进入构建交付批次须同步候选说明书；当前未推送或运行Actions。

## 下一道决策与验收

用户本轮已确认：iPad Air 6、iPadOS 26.2、官方Moonlight（具体App版本未提供）；接受先验证明确键鼠模式／纯手柄模式，再推进合并。此方案不是已完成的三合一，也不等于授权更改厂商身份、刷写或清配对。

安卓原验证入口是盖世游戏的手柄测试，iOS修复之前测试成功；目前不能把修复后盖世测试失败写成Android系统不枚举。安卓回归需区分“系统根本不枚举”和“枚举但MEDIA不发事件”。优先观测完整Map、ReportRef、ID4 CCC及按键notify，不能要求无目的重复刷机；任何清配对、刷写或控制设备另取精确授权。

### 本轮新增云构建授权（交互问答已确认）

用户选择“允许候选推送与构建”：可推送本轮改动至 `murongqiaochu-hub/zmk-config-9981-pro` 的独立分支 `codex/cross-platform-hid-20261026`，运行纯BLE诊断候选GitHub Actions最多3次，含必要编译修复重跑。授权时已使用0次；后续逐次记录run ID与结果，不因自动续轮重置计数。

执行顺序：同步候选说明书 → 准备精确诊断构建入口 → 候选推送/Actions → 检查真实ARM、ELF iterable唯一HIDS/16属性、compile_commands、DTS/config和UF2来源 → 再讨论实机测试。授权不含发布、刷写、清配对、改主机、推稳定分支或厂商身份替换；蓝牙键鼠在此诊断候选中关闭，USB源码/键位保留不等于已通过实机回归。

本轮授权记录在此文档，不依赖自动goal轮次中的目标编辑（运行时只允许顶层人工轮次编辑目标）。

### 构建前准备与计数

- 新增诊断P1补充说明，同批PDF（5页）、可编辑Word与内嵌字体离线HTML已导出；Word以OOXML重新解析、PDF抽取核对关键内容，PDF首尾页已目视检查。当前保留“待ARM/未刷写/未实机”的构建前口径。首次导出错误地调用了要求四张键图的旧API，已改用独立文字型导出，不更改或放宽原手册四图校验。
- 指定11ee89e回退原件已只读核验：619520字节，SHA-256 `1e277b08428984a6120c42310c34a6ad40eaaecdf491cbfb1d706e58c3359264`；原件未改。
- 现有main工作流新增手动profile输入，默认仍standard；pure-ble仅允许本授权repo/branch，令牌contents只读，旧build在pure时跳过，既有deliver分支/触发条件不放宽。以`[skip ci]`推送，再显式手动触发，避免顺带构建默认配置。
- 验收器新增ARM32最终iterable表唯一HIDS/16属性、Map/ReportRef/权限/characteristic检查，三份编译命令唯一及prefix边界，UF2应用区与已验证ELF flash载荷逐字节对应。合成布局/编译命令反事实和完整host回归均exit0（日志14）；这还不是ARM通过。
- **Actions计数：3/3，额度已用尽，不得再自行触发或rerun。** 第3次已于2026-10-26 17:35（Asia/Shanghai）触发：[Run 40300798791](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/40300798791>)，来源 `797351be08b64af47e32f910730878cb6ff7b836`，profile=pure-ble，**整体success**：regression、真实ARM和后置产物验收全部通过；下载后重算ELF/编译命令/UF2与云端验收JSON一致。第三次UF2与第二次逐字节相同（SHA-256仍为52f9385e…1e1f6d）。同批最新版说明书在任务目录24，候选与原始验收JSON在目录27。尚未刷写或实机验证。
- 第2次已于2026-10-26 16:43（Asia/Shanghai）手动触发：[Run 40294260252](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/40294260252>)，来源 `785248655b9808d03c18ff1f2855ae2e3c649f98`，profile=pure-ble：regression、SDK初始化和真实ARM编译/链接均成功；工作流在后置验收器误认连接回调符号名时失败。该失败仍计入额度。
- 第1次已于2026-10-26 16:29（Asia/Shanghai）手动触发：[Run 40292629930](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/40292629930>)，来源 `7fcb0f07c8492e72507d2511140563daaa69da84`，profile=pure-ble，已失败：regression完整通过；容器中的source checkout触发Git dubious ownership，停在生成候选之前，未进入SDK初始化或ARM。推送含`[skip ci]`，没有另触发默认配置构建。失败仍计一次，不得因失败或自动续轮重置。
- 修复限定在stage这一个CI步骤的命令环境：`GIT_CONFIG_COUNT=1`及`safe.directory=${{ github.workspace }}`，仅信任已授权checkout的精确路径；不使用通配符、全局Git配置或chown/chmod。已用Git的不同owner测试开关复现，证明授权目录可用、兄弟目录仍被拒绝。
- 首次云前只读复核指出regression原先继承actions:write；现已显式收紧为contents:read并关闭凭证持久化。验收器另补Map读取回调指针、UF2额外尾块/非填充间隙检查；报表中的10/4字节明确属于描述符定义，真实空口长度仍未验证。
- 第二次产物已下载并完成本地复验，固件源码无需修复：固定SDK的BT_CONN_CB_DEFINE实际生成`bt_conn_cb_bbp_exp_connection`，不是原验收器假设的裸名。已改为检查真实callback iterable范围、记录大小及connected/disconnected函数指针，现有runtime回调与实验回调均在表中。
- 另一处验收器假设也已纠正：PT_LOAD包含NOBITS/对齐空洞，原始ELF文件中为00而objcopy以FF填充（本产物共15字节），不能将这些空洞当实际程序字节。现在严格逐字节比较SHF_ALLOC的文件载荷节，经PT_LOAD换算LMA；只有空洞/对齐/尾部填充允许00/FF，额外尾块仍拒绝。
- 从`7852486`干净Git archive重新运行生成器，所得manifest与云端原件**完全相同**；修正后的完整CLI离线复验通过（记录22）。真实ELF：唯一HIDS、16属性，ID3/10字节输入和ID5/4字节输出描述符，182B Map哈希符合固定值；两项连接回调已注册；三TU与guard边界正确。UF2为612864字节、应用区`0x1000..0x4bd00`，实际306357字节ELF节载荷全部匹配，SHA-256 `52f9385ef8289dac8a8c4c95929eaaa0a57ab3b784f27c5d3846677bef1e1f6d`。这证明ARM/静态产物，不证明通知或任何设备兼容。

## 原始来源

- https://raw.githubusercontent.com/zmkfirmware/zmk/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/keymap.c
- https://github.com/moonlight-stream/moonlight-ios/blob/master/Limelight/Input/ControllerSupport.m
- https://github.com/jfedor2/portable-gamepad-firmware/tree/aa09d1d05939ab0025107f6df359e0b8a3b714c0
- https://raw.githubusercontent.com/jfedor2/portable-gamepad-firmware/aa09d1d05939ab0025107f6df359e0b8a3b714c0/app/src/hids_zephyr.c
- https://learn.adafruit.com/adafruit-bluefruit-le-shield/ble-services#at-plus-blehidgamepaden-2435778
