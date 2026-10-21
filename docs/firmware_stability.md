# v2.3.1 长期稳定性加固记录

2026-10-21；源码 `7d6ab94e57653deb8e20d142665eee351736f297`，候选分支 `codex/firmware-stability-v2.3.1-20261021`。加固基线 v2.3.0 / `2b2c7dc3cacfc64e5ec4a2b64c306f1caf2a1d6e`；历史全局重构行为基线仍是 v2.2.9 / `f7412ec90c925487617a1e8d40dd886c310142a6`，没有重置或放宽。

## 结论与范围

**修复两处已复现的 USB 轻拍报文隐患；只改一个生产文件，不改产品操作。** 键位、默认值、手势时序、阈值、灯效、存储格式、线程/轮询、GPIO、描述符与手柄实现均保持。80份配置中79份与v2.3.0逐字节相同，唯一例外为 [USB轻拍快照](<../config/boards/arm/bbp9981/custom_driver/trackpad_usb_snapshot.h>)；SDK fixture及原行为oracle保持。新的测试不替代历史全量/差分闸。

这是一版**软件稳定性加固候选**，不是“已经证明长期绝不卡键”。本轮完整host exit0、专项、旧失败/新通过对照、原差分/机械证据与连续模拟通过；本轮ARM/UF2状态以[交付记录](<../firmware-delivery/XBOX_CURRENT.md>)为准。未刷写、未实机连续运行、未发布/归档。

## 1. 缺陷与最小修复

### A. 异步 USB 使用已经退栈的报文

旧实现把函数栈上的鼠标report交给 `hid_int_ep_write()` 后返回。固定HID→USB→nrfx链路保留指针，由之后的IRQ/feeder消费；SDK返回成功不代表已经复制、发送完成或主机收包。旧mock在提交时立即复制，掩盖了此问题。

同一公共API测试在旧头下编译成功后，ASan开启 `detect_stack_use_after_return=1` 明确报告 **stack-use-after-return**（exit91）。新mock只保存源地址，发送函数返回、调用方/全局改写、栈clobber后才模拟完成，新头通过。

修复采用私有uint32对齐静态report。`IDLE → SUBMITTING → INFLIGHT`：先占有再copy，整个SDK调用及其内部重试期间不能退役；调用返回后，仅在线程上下文的短IRQ锁内、保存的合法实际IN端点 `busy=false` 时才能复用RAM。端点读自公开配置的index0，不猜0x81，不因超时或RESET名称强制解锁，不持IRQ锁等待SDK。

**RAM退役不补 `hid_sem` 许可。** 原生键盘/鼠标发送者可能已经持有该许可。只归还本次成功take后因预检拒绝的许可，SDK write-error保留旧处理；take失败无give。报告仍为ID3+原body，刷新所有owner的按钮位，XY/滚轮为0，只发送report长度而非对齐padding。

### B. 等待信号量期间切成 BOOT 协议

旧实现仅在等待前检查REPORT/BOOT；30ms信号量等待期间切到BOOT，返回后仍可能发送带ID3的REPORT。相同公共API `protocol-wait` 在旧头下运行assert失败（不是编译失败），新头在等待结束后短IRQ锁内重新检查协议、epoch和连接状态，正确拒绝且只还本次许可。BOOT编译关闭时不伪造不存在的切换。

这是关闭**等待窗口**，不是使协议/路由变化能原子撤回已经admit或已经进入SDK的硬件工作。

## 2. 实际执行的软件验证

[完整入口](<../tests/run_host_tests.py>)末尾新增[稳定性runner](<../tests/run_stability_tests.py>)；strict `-Wall -Wextra -Werror` + UBSan，不恢复错误；寿命负例/正例另用ASan。

