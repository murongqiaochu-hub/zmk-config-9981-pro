# 9981 Pro 固件：开发与说明书维护规则

## 当前：完整修复候选（2026-10-25）

- 用户已确认原 v2、USB-only `266c27b`、runtime-only `7eeca50` 实机通过。当前分支 `codex/ipad-full-repair-20261025`，功能源 `85893971ddecfa32bd21a2309ff16e7925940c9b`；恢复二分前原 `1c61b81` 完整功能 / v2.2.1 旧映射，不混 v2.3.3。
- 普通 / SYM / aA 三层和原参数保持；恢复 MEDIA 输入、WASD D-pad、黑莓切杆、机械点击 DOWN 捕获 L3/R3。可完整验收，不再沿用 runtime-only 的“只测普通层、不进 MEDIA”限制。
- 两项修正：runtime 先将最高活动层 INDEX 转 stable ID 再判 MEDIA；BLE 第二独立 0x1812 gamepad HIDS，stock HOG 不改，196B 键鼠 Map 与 141B GP Map 分开。Input9 无 ID；Control Point 每 peer 独立，Suspend 只 gate 该 peer GP，Exit Suspend 先 neutral，无 remote wake。
- 原完整 gamepad suite、真196+141独立服务、重排旧代码失败/新代码通过、12负向、完整host/云回归/ARM/UF2 PASS。设备实际重排未证，不称INDEX bug已确定为设备根因，实机尚待用户测试。
- 构建79b7b7cd88d6a43a9261d4b145665cf355ee1995 / Run40201273432 / artifact12602038347成功；deliver实际skip。605696B/1183块、[0x1000,0x4af00)，SHA cb89bd9060347df06c5c66f7010afcb821513309ed14312c5e37a12ee3fd30b1。原生1163配置与首刀全同，相对通过runtime只加BBP_GAMEPAD与DT兼容两符号；DT同首刀29300B/SHA5f46dc75122cff42d25fdc02f7046a60a34eddb056c4fbdbbd991f118cf4582e。直接读取原v2和新UF2静态GATT表，原6服务UUID/权限/次序/绝对句柄全部相同，HOG28–49/22attrs保持，新独立GP最后54–64/11attrs；两Map分别196/141，USB/蓝牙各一份。不是用service名字猜排序，不是无线验收。最终文档skip-ci不重建。
- 同一 BLE 身份仍共享 cache / 连接，多 HIDS 不保证 iPad 或游戏支持。定位构建按已授窄门执行；绝不自动刷写、清配对 / settings、改主机或稳定发布，不动 v2.3.3。下方二分记录均为历史，不覆盖本节。
- [手册](<docs/9981Pro_使用说明书.md>) v2.2.1.7 恢复完整 27 章；导出工具恢复 gamepad 标签与 27 章版式 / 预览支持。manifest、PDF / DOCX / HTML 及 QA 由父会话生成并核验。

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
7. 仅写说明书不授权改固件功能、自动触发新 ARM 构建、推送、刷写、清配对或主机设置。本轮为说明书任务，轻拍继续暂停调优。

## 已通过运行时单变量的历史记录（2026-10-25）

- 用户确认USB-only 266c27b“这个版本能用”，并20:26要求继续。定位构建无需逐次确认，不自动刷机/清配对/settings/改主机/稳定发布或动v2.3.3。
- 从已通过1014c94文档头（config同266c27b）建立分支codex/ipad-runtime-only-20261025；只改CMake编译列表，将neutral shim替换为原1c61b81 behavior_gamepad.c和第一刀d05c9e1 gamepad_ble_disabled.c。原neutral文件保留但不编译，二原件逐字节不修改。Kconfig/conf/DT/两键图/触板/轻拍/USB及BLE HOG均保持上一通过版。
- 无新DT行为实例；原SYS_INIT独立，线程确会启动。恢复上下文监听/worker/USB中立握手；BLE恒ENOTSUP，无BLE手柄GATT/Map。没有游戏键绑定和指针生产者，不是完整可玩手柄。runtime仍保留进入/退出MEDIA的取消轻拍回调，所以本轮只测普通层、启动后不进MEDIA，不宣称所有MEDIA语义不变。
- 精确HTTPS专属分支推送，继承旧auto-deliver窄门不扩展，软件通过不等于iPad恢复。先交本轮用户测试再决定输入接管组。

