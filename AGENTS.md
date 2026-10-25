# 9981 Pro 固件：开发与说明书维护规则

## 当前说明书入口

- 面向用户的可维护正文：[9981 Pro 使用说明书](<docs/9981Pro_使用说明书.md>)。
- 默认键位真源：[主键图](<config/bbp9981.keymap>)和[板级键图](<config/boards/arm/bbp9981/bbp9981.keymap>)，两份实际绑定必须一致；C 驱动中的物理位置监听也必须核对。
- 版本与源码核对清单：[manual_manifest.json](<docs/manual_manifest.json>)。
- 导出工具：[build_manual.py](<tools/build_manual.py>)与[print_manual.mjs](<tools/print_manual.mjs>)。历史 PDF / Word / HTML 留存，不覆盖旧版。

## 用户明确新增的设定（2026-10-19）

**以后每次更新功能或默认键位，必须在同一批次同步更新说明书；文档同步是交付验收的一部分，不是事后待办。**

1. 范围包括：键位、各层/透明或禁用键、组合键、物理监听、短按/长按/单双三击时间、触控板/滚轮/轻拍、灯光、亮度与其他记忆、连接/输出、电源/空闲、维护键及重要限制。
2. 修改实现时同时维护正文的操作、示例、排错、限制与版本记录；若相关配置只作非功能修改，也须明确审阅并刷新核对清单，不能盲刷哈希掩盖文档过时。
3. 保留“源码提交、构建、已交付、用户刷入、实机反馈”的区分。最新候选不能自动称为用户已安装或已验收。运动启发式不是可靠触点检测，历史回放候选数不是命中率。
4. 若改键，重建四层各 42 键的图，检查默认键图与驱动物理位置监听是否仍一致。若改功能，正文先写清新旧差异与适用范围，再刷新清单。
5. 文档版本至少递增修订号，并记录日期、功能源码版本、验证状态、已知问题。导出 PDF、可编辑 Word、离线 HTML 供用户阅读；文字、图示与所有导出都应来自同一正文和键图。
6. 每批次都运行 `python3 tests/check_manual_sync.py`；完整主机回归已经包含此检查。`config/` 任一受跟踪文件增加、删除或内容变化而未同步清单会失败。同步检查只是防遗漏，不替代人工核对说明是否真实。
7. 仅写说明书不授权改固件功能、自动触发新 ARM 构建、推送、刷写、清配对或主机设置。2026-10-20 用户已撤回未发布 GBA 候选，另明确选择 **MEDIA / Xbox 布局标准 HID**，不是原生 XInput，不冒充微软 VID/PID；不得继续发布 GBA 或称 Xbox 已交付。轻拍继续暂停调优。2026-10-20 10:44 用户已精确批准本次 Xbox 分支推送 / Actions 云编译 / UF2 校验交付；仅本次新分支，不发布旧 GBA 历史。刷写、清配对等仍须各自精确授权，不由历史文档任务推定。

## 本轮诊断构建授权（2026-10-25）

- 用户明确“整！直接构建，我直接刷，刷好了我测试，过不过都告诉你”：授权基于 `17192e6` 实现仅增加隐私安全、有界观测的 v2.2.1 诊断候选，专属分支 `codex/ipad-enumeration-diag-v2.2.1-20261025`，本次可推送精确仓库 `murongqiaochu-hub/zmk-config-9981-pro` 并运行既有Actions、下载校验交付单份UF2。不得扩大稳定/release或自动归档门。
- 不改描述表、身份、GATT属性顺序、键图、发送策略、权限/安全等级策略、配对/settings；保留键鼠及USB/BLE手柄。只观测，不以诊断代码主动发送HID、配对、断连或设置连接参数。原v2.3.3不动，不自动刷写。
- Mac两轮实测95条路由心跳均BLE，三组鼠标实际notify ret0。候选 `CONFIG_BT_GATT_ENFORCE_SUBSCRIPTION=y`，故支持当时固件鼠标订阅检查通过；可能是旧bond恢复，不能外推本次订阅写入、键盘CCC或iPad识别。
- 新诊断独立于旧30秒输入诊断门；仅匿名连接、security/MTU/CCC、读取offset/长度/返回值、notify句柄/长度/返回值与计数，不输出键码、输入payload、地址、序列号或密钥。不得通过开启全局BT DEBUG泄露隐私。构建通过不是iPad修复。

