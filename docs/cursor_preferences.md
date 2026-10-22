# v2.3.3：五档指针记忆与 MEDIA 键盘慢呼吸

2026-10-22；9981 Pro / P9981 / bbp9981，非 Air。功能源 `V233_FUNCTIONAL_COMMIT`，候选分支 `codex/cursor-presets-v2.3.3-20261022`。仅候选，不发布/自动归档、不刷写、不清配对/settings、不改主机。构建/交付不是设备验收。

## 用户已确认的操作

- 用户已刷 v2.3.2，实际反馈“有提升，但是提升不大”。不把该反馈写成抖动完全解决；此前刷机连接问题由用户确认为坏 C-to-C 线，不是已证实固件退化。
- **aA/UPPER 层 Z/P28** 每次真实 DOWN 前进一档：**0.6 → 0.9 → 1.2 → 1.5 → 1.8 → 0.6**。首次/无有效记录默认 1.2，因此第一次 Z 是 1.5。保持右 aA 再按 Z，松开后试滑；锁定/一次性 UPPER 中同样是该绑定。
- 按住不自动连跳，重复 DOWN 不加档；松开后可再按。普通层 Z 仍发字母、MEDIA Z 仍 D-pad 下。其他 167 个位置绑定未动，M 翻转/Y、F 留空/维护键保持。
- 按住物理左下 aA/Ctrl 为所选档的一半：0.3/0.45/0.6/0.75/0.9；一次性 Ctrl、组合 Ctrl 不触发该物理减速。
- 这是**相对位移软件增益，不是传感器硬件 DPI 寄存器控制**；系统加速度、传输调度与应用仍影响屏幕表现。没有档位/保存成功专属指示。

## 指针数学与采样边界

[纯指针帮助器](<../config/boards/arm/bbp9981/custom_driver/trackpad_pointer.h>)沿用分母 400，五档分子 240/360/480/600/720，Ctrl 精确折半。保留原始 C 向零截断、小数余量、反向清余量、逐轴 idle>50ms 过期；零输入绝不花余量生成尾巴。

[速度服务](<../config/boards/arm/bbp9981/custom_driver/trackpad_cursor_speed.c>)分离 selected 与 motion-frame 两个原子快照；低 3 位档位、高 29 位 generation，每次循环前进一代，即使五次回到原档也不混。只在约 2^29 次变化后代次有限宽度回绕，非五次循环就别名。

[A320](<../config/boards/arm/bbp9981/custom_driver/a320_0x57.c>)是唯一 motion-frame 写者：非重入 poll 开始 capture 一次 selected，同一快照判 epoch，并供整轮实际指针、RAW 轻拍影子及 replay 使用。用户/load 在 I2C 或输出等待期间切档，本轮完整使用旧 capture，下轮先清 pointer/filter 并 interrupt 旧 tap/replay，再用新档；不跨 backend/I2C/input 持服务锁。不是反复 fresh-read gain 或仅检查档位索引。

切档不回放旧小数、防抖余量或未决轻拍候选，不补偿旧位移。当前单消费者边界须保留；将来新增传感器线程/独立微拍调用，须改显式 frame context，不能随意复用全局 latch。

轻拍资格数值仍 3..50 推算像素、1..8 帧、span≤120ms、quiet≥120ms、gap≥300ms；**影子按所选 gain 换算 RAW**，不走防抖/削峰。增益改变会改变对应的 RAW 接受范围，不能称各档 RAW 阈值完全相同。没有接触/抬指识别，也没有借本轮重新调轻拍。

v2.3.2 防抖/旧削峰/BLE积压合并原样保留；滚轮/游戏杆继续走原 RAW 路径，不乘五档。旧小幅抖动未承诺完全消除；[原修正取舍](<pointer_steady.md>)保留。

## 重启记忆与失败语义

- 独立 key：`bbp9981_cursor/gain_v1`，精确两字节 `[0x01,index]`，index 0..4；不复用灯光、蓝牙、Studio 或其他持久化记录。
- `h_set` 只 stage，首个 `h_commit` 一次消费；未知 key、长度、版本、索引、短读/负读被拒绝。首次空 commit 也关闭恢复窗口；重复 load/commit 不回放旧档。用户在 load 前/中改变档位，RAM 用户动作优先。
- 静态 `K_WORK_DELAYABLE_DEFINE`，没有稍后的 SYS_INIT 重置早期 commit/work；默认/读取/恢复不自动写闪存。
- cycle 立即改 selected RAM，最后一次用户动作后 **1000ms 合并保存**。请等至少 1 秒再正常断电；工作队列/闪存阻塞可能延后，未到保存时断电可能仍恢复上一档。
- 每代保存失败最多 3 次，重试间隔 1 秒，随后静默保留 RAM dirty，直到下一次用户 cycle 重置预算。存储硬件失败不保证重启保留；没有成功提示，不用清 settings 排错。
- state/dirty/deadline 在短 spinlock 中操作，settings read/save 均锁外。保存中有新 generation，旧成功不能清新 dirty，旧失败不能花新预算；五次回原档也如此。
- 配置依赖 SETTINGS；单独编译的 SETTINGS=n 服务路径明确仅 RAM，不引用 backend/work。它是 OFF/可移植性测试，不是主候选默认。