| 对象 | 本轮实际通过的连续序列 / 故障 |
| --- | --- |
| USB快照 | BOOT0/1×13种模式：真实共享许可、延迟源消费、旧ready、SUBMITTING重入/早完成/同SDK重试、reset/abort、保存端点、非法端点、ISR、write-error、status与protocol窗口、stock路径 |
| USB连续运行 | BOOT0/1各50000次DOWN/UP周期；每种104614次write尝试，整个fixture只初始化一次；夹杂source/global/stack覆写、stale epoch、错误、迟到ready与abort；不把显式模拟late-ready当自动恢复 |
| 手柄runtime | 30000次不重置fixture的路由/故障/恢复周期，198000份mock接受报告；FIFO有界、引用归还、恢复新键、signed/u32边界和120day时钟 |
| A320/模式 | 24000次Ctrl/aA锁轮/按住/解锁周期；epoch回绕、超过2^32/2^40事件时间；无sticky/fraction残留 |
| microtap | 20000次native/auto DOWN-UP及模拟日跳变；599/600ms保护、300ms spacing退休、引用/队列；半范围长空闲最多保守保护一窗后恢复 |
| 两盏灯记忆 | 各10000次raw用户更新、2000000次idle/动画poll、104次三写耗尽/重新武装、40次模拟RAM重启；不逐帧写、不串设置键 |
| 回归负例 | 同公共API编译旧/新prefix：旧protocol运行失败、旧ASan报stack-use-after-return；新协议/寿命/原合同均通过 |
| 范围/历史证据 | 79/80配置、冻结SDK/原oracle；原v2.2.9机械证明、68万事件差分与52万余PWM对照继续执行，仅明示快照例外 |

上述“日”“120day”“百万poll”全是**加速模拟时钟/事件数量**，不是设备已经通电这些天、真实flash写入这些次，亦非物理USB/BLE IRQ交错证明。Flash磨损、掉电原子性、主机兼容和耗电不从mock推算。

ASan环境记录：初次PIE二进制的一次独立启动出现重复DEADLYSIGNAL、无有效诊断，未据此改生产源码或记为通过；使用非PIE宿主测试二进制后，寿命/协议/原合同各重复5次通过，自动runner固定 `-no-pie`，且仍要求旧版必须明确报指定UAR而非任意崩溃。不能将宿主启动异常当已证固件缺陷。

## 3. 审计后没有乱改的部分

- 保存器观察/保存由系统workqueue串行执行；无证据的mock重入不算真实并发缺陷。两套保存语义不同，保持原字节。
- TP设置commit在当前有效板级/原生PWM配置没有找到可证瞬态失败路径；“负返回后无自动commit重试”留作条件性防御缺口，不能包装成已发生故障。
- microtap短期历史已有退休机制；丢UP、FIFO溢出、FAULT的fail-closed是安全策略，不以定时强清或自动补DOWN“修复”。
- 未新增线程、看门狗、重连循环、周期性重启或自动清配对/settings；这些没有证据还可能掩盖根因/改变行为。

## 4. 明确保留的限制

1. 原生HID_0忽略sem timeout、可变全局report的异步DMA问题**未全局修复**；本轮仅修扩展的轻拍按钮snapshot。原callback/descriptor和原生发送源码未hook。
2. abort若没有ready，stock semaphore仍可能缺许可；即使RAM已经安全也不冒领许可。新增测试保留此限制，不虚报USB全恢复。
3. `busy=false`只证明固定平台的RAM寿命，不是host ACK；已经进入SDK/无线队列的旧包不能保证撤回，断连/耗尽预算不能保证UP到达。
4. 目标App/XInput-only兼容、矩阵三键鬼键、真实低电量/长期无线连接、flash掉电恢复仍待设备验收；非XInput、无振动、数字扳机等原限制不变。

## 5. 后续与回退

实际检查按[长期实机验收清单](<long_term_acceptance.md>)，优先USB/BLE日常使用、松键后的中立状态、睡眠/唤醒/重连与合理次数的冷启动。遇故障记录上下文，不先清设置或反复刷机。

回退在**新的候选分支**从v2.3.0 `2b2c7dc`构建，或由用户选用[历史交付记录](<../firmware-delivery/XBOX_CURRENT.md>)中的原UF2；不要对工作树hard reset，不覆盖旧件，不自动刷写/发布。行为重构的原始合同仍见[全局架构](<firmware_architecture.md>)。
