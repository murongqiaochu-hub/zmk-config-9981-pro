# 最后一轮：蓝牙键鼠优先／安卓手柄候选

日期：2026-11-02。功能源 `a25ec8fd00fcc1ef15ae4c0627e3e602ba89a791`；基线 `11ee89eee92969a15f75a0d5848c38c6a2c2ce15`；分支 `codex/keymouse-android-final-20261102`。仅测试候选，未刷写、未实机验收，不发布或自动归档。

## 目的和最小修改

用户已实测3754c9b在iOS通过BLE能连接，但键盘和鼠标都失效，全部平台手柄不可用。最后尝试全平台蓝牙键鼠优先、安卓蓝牙手柄；iOS/Moonlight手柄不再追，USB通过不能替代蓝牙。

本轮直接从完整11ee89e恢复键鼠基线；其86份config中85份逐字节保留。只更改[独立手柄服务](<../config/boards/arm/bbp9981/custom_driver/gamepad_ble_service.c>)，新增[BLE报告适配](<../config/boards/arm/bbp9981/custom_driver/gamepad_android_report.h>)。不把手柄拼入stock HOG，不全局关闭BLE键鼠，不改键图、原runtime、USB、蓝牙identity/profile/settings或SDK。

| 项目 | 本轮合同 |
|---|---|
| 原键鼠 | 固定196B Map、22属性；保留键盘/Consumer/鼠标与LED、原发送路径 |
| 服务布局 | stock句柄28–49、Studio50–53；独立手柄从54开始，11→16属性；须实际ELF核验 |
| 手柄输入 | 独立Map184B；Report ID4，BLE正文10B无ID前缀；原internal/USB逻辑9B不变 |
| 手柄输出 | ID5、4B，合法写入接受但忽略；没有物理振动 |
| 新尾部 | 保留原服务0..10属性，尾加Output和Protocol；输入/CCC/Ref仍8/9/10 |
| Protocol/CP | Report-only，只接受1；CP每peer suspend；无remote wake |
| 用户操作 | MEDIA入口/出口与全部168绑定不变，退MEDIA恢复普通键鼠路径 |

协议描述符来自固定上游 [portable-gamepad-firmware](https://github.com/jfedor2/portable-gamepad-firmware/tree/aa09d1d05939ab0025107f6df359e0b8a3b714c0)，保留[许可](<gamepad_protocol_LICENSE>)。仅输入ID3→4及显式初始Logical Minimum0（独立Map中冗余但合法）。不借用Google或微软设备身份，PnP仍 `01 50 1d 5e 61 01 00`；不同于Source2纯手柄候选，不把其有限成功反馈外推。

## 验证分层与边界

1. [最终主机入口](<../tests/run_final_android_tests.py>)冻结所有既有test/oracle/SDK fixture，跑原11ee89e完整回归；新增范围闸拒绝不相关配置漂移及12个负例，再编译实际新手柄服务和固定stock HOG。
2. 本机未确认到可用CMake；完整基线host exit0，但本机CMake32+9明确SKIP，CI必须真实执行，不能把跳过写通过。
3. [ARM产物闸](<../tools/verify_final_android.py>)检查实际ELF的服务顺序/属性/权限/回调/报告引用/两份Map、编译输入、PnP、1170项历史Kconfig与完整DTS，并逐段比对ELF应用载荷和UF2。
4. 所有软件结果均不证明空口订阅、OS分类、无线收包或iOS/安卓实机兼容。本轮只允许一次用户候选测试；具体构建运行号与最终hash以同批验证JSON/测试包为准，不能套用历史运行号。

没有足够空口证据确定3754c9b的唯一根因；缓存、系统分类、订阅路径均未复现。保留stock源码和前缀句柄可以机械验证，但**整个数据库改变仍会改变全局GATT hash/Service Changed状态**，不承诺旧缓存不受影响或免重配。新手柄服务对所有BLE主机可见，“安卓”指验收范围，非按系统隐藏。没有改名、重启换人格、双identity或逐片按槽切Map。

## 最短用户测试（只一轮）

1. 按熟悉的方法手动刷本次唯一候选；先保留回退件。不要用settings_reset，不清配对或擦设置；不猜物理复位位置。
2. 不插USB做功能验收，先在iOS及其他日常平台连BLE：普通层打字、移动/点击鼠标、滚轮。任一键鼠失效立即停止，记录平台和现象，回退11ee89e，不再试手柄。
3. 仅安卓：按住SYM、快双击$、松开SYM进入MEDIA；在目标App检测W/D/Z/A方向、O/K/退格/$面键、肩键、滑动摇杆；按SYM或右aA退出，复查普通字母及鼠标恢复。
4. 若安卓手柄仍不能用，结束手柄路线，恢复11ee89e键鼠；不追加第二轮刷测或靠清配对反复试错。

## 指定回退

同批附原11ee89e UF2，619520B、1210块、nRF52840 family `0xada52840`、应用区 `[0x1000,0x4ca00)`。

SHA256：`1e277b08428984a6120c42310c34a6ad40eaaecdf491cbfb1d706e58c3359264`。

该件有用户历史键鼠可用反馈，本轮重新读原件校验格式/hash，不冒称重新实机通过。回退不保证消除主机旧缓存；本次未对设备执行任何刷写、重启、清配对或settings操作。