- 本轮功能源6c0036736682ee19d956181d246ad1aace615503，构建7eeca50baa3b2d823c77a6af0cd6b91ca02cc54c / Run40193709410 / artifact12599376433成功。完整host/24负向/实际runtime+disabled出口/ARM/UF2 PASS。601088B/1174块，SHA0390002d143dcfc3bc57a3a3c483df693bebbb341f3b1bdc25b9a287be1240d5，仅应用[0x1000,0x4a600)。原生1161配置与通过USB版完全相同，DT同29007B/50892b164b117c966bc8ca86cf732e83b49f975e7c0fcb143c95fad9155ef01b；196B描述符2处/141B仅USB1处，runtime和disabled实际编译，neutral shim未编译，映像含原worker名/policy。待用户测试，未自动刷写。最终文档skip-ci不重复构建。
- 额外只读检查固定ZMK behavior.c/keymap.c：当前原生CONFIG_ZMK_BEHAVIOR_LOCAL_ID_TYPE_SETTINGS_TABLE=y，正常按device name恢复local_id，新增DT实例并非简单重排所有旧绑定ID。孤立/缺失settings等例外无设备证据，不据此猜根因或要求清settings。本轮不加DT实例。

## 已通过USB单变量的历史记录（2026-10-25）

- 用户已重新刷入原始v2.0 `28f6c86`确认“一切正常”；原UF2 SHA256 `e0c20d4dc2d35d5fe6395e4574ad7bb4272d92d3269b6452af95e2d87dcbb94d`、586752B。原样重编Run40190509334/124c073与原文件逐字节相同，1161配置及容器digest相同，不需重复刷控制。
- 用户明确后续定位构建不必逐次确认；允许必要测试源码/专属分支HTTPS推送/Actions/校验交付，不含自动刷机、清配对/settings、主机设置、稳定发布或v2.3.3改动。
- 原失败版、元数据/diag1、第一刀8f56e28、BLE枚举1c60120均失败。第一刀混合USB+runtime；互补版使用neutral shim不是严格补集，不据此断言两独立根因。只读审计未发现never-MEDIA+BLE时第一刀显式清键鼠/切目标/请求安全路径，不排除底层/时序。
- 当前从正常v2基线72c0cee（config同28f6c86）只加USB枚举面：conf两值HID_COUNT2/REPORTS4，原USB TU/6原头及源保持1c61字节，新增中立shim，无主动worker/游戏输入；GET_REPORT10B中立响应。无BLE扩展，无游戏键图/触板接管，无metadata/IPDIAG。HID_0/原BLE Map196保持，新增HID_1 Map141。不是可玩手柄版。
- 分支codex/ipad-usb-enum-only-20261025，仅推https://github.com/murongqiaochu-hub/zmk-config-9981-pro.git；不扩大继承的auto-deliver旧分支窄门。先交本轮测试，再按反馈选下一最小变量。

- USB-only构建成功：功能源b942152802a90ecc4c1b105aa485ed94e5fecbac；构建266c27b0205eec61ddea180176b1b950cae50ed0 / Run40191811435 / artifact12598825950。完整host/云回归/ARM/UF2 PASS，deliver实际skip。UF2 588800B/1150块，SHA256 1b94c830732723fcb4d2531c28342e8b3f0d82e8994652cd0921264259cae636，应用区[0x1000,0x48e00)。1161原生配置仅USB_HID_DEVICE_COUNT 1→2、USB_HID_REPORTS 1→4；DT与原v2逐字节同29007B/SHA50892b164b117c966bc8ca86cf732e83b49f975e7c0fcb143c95fad9155ef01b。映像196B键鼠Map2处/141B仅USB1处，未编译GP worker/BLE扩展，neutral shim已编译。未自动刷机，待用户iPad蓝牙键鼠反馈。若失败且电池可用可由用户另测拔USB；不清配对、不扩日志。文档skip-ci不重复构建。

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
