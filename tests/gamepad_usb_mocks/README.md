# USB 手柄专项 host harness（独立 HID_1）

## 实现与集成约定

- 生产实现：[gamepad_usb_extension.h](<config/boards/arm/bbp9981/custom_driver/gamepad_usb_extension.h>)。主代理提供唯一专用 TU，仅 `#include "gamepad_usb_extension.h"`，**不要**对 SDK `usb_hid.c` 注入该头。
- 唯一新入口为 `SYS_INIT(bbp_gamepad_usb_init, POST_KERNEL, 80)`；该初始化幂等，仅绑定、注册、初始化一次 `HID_1`，不会调用 `usb_enable()`。公开的 HID_1 接口描述符在 USB descriptor fixup/enable 前设置为 non-Boot subclass/protocol `0/0`，不改 HID_0。
- 保持三参数接口 `int bbp_gamepad_usb_send(const struct bbp_gamepad_report_body *, uint32_t expected_usb_epoch, uint32_t expected_context_epoch)`。
- 需要 `CONFIG_USB_HID_DEVICE_COUNT=2`、MPS≥10、禁止 USB 自动 boot enable。启用 SOF 时需 `CONFIG_USB_HID_REPORTS>=4`。编译门还要求 nRF52840 QIAA + USB_NRFX，以及默认设备初始化优先级 `<80<` USB descriptor fixup 使用的 `CONFIG_APPLICATION_INIT_PRIORITY`。

## 测试命令

从源码根目录运行，只编译 host C，不进行 ARM 构建、刷写、清配对或联网发布：

```sh
set -e
for b in 0 1; do
  cc -std=c11 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable \
    -fsanitize=undefined -fno-sanitize-recover=all \
    -DCONFIG_ZMK_USB_BOOT="$b" \
    -Itests/gamepad_usb_mocks -Itests/hid_snapshot_mocks -Itests/sdk_snapshot_mocks \
    tests/test_gamepad_usb.c -o "/tmp/bbp_gamepad_usb_$b"
  for mode in normal abort submit-reset submit-reset-abort error-ready stale-ready \
              endpoints missing bad-cfg init-fail; do
    "/tmp/bbp_gamepad_usb_$b" "$mode"
  done
done
```

可额外使用编译器的 AddressSanitizer 验证延迟指针访问；测试底层不会在提交时复制 source，而是在模拟完成时才消费指针。

**本批次验证状态（2026-10-20，Xbox 布局标准 HID，非 XInput）：** 新 ABI 为 `9-byte / 72-bit body`、`10-byte / 80-bit USB report`、`12-byte aligned staging`、独立 `10-byte EP0 snapshot`。父代理已实际编译并执行 USB `BOOT=0/1 × 10 modes` 共20组合及全部5个HOG配置，`-Wall -Wextra -Werror`、UBSan、不恢复 sanitizer 错误条件下全部 **PASS**；父代理已收集任务 `bash-7ff9ae3f124f47f6`，退出码0。本 transport 子会话无 shell/exec，以上执行证据由父代理提供，不是本子会话独立运行。

上述早期结果仅为 transport harness；后续集成现已完成，实际 runtime 合并/quiet、真实 A320/micro 与异步 bridge，以及[完整主机回归](<../run_host_tests.py>)均已由主任务实际执行通过（功能源 `58b5f3b`，日志 exit0）。本次已另行完成 ARM 云编译与 UF2 校验归档，见[当前交付记录](<../../firmware-delivery/XBOX_CURRENT.md>)；仍未真实枚举或设备验收，不将 mock 结果当作硬件证明。

先前撤回的 GBA `7-byte body / 8-byte report` 在父代理执行中曾通过 20 个 USB 组合和 5 个 BLE 组合；**那是旧 ABI 的历史结果，不是本轮 Xbox ABI 的验证结果**。父代理维护的 [run_gamepad_tests.py](<../run_gamepad_tests.py>) 还会校验四个 pinned fixture 的 SHA；本批次未改这些 fixture。不能把 host 模型结果写成 ARM 构建、真实枚举或实机通过。

## 保真与范围

