# 9981 Pro：v2.3.1 研发封版

2026-10-22。用户明确：不再继续投入固件研发；600KB不是约束。**最终只推荐v2.3.1现有二进制，不再改键、调参、缩减功能或重构。** 本轮只增加冻结/恢复边界测试和说明，80份配置全部与已构建版本原字节相同；不是重新编译出的另一个版本。

## 唯一推荐件与来源

- 固件：v2.3.1；[最终UF2](</vol1/1000/agent/output/1022/104251_9981Pro最终封版_v2.3.1/09_最终固件/9981Pro_v2.3.1_封版.uf2>)，613888B（599.5KiB），1199块。
- SHA-256：`ab9c0c10cb46e5c957ae4f55ecd71dac0817e345353e7f7777776a2abf66f50a`。
- 功能源码：`7d6ab94e57653deb8e20d142665eee351736f297`；实际构建：`d607df8b55ab676aa8910e787fb0a18e9e221389`。
- 已有[Actions Run39614649971](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39614649971>) regression/ARM通过，CI32组合CMake和9负例、新ASan与连续模拟通过；UF2 family `0xada52840`，应用区`[0x1000,0x4bf00)`，不含bootloader/NVS。
- 封版重新逐文件核对全部80份配置、复验UF2完整字节和SHA；**若已刷入同SHA的v2.3.1，不需要因“封版”字样再刷一遍。** 不将文档/test-only变动冒充新的ARM固件。
- 手册v2.3.1.1仅为封版文档修订，对应固件仍是v2.3.1；日期不同不表示功能或固件变化。

## 为什么最后不再改实现

两次独立只读复核及父侧核查覆盖HID_0发送/协议/许可、固定nrfx异步源寿命、gamepad旧peer/队列/引用、microtap长空闲/FAULT/必要UP、两套亮度保存的实际板级调用。**没有发现新增且具备安全最小解、应阻挡封版的问题。** 盲目合并所有原生USB发送者或补许可“自愈”，会引入新的协议、并发和键鼠回归风险，不适合在最后一版中临时进行。

已有v2.3.1两项修复保留：轻拍report改私有持久RAM；等待后重新检查REPORT/BOOT与连接代际。没有为了体积牺牲任何功能，v2.3.1较v2.3.0还多288B FLASH/16B RAM。

## 新增的封版边界证明

[封版runner](<../tests/run_final_review_tests.py>)冻结全部配置到实际ARM构建，原v2.2.9重构BASE与v2.3.0加固基线均不换；[恢复测试](<../tests/test_final_usb_recovery.c>)在BOOT0/1的keyboard、consumer、mouse、可变mouse，以及BOOT键盘共9配置，各500次、不逐周期重置fixture，合计4500次模拟abort/reconnect。

- abort无ready后，snapshot单独重试仍失败关闭，**不捏造late-ready、不直接补共享许可**。
- 后续原生报告在状态/端点允许时真正被mock接纳并完成，其真实模拟完成回调可恢复共享许可，再接受fresh snapshot。
- **这是条件恢复，不是保证自动恢复；许可恢复不代表已经进入FAULT而停用的轻拍会自动开启。** 测试没有证明物理USB/主机时序或发生率。
- 刻意延迟原生mouse DMA再清global XY/wheel，仍能观察到可变报文风险；没有假装把这个旧限制修掉。固定输入监听确实在send后清motion，但实际nrfx IRQ可能在返回前已消费，不能从该反例推断实机必丢位移。

## 保留的边界，完整告知而不继续研发

- 原生HID_0忽略semaphore timeout、可变global报文DMA仍是条件风险；丢最后UP可能等下份同类成功报告纠正。不是栈悬空，更不是整个USB必然永久停发。
- 断链、拒绝发送、重试耗尽不能保证远端收到UP；本地neutral或RAM `busy=false`不等于主机ACK。
- microtap的FAULT是失败关闭，不自动强清；三键矩阵鬼键、各App兼容、低电量/真实多日使用和掉电存储仍无全面实机证明。Xbox只是标准HID布局，非XInput。
- **“研发封版”表示停止功能迭代，不表示已做硬件寿命认证、已刷写或已发布到稳定分支。** 本任务未刷写、清配对/settings、改主机或发布/归档。

## 日常只记三件事

1. 切USB/BLE/蓝牙槽或主机睡眠前，先松键并退出MEDIA；重连后重新按键。异常先松键、正常重连/重开目标App，不乱按维护键或清配对/settings。
2. 轻拍重启默认OFF，机械操作后约600ms保护；若自动轻拍异常，先用机械点击。不要把保护/FAULT误认为所有键鼠都坏了，也不承诺自动解除FAULT。
3. 调好亮度等至少1秒再断电；空闲熄灯不是亮度丢失。锁轮与鼠标翻转不跨重启保存。

**不再要求24/72小时或7天专项长测，不再列继续调参/研发任务。正常使用即可。** [旧长测清单](<long_term_acceptance.md>)只留作可选排错资料，不是本次交付待办。

## 固定源证据

- [固定ZMK原生USB发送器](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/usb_hid.c>)、[输入监听send后清motion](<https://github.com/zmkfirmware/zmk/blob/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/pointing/input_listener.c>)。
- [固定Zephyr nRF USB驱动](<https://github.com/zmkfirmware/zephyr/blob/dacab4875df72109b96cc8977547a0dc04875bcd/drivers/usb/device/usb_dc_nrfx.c>)、[固定nrfx transfer/feeder](<https://github.com/zephyrproject-rtos/hal_nordic/blob/884c4d61746bc35fbd379c169fc87ddb56c6461d/nrfx/drivers/src/nrfx_usbd.c>)。
