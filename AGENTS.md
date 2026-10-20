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
7. 仅写说明书不授权改固件功能、自动触发新 ARM 构建、推送、刷写、清配对或主机设置。2026-10-20 用户已撤回未发布 GBA 候选，另明确选择 **MEDIA / Xbox 布局标准 HID**，不是原生 XInput，不冒充微软 VID/PID；不得继续发布 GBA 或称 Xbox 已交付。轻拍继续暂停调优。2026-10-20 10:44 用户曾精确批准一次 Xbox 分支推送 / Actions 云编译 / UF2 校验交付；该授权不自动延伸到后续版本。2026-10-20 用户当前又明确要求“出固件吧”，针对 v2.2.4 新增 aA 锁轮功能，本轮按最终源码推送目标分支、运行 Actions ARM 构建、校验并交付 UF2；不含刷写、清配对或擦设置。

## 本轮 Xbox 标准 HID 合同（2026-10-20）

- 当前文档版本为 v2.2.4，Xbox布局标准HID（非XInput）。v2.2.4新功能：aA双击后锁轮逆时针旋转90°并固定UPPER；锁定期间任一肩键使触板滚轮半速、屏蔽该肩键自身wheel；按住aA临时覆盖锁定，恢复原方向与原生肩键滚轮。手册原18–23章号不变，第26–27章说明velocity虚拟摇杆的使用、参数与鼠标隔离。
- 仅 MEDIA：E/X/S/F＝D-pad 上/下/左/右，不是左杆；D/K 独立输出 L3/R3；触板机械 DOWN 固定为 Guide/Xbox，空格亦为 Guide；I/J/L/M＝Y/X/B/A；通话/结束＝LB/RB；双肩＝数字 LT/RT（0/255）；T/Y＝View/Menu。黑莓按住换右杆、松开不锁定，按钮不因切杆重映射。19 个 gamepad 位置、3 个回 DEFAULT 出口、20 个 None；N/Enter 留空。v2.2.4 在此映射上新增 aA 锁轮功能，已推送并由 Run 39428313318 成功 ARM 构建、UF2 边界验证；旧 Run 39361865259 不匹配，当前件见[交付记录](<firmware-delivery/XBOX_CURRENT.md>)。未刷写。
- 触板只有相对光学位移：默认左 X/Y、按住黑莓右 Z/Rz，raw 右正下正；velocity 不累计位置。默认 gain8 / 逻辑 deadzone4 / 75%新+25%旧对称舍入，最大输入首帧为 ±95 而非满量程；有效位移续时，120ms 无有效位移本地归零，GPIO/I2C 故障清轴但不代替按钮 UP。不能把本地计时写成 host ACK 或收包时限；停住手指不能保持杆倾斜；触板机械点击只发 Guide/Xbox、不作 L3/R3；D/K 是独立 L3/R3 按键。
- 游戏层新增 driver 独占分支、晚发送 gate 及轻拍临时保护 / 取消旧候选，退出恢复原鼠标；v2.2.4 另新增 A320 锁轮旋转、肩键半速余量和 aA 临时覆盖。原 gesture 参数、开机 OFF、三击开关、bridge I/O / 必要 UP、灯光与 GPIO / 矩阵保留。范围闸验证 46 份旧配置字节相同；精确剥离 GP 门和 v2.2.4 scroll block 后，A320 / microtap / bridge 源恢复基线（A320 仅归一历史 CRLF）；8 个集成文件 + 12 个新增 gamepad/scroll 配置文件。P37 slow、P41 滚轮物理监听仍存在，但游戏层不发鼠标 / 滚轮，P37 不是摇杆加速。
- 标准 HID ID4：body9，USB10（aligned staging12 但只发10），BLE9 无 ID；13 个真实 Button usages、四轴、数字 LT/RT。它不是 Xbox 原生 XInput，不保证 Windows XInput-only、Xbox 主机或各版本目标 App 兼容；无振动 / Share，Guide 可能被系统截获。矩阵三键鬼键风险未消除，移动 + modifier + 开火等组合必须实测。
- 旧 `28f6c86` 与手册第 19 章的 UF2 SHA 保留作键鼠回退；旧 GBA `abe65ea` / `89fbe9d` 仅是本地撤回历史，不搬动旧产物。旧版 ARM / UF2 构建源 `1c61b81`、Run `39361865259` 仅对应旧键图（S=↓，Z/K空），见[历史交付记录](<firmware-delivery/XBOX_CURRENT.md>)，不能用于 v2.2.4；v2.2.3既有测试历史PASS；本轮v2.2.4滚轮/肩键专项及完整run_host_tests.py（strict UBSan、66个配置文件/四层键图与说明书同步）PASS。源码 `9f41197` 已推送；Run `39428313318` ARM 构建及 UF2 验证成功，归档提交 `89311aa`。未 USB / BT 实机枚举、刷写或 App 实玩；host / 云构建都不是实机验收，旧键图的兼容结果不能外推。
- [使用说明书](<docs/9981Pro_使用说明书.md>)、[实现笔记](<docs/gamepad_implementation_notes.md>)、[README](<README.md>)、[核对清单](<docs/manual_manifest.json>)与四层图稿须同批同步；导出 PDF / Word / HTML 并核对排版。下面的维护命令不是推送、Actions、刷写或清设置授权。

- 本次 NAS 工作树的 `origin` 是旧本地工作树，不是 GitHub；本次使用已核实的仓库 HTTPS 地址和单命令凭证 helper 发布，未改全局 Git / 代理。后续须先核对 remote，不把本机路径推断成云端。

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