- 本轮交付：功能源 `cb20e1473931fcc5c829f4ecfe385497d498f048`，实际构建 `b929f2fe20702c47fd6b4423bae22fe8c94eff98` / Run `40184337014` / artifact `12595661599` 成功。UF2 609792B，SHA256 `b8d8e74d079cc3f94380360203d80c3cbaf60a329eb58ce6969340e6320f8f4e`；仅应用分区 `[0x1000,0x4b700)`。原生配置仅新增IPAD_DIAG=y，DT29300B完全相同；旧196B及手柄141B描述符各两处保持。完整host与云端regression/ARM PASS，新映像CDC debug3/4、Studio0/1离线核验PASS；未自动刷机，等待用户反馈。
- 首次Run `40183946346` 因云容器Git ownership检查止于CMake，无UF2；已仅用命令级精确仓库safe.directory修复，不改全局配置。后续文档提交使用skip-ci，不重复构建或自动归档。

## 最新实机反馈（2026-10-25）

- 用户确认唯一元数据候选（实际构建 `17192e6` / Run `40082379873` / UF2 SHA256 `9dd7bbf5e33ef2a414e7725994c710131c9036e751f322f7009783e08e4691fe`）仍失败：iPad显示蓝牙已连接但键鼠无反应，且设置未出现硬件键盘选项。不得继续称“未实测”或把旧软件/ARM PASS写成iPad成功；尚无设备端固件哈希回读，候选对应关系来自本轮交付后的直接反馈。
- 保持原v2.3.3不动，不移植失败方案，不继续盲修元数据，不自动刷写、清配对/settings或改主机。下一步先收敛HOG发现/安全/主机绑定证据；“已连接”不证明加密、Report Map完整读取或CCC订阅成功，缺菜单也不能独自证明根因。
- 反馈与本轮诊断材料：[实测失败记录](</vol1/1000/projects/output/1025/150646_iPad已连接无输入_枚举诊断/01_iPad实测失败记录.json>)。以下2026-10-24构建数据保留为软件历史，不是设备验收结果。

## iPad 元数据候选合同（2026-10-24，实测已失败）

- 当前分支 `codex/ipad-hid-metadata-v2.2.1-20261024`；用户要求先对照正常 v2.0 `28f6c86` 与失败 v2.2.1 `1c61b81`，先交单份最小修正 v2.2.1，再由用户 iPad 实测；确认成功才移植原 v2.3.3 `432d6c857382769963afd65de0f803a303a67e59`。不能擅自继续 v2.3.4 单主机路线。
- 功能源 `f1d862ffc22ad7371b5a59471b11e086764cf5d6`：仅 HID Information bcdHID `0000→0111`、USB-IF PnP Source `1→2`、标准 DIS 软件版本 `2.2.1-ipad-hid1`。63 份配置恰 4 处受控改变、59 份字节相同；168 绑定、原 USB/BLE 手柄、键鼠、生命周期/路由、权限、配对及 settings 不删改。DIS 新增两属性会改变数据库和可能的绝对句柄，不能称已清缓存或 HOG 绝对句柄不变。
- 两项旧元数据问题也存在于正常 v2.0；“加入 Game Pad 后触发更严格主机处理”只是待实机验证假设，不得写成已定位根因或已修好 iPad。新测试固定真实 337B map，不能用精简 mock 的 map 代替；旧 BASE、SDK fixtures、旧 oracle 和原回归保留。
- 本轮仅获精确仓库 `murongqiaochu-hub/zmk-config-9981-pro` 的本候选分支推送、既有 Actions ARM 构建、单份 UF2 下载校验授权；不推稳定/release、不扩大既有 auto-deliver 旧分支闸门、不归档回推、不刷写、不清配对/settings、不改主机或 MAC，不读取/发布凭证或网络身份。
- 当前手册 v2.2.1.1；本轮新增真实 HID 16组合 UBSan＋ASan、特定 OFF 负例、旧范围闸及完整主机回归均 PASS（exit0）；ARM构建 `17192e63fdc94e8080d61f89cd97724339bd64c7`、Run `40082379873` 及605184B单份UF2校验PASS，SHA256 `9dd7bbf5e33ef2a414e7725994c710131c9036e751f322f7009783e08e4691fe`；原生DT全文相同、旧正向Kconfig仅PnP来源改变另增3项，auto-deliver旧分支闸门保持且实际SKIP。未刷写、未 iPad 验收。状态须随本轮实测证据更新，同批导出 PDF/Word/离线 HTML；见[对比与修正说明](<docs/ipad_hid_metadata.md>)。下面保留原 v2.2.1 的布局/参数合同，其历史状态不替代本轮结果。