- 直接编译既有完整固定 SDK `usb.c`、256 行 `usb_hid.c` 和生产 epoch/snapshot 头；不改旧 fixture。
- `-I` 顺序选中既有完整官方 ZMK `hid.h`，其完整键盘/consumer/mouse descriptor 与结构体保持原样。新目录没有另造 `zmk/hid.h`。
- 新 USB mock 使用固定 Zephyr 的完整 `hid_ops` 字段顺序，包含 `on_idle`、可选 `int_out_ready`，以及真实 `uint32_t` write 长度/输出参数原型。公开 `usb_cfg_data`、接口描述符与 endpoint 项布局保留真实字段；endpoint ABI 复用固定官方 fixture。
- nRF busy mock 保留固定 HAL 的完整 endpoint 枚举及 `nrfx_usbd_ep_is_busy(nrfx_usbd_ep_t)` 原型，明确要求线程上下文且 IRQ lock 已持有。它只模拟 busy 契约，**没有**模拟 nRF 寄存器、feeder、真实 IRQ 或 EasyDMA。
- 验证 HID_0 原 descriptor/table 精确指针与 IDs1..3 不变；HID_1 单独有唯一 ID4、72-bit body、80-bit / 10-byte USB report。明确检查 body 字段 offsets0..8、report body offset1、12-byte 对齐 storage 和独立 10-byte EP0 snapshot；mock write 长度必须恰好10，延迟完成目的缓冲的第10/11字节保持哨兵，禁止多送 union padding。原 LED/resolution GET/SET、键鼠发送与独立 callback/sem 保留。
- 覆盖两条独立 endpoint DMA、stock semaphore timeout 后伪空闲不影响手柄、含四个活跃摇杆轴和两个非零扳机的 caller stack body 覆写/退栈/stack clobber 后完整延迟消费；GET_REPORT 改写 EP0 时不影响在途 IN，新的 IN 也不改写 EP0 snapshot。
- MEDIA 退出/context 与连接 epoch 等待失效、neutral 的限定豁免；仅 LT 或 RT 非零（其他按钮、四轴均为0、hat8）仍必须拒绝旧 context，不能获得 neutral 豁免。fresh context 保留完整末尾两字节；物理扳机0/255，中间值仅测试 raw wire。BOOT 开/关及两接口隔离、SUSPEND 拒发、write 拒绝、早于 write return 的 callback 均保留。
- 对 reset/abort/迟到 callback，覆盖 `busy=true` 旧 ready、新 transfer 的旧 ready、`SUBMITTING` 时 `busy=false` 的旧 ready/重入发送、driver 调用中 reset（仍 busy 与已 abort 两种）、恢复后 fresh-epoch neutral、非 `0x81` 的实际 endpoint（`0x86/0x87`）、无效/缺失 endpoint 与 ISR fail-closed。

## RAM 生命周期证明与不能声称的事项

生产 gate 只有 `IDLE/SUBMITTING/INFLIGHT`。在同一个短 IRQ lock 内，先进入 SUBMITTING，再将10字节 report 复制到私有12字节 uint32 对齐 staging；实际只送 report 子对象的10字节，末尾2字节不是 wire ABI。整个 `hid_int_ep_write()`（包括 SDK 内部重试）返回前不允许 callback 解锁。退出 SDK 后和之后的 callback/retry，仅当已保存且合法的实际 IN endpoint `busy=false`，才退役该 RAM ownership。reset 或 abort 不会凭状态名直接释放 source；SUSPEND 不发报告、不强行解锁。

这是针对固定 nRF52840/Zephyr/nrfx 的**线程上下文 RAM 可复用证明**，不是 host ACK、status-drain 证明或已进入 SDK 的发送可撤销证明。旧 callback 遇到新 endpoint transfer `busy=true` 必须被忽略；`busy=false` 允许回收 RAM，即使旧完成事件尚未排空。

本适配器不修复既有 HID_0 的 ignored-semaphore-timeout、mutable report DMA 或旧 snapshot stack source 问题；它保持原键鼠路径不变，并通过独立 HID_1 避免将新增手柄的 gate/恢复策略施加给键鼠。不得在手册中把新手柄的 RAM lifetime 保证写成全键鼠路径都已修复。

## 固定公开源码证据

- [ZMK v0.3.0 west manifest](<https://raw.githubusercontent.com/zmkfirmware/zmk/v0.3.0/app/west.yml>)：Zephyr `v3.5.0+zmk-fixes`。
- [固定 Zephyr HID API](<https://raw.githubusercontent.com/zmkfirmware/zephyr/v3.5.0%2Bzmk-fixes/include/zephyr/usb/class/usb_hid.h>)：完整 ops、register/write/init API。
- [公开 USB 配置结构及修改时机合同](<https://raw.githubusercontent.com/zmkfirmware/zephyr/v3.5.0%2Bzmk-fixes/include/zephyr/usb/usb_device.h>)。
- [固定 HID class](<https://raw.githubusercontent.com/zmkfirmware/zephyr/v3.5.0%2Bzmk-fixes/subsys/usb/device/class/hid/core.c>)：实例化多个 HID、IN endpoint index0、全局 BOOT subclass。
- [固定 HAL USBD 头](<https://raw.githubusercontent.com/zephyrproject-rtos/hal_nordic/884c4d/nrfx/drivers/include/nrfx_usbd.h>)：枚举与 busy API。
- [固定 Zephyr nrfx 构建集成](<https://raw.githubusercontent.com/zmkfirmware/zephyr/v3.5.0%2Bzmk-fixes/modules/hal_nordic/nrfx/CMakeLists.txt>)：头目录全局导出、USBD 实现按配置链接。

主机模型不替代最终 ARM 编译、组合设备枚举、nRF abort/reset 与主机设备识别验收。这些未执行时必须保持“未验证”。