## MEDIA 键盘灯，非触板灯

[键盘背光](<../config/boards/arm/bbp9981/custom_driver/keyboard_backlight.c>)仅新增四个 feature 块（+26/-0）：MEDIA 独立相位，20ms 更新、每步 1%，10→100→10%，180 步/标称约 **3.6 秒完整周期**。旧 MEDIA blink 回调在该层早退，不得覆盖慢呼吸；不增加线程或 flash 保存。

- 乘用户亮度上限；亮度 0 或关灯持续熄灭；动画帧不覆盖用户 raw 设定。
- 两通道共用原细分 PWM/缓存与失败恢复路径，约 2kHz 载波；不是低频反复开关模拟最低亮度。
- 切层/idle 继续取消旧 work；恢复/重入 MEDIA 从低亮相位开始。
- DEFAULT 按键授权常亮、SYM 稳态500ms高低亮、UPPER 原5%/20ms呼吸完全保留；触板 USB/BLE 动画和两个亮度保存器不动。
- 3.6 秒为无额外队列阻塞的标称调度周期，不是示波器测量/硬实时保证；真实观感、寄存器与续航未验。

## 范围与证据链

基线是 v2.3.2 文档源 `11218ad83292f5ccf9105e41010c1062073e474d`。当前 85 个 config 文件：73/81 个既有文件原字节未动、8 个既有文件仅显式追加、4 个新速度/behavior 文件。

[新范围闸](<../tests/user_preferences_scope.py>)精确剥离两 feature，还原全部 81 份 v2.3.2，再进入[原 v2.3.2 范围闸](<../tests/pointer_steady_scope.py>)还原 80 份 v2.3.1；所有更早 BASE/C oracle/SDK fixture 保留。历史功能证明只看经过新 gate 验证的 OFF 视图；新的 actual ON 由[专项 runner](<../tests/run_user_preferences_tests.py>)真实编译生产源码验证，不用换旧 oracle 令其接受新行为。

两个键图都 gate behavior 节点与整行 Z 分支，OFF 保留原 `&none`；不借别名少算一个键。说明书/矩阵解析显式选择唯一宏分支，并以真正 C preprocessor 对照 ON/OFF。

### 本轮验证状态

| 项目 | 结果 |
| --- | --- |
| 精确生产范围/四层绑定/CPP ON-OFF | 父侧已执行 PASS |
| service/behavior 四组合 strict UBSan | 父侧已执行 PASS（含 frame latch） |
| MEDIA ON 与三种旧 OFF oracle strict UBSan | 父侧已执行 PASS |
| 历史重构机械证明/差分 | 父侧已执行 PASS；本地无 CMake 明确 SKIP |
| 五档全 XY/actual A320/ASan/完整 host | 本轮执行中，完成后填实际结果 |
| 独立只读审查 | 已核 pinned SDK/settings/workqueue/release；发现并修正帧内 fresh-gain 窗口，待新集成测试 |
| Actions ARM/UF2/DTS/Kconfig/资源 | 待本轮构建，不拿旧结果替代 |
| 同批 PDF/Word/HTML/四图 QA | 待本轮导出；Word 不冒称独立 Office 渲染 |
| 设备刷写/断电保存/视觉/手感 | 未执行，device_tested=false |

## 精确回退

[已刷 v2.3.2 原 UF2](</vol1/1000/agent/output/1022/124037_9981Pro蓝牙指针稳态修正_v2.3.2/09_云端构建产物/bbp9981-zmk.uf2>)，SHA-256 `dbfc480a7da14ec8932b92083a60aea13986f98847320ba854577b838eda940c`；构建 `3d7078e65bbc1e0c30cb4a8da272c1f578aaf343`、Run39725910232、615424B/1202 blocks。回退恢复固定1.2/0.6、UPPER Z无输出、MEDIA快闪，不擦新的独立速度记录。没有自动刷写或清设置。

[使用说明书](<9981Pro_使用说明书.md>)与[交付记录](<../firmware-delivery/XBOX_CURRENT.md>)是用户入口；历史 `device_tested=false` 不因后续有限反馈被追改成全面硬件验收。