## 原始 Xbox 标准 HID 合同（2026-10-20，布局沿用）

- 原始文档为 v2.2.1，现修订手册 v2.2.1.1，保留原普通 / SYM / UPPER 说明和参数。手册原 18–23 章号不变，第 26–27 章说明 velocity 虚拟摇杆的使用、参数与鼠标隔离；功能源码只在手册第 23 章单点回填，未填前不冒称最终功能提交。
- 仅 MEDIA：WASD 是 D-pad，不是左杆；I/J/L/M＝Y/X/B/A；通话/结束＝LB/RB；双肩＝数字 LT/RT（0/255）；实体 T/Y＝View/Back、Menu/Start；空格＝Guide/Xbox。黑莓按住换右杆、松开不锁定；机械触板 DOWN 捕获 L3/R3，切模式不重映射已按住按钮。17 个 gamepad 位置、3 个回 DEFAULT 出口、22 个 None；原 J/K＝B/A 已废止，K/N/Enter 留空。
- 触板只有相对光学位移：默认左 X/Y、按住黑莓右 Z/Rz，raw 右正下正；velocity 不累计位置。默认 gain8 / 逻辑 deadzone4 / 75%新+25%旧对称舍入，最大输入首帧为 ±95 而非满量程；有效位移续时，120ms 无有效位移本地归零，GPIO/I2C 故障清轴但不代替按钮 UP。不能把本地计时写成 host ACK 或收包时限；停住手指不能保持杆倾斜，单触板不能独立双杆或同时 L3/R3。
- 游戏层新增 driver 独占分支、晚发送 gate 及轻拍临时保护 / 取消旧候选，退出恢复原鼠标；不能声称触板 driver / microtap runtime / 输出 bridge 整文件未变。三者仅新增 GP 所有权与代际门；原 gesture 参数、开机 OFF、三击开关、桥接 I/O / 必要 UP、灯光和 GPIO / 矩阵保留。范围闸验证 46 份旧配置字节相同、3 份源去门后精确恢复（仅归一历史 CRLF）。P37 slow、P41 滚轮物理监听仍存在，但游戏层不发鼠标 / 滚轮，P37 不是摇杆加速。
- 标准 HID ID4：body9，USB10（aligned staging12 但只发10），BLE9 无 ID；13 个真实 Button usages、四轴、数字 LT/RT。它不是 Xbox 原生 XInput，不保证 Windows XInput-only、Xbox 主机或各版本目标 App 兼容；无振动 / Share，Guide 可能被系统截获。矩阵三键鬼键风险未消除，移动 + modifier + 开火等组合必须实测。
- 旧 `28f6c86` 与手册第 19 章的 UF2 SHA 保留作键鼠回退；旧 GBA `abe65ea` / `89fbe9d` 仅是本地撤回历史，不搬动旧产物。原始 2026-10-20 文档记录当时未 ARM / UF2、USB / BT 枚举、刷写或 App 实玩；纯核、runtime 新轴合并 / quiet、真实 driver、异步 bridge 及新 ABI 专项已实际通过；完整主机回归已实际通过，含原键鼠 / 灯光 / 矩阵 / 文档同步与新手柄专项；这不是 ARM 或实机验收。
- [使用说明书](<docs/9981Pro_使用说明书.md>)、[实现笔记](<docs/gamepad_implementation_notes.md>)、[README](<README.md>)、[核对清单](<docs/manual_manifest.json>)与四层图稿须同批同步；导出 PDF / Word / HTML 并核对排版。下面的维护命令不是推送、Actions、刷写或清设置授权。

## 更新和导出的最短流程

```bash
# 先改实现（如已授权）及 docs/9981Pro_使用说明书.md，再填写实际版本：
python3 tools/build_manual.py --source-commit <功能源码提交> --refresh-manifest --review-note '本轮功能/键位与说明书同步说明' --output-dir <当天同任务交付目录>
python3 tests/check_manual_sync.py
# 导出 PDF 和键位 PNG：只使用本人新建的打印标签页，结束关闭自己的标签，不操作用户标签。
node tools/print_manual.mjs --output-dir <当天同任务交付目录>
# 再导出带键图的 Word，并核验三个格式：
python3 tools/build_manual.py --output-dir <同一交付目录> --word-only
```

未执行或不可用的验证要如实说明；不能用“同步清单已更新”冒充实机测试。说明书、导出工具与本规则归项目所有，不把历史旧手册或某个全局记忆自动当当前版本。
