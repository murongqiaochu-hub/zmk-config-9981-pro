# 9981 Pro v2.3.0：行为保持型全局重构

2026-10-21。行为基线 **v2.2.9 / `f7412ec90c925487617a1e8d40dd886c310142a6`**；重构源码 **`5c82f7ac3a4fa0acd5d4e2ae504a0fc5f5659fde`**；候选分支 `codex/firmware-refactor-v2.3.0-20261021`。

## 1. 目的和边界

将渐进增加功能造成的职责混杂和重复路径拆清，**不是重新设计功能，不顺手修正历史行为**。不改默认/Studio键图、阈值、滤波、按钮和轴、协议描述符、灯效、持久化格式、工作队列优先级/间隔、GPIO/矩阵或维护键。新增结构不意味着增加新的线程、轮询或SDK全局钩子。

基线源已在当日交付目录保留完整源码归档与重构前完整host日志。软件验证能证明被检查的文本、数学和模拟事件轨迹一致；不能证明所有真实RTOS交错、蓝牙/USB主机、DMA时序或实机组合完全等价。**本版不是字节相同二进制；内部符号、地址、源码行与日志函数名前缀可变化，日志正文/级别/事件顺序必须一致。**

## 2. 全局分层和状态归属

| 组件 | 入口 / 责任 | 不应承载 |
| --- | --- | --- |
| 输入模式服务 | [trackpad_mode.c](<../config/boards/arm/bbp9981/custom_driver/trackpad_mode.c>) 监听原物理位置事件，发布aA/锁轮、Ctrl速度、鼠标翻转与肩键原子快照；保留原监听名与事件顺序 | 传感器读写、指针/滚轮积累、灯效渲染 |
| A320硬件适配 | [a320_0x57.c](<../config/boards/arm/bbp9981/custom_driver/a320_0x57.c>) 管GPIO/I²C、原始采样、故障与输出路由，仍在原workqueue按原间隔执行 | 按键手势仲裁、亮度或手柄发送 |
| 运动纯核 | [trackpad_pointer.h](<../config/boards/arm/bbp9981/custom_driver/trackpad_pointer.h>) 保留指针小数算法；新增 [trackpad_scroll.h](<../config/boards/arm/bbp9981/custom_driver/trackpad_scroll.h>) 持有滚轮累积/时间/半速余数和原增益树 | GPIO、取时钟、HID发送、调度 |
| 轻拍与按键所有权 | [trackpad_microtap_runtime.c](<../config/boards/arm/bbp9981/custom_driver/trackpad_microtap_runtime.c>)、[trackpad_tap_output.c](<../config/boards/arm/bbp9981/custom_driver/trackpad_tap_output.c>) 与aA/肩键behavior保留既有上下文和引用协议 | 因“代码重复”而合并不同生命周期或放宽保护 |
| 灯光渲染/记忆 | 两灯驱动仍各管自己的动画和保存；[indicator_pwm.h](<../config/boards/arm/bbp9981/custom_driver/indicator_pwm.h>) 只共享分数PWM计算、cache和两遍写事务 | 共享动画时钟、混用两套设置键或保存策略 |
| 手柄输入适配 | [behavior_gamepad.c](<../config/boards/arm/bbp9981/custom_driver/behavior_gamepad.c>) 只映射ZMK参数与opaque/error返回；26行，原555行 | worker、引用、目标状态或传输等待 |
| 手柄运行时 | [gamepad_runtime.c](<../config/boards/arm/bbp9981/custom_driver/gamepad_runtime.c>) 独立拥有输入状态、32帧FIFO、目标引用、epoch、worker与原监听/init | 反向依赖ZMK binding/event类型 |
| USB/BLE平台适配 | [gamepad_usb.c](<../config/boards/arm/bbp9981/custom_driver/gamepad_usb.c>) 真正承载独立HID_1实现；BLE和原USB生命周期前缀继续按原序注入各自SDK TU | 把独立USB实现伪装成SDK前置头，或跨TU搬散DMA证明 |
| 板级组装 | [CMakeLists.txt](<../config/boards/arm/bbp9981/CMakeLists.txt>) 从164行缩为25行，委托 [cmake模块](<../config/boards/arm/bbp9981/cmake/gamepad.cmake>) 管各自集成与SDK拒绝门 | 全局force-include、自动改SDK或把失败改警告 |

A320主文件653→465行，模式服务133行、滚轮纯核86行。重构是职责和依赖的拆分，不以总行数少或所有文件都改过为目标。原有已分离良好的纯FSM、指针/摇杆算法、SDK兼容层与危险维护入口保持原字节。

## 3. 不能“优化掉”的行为差异

### 输入 / 滚轮

- 模式快照在与原来相同的位置分别读取；不替换成新跨字段事务，不改变按键监听与poll的顺序。aA+SYM的两种按下/释放顺序仍由原owner协议处理。
- 锁轮双击每拍 `<450ms`、释放间隔 `≤350ms`；P3/P5方向档三击和aA+M双击为按住 `≤180ms`、释放间隔 `≤220ms`。固定档幂等、不累加，锁轮期间显式按住aA优先蓝牙槽1/2。
- 原Ctrl滚轮整数 `/2` 在进入纯核前执行。40ms积累窗、成功零帧清样本、严格 `>50ms` 新滑动、按纵轴选择原增益分支、锁轮方向和带符号半速余数均保留。
- 故障、模式/速度/方向边沿清理保持原先的不同粒度；GPIO idle、GPIO错误、I²C错误不是同一事件。游戏进入/退出的pointer epoch仍能使两次poll间的往返失效。

### 灯光 / 保存

- KB分母10000，TP分母30000；64位乘积，最后才向下舍入。TP仍最终1/3，不提前把raw亮度或pattern除3，不强制非零tick。
- 先算全通道，再按原序 `ready→write→即时error→下一通道`；失败不早退、不部分提交cache，下轮全通道重试。cache hit不检查硬件，也不重算诊断级别。
- [键盘记忆](<../config/boards/arm/bbp9981/custom_driver/keyboard_light_memory.h>)与[触板记忆](<../config/boards/arm/bbp9981/custom_driver/trackpad_light_memory.h>) **原字节不变**。KB成功保存无条件清dirty；TP成功保存还检测期间变化并可续存。TP有driver-ready/幂等commit闸、保留native OFF；KB轮询不等待该闸。统一成一个“更正确”的保存器会改变功能，因此本次不做。
- 两个设置键和1字节raw格式不改；250ms合并、失败1000ms、原3次自动重试策略保持。USB呼吸优先级、首次dark、真实elapsed、迟到handler、键盘idle授权和动画先输出/先推进的不同也保持。

### 手柄 / SDK

- 基本类型API保留 `uint32_t position/action` 与 `int64_t timestamp`，避免非法输入被窄化。DOWN检查action，UP释放DOWN捕获的owner，不重映射。
- 键事件年龄 `>250ms` 拒绝；轴样本 `≥120ms` 拒绝。无效样本只清轴，不能代替按钮UP。`reschedule`与` schedule`分别保留立即wake和不覆盖wake的defer语义。
- 旧captured peer先neutral，再接纳新上下文；连接引用仍由worker转移/释放。USB bus epoch、BLE旧peer neutral的闸不同，不合并。
- 原HID_0、ID4/body9/USB10/BLE9、13真实按钮、hat8和轴/扳机完全不变。SUBMITTING/INFLIGHT RAM生命周期、安全升级/重试和必要UP不改。
- SDK注入仍为hog的mouse snapshot→gamepad，usb的epoch→gamepad lifecycle；每个只作用于指定SDK TU。精确Zephyr/HAL版本、脏文件和4份SHA拒绝门逐项保留。

## 4. 验证设计与真实状态

已在本地主机执行：

1. v2.2.9重构前完整host exit0，源码归档留存。
2. [重构范围与差分入口](<../tests/run_refactor_tests.py>)：62份基线配置逐字节不变，仅8份已有文件与10份新增文件在明示范围；四层键图、DT、Kconfig、持久化头、轻拍核、传输前缀均未改。
3. [完整传感器机械证明](<../tests/test_refactor_sensor_structure.py>)：从固定Git基线重建全驱动token，校验移出的状态/监听、完整reset、半速算术和增益树。
4. [手柄机械证明](<../tests/test_gamepad_refactor_structure.py>)：27个runtime函数及12个USB函数原文字节相同，整文件仅按允许变换分解；原测试断言保留。新增基本类型API边界和非零opaque测试通过。
5. 旧/新独立二进制执行同固定seed：传感器240000步、灯光settings0/1×KB/TP各80000步、手柄USB/BLE共120000步，合计 **680000事件**；按1000步累积摘要逐检查点相同。观测输出、取向/epoch、PWM写与故障、调度、保存、目标/引用；灯光日志级别/正文顺序也纳入摘要，排除内部源码行/函数名。
6. 共享PWM对旧KB/TP独立公式各262144组，共524288次输出比较，以及ready/write/error顺序、部分写失败、全cache提交和重试专项通过；原8个灯光strict UBSan目标通过。
7. [独立TU链接测试](<../tests/test_refactor_link.py>)：USB-only/BLE-only/双传输×tap OFF/ON，真实adapter与runtime分别编译再链接，单一init/listener和订阅/API边界通过；不是实际USB/BLE栈链接。
8. 原[历史范围闸](<../tests/check_tap_successor_scope.py>)未删除、未放宽；在隔离的固定v2.2.9参考树中照常执行，再叠加当前精确范围和差分闸。不能把历史只允许增量功能的源码字节断言直接套在重构后或简单删除。

**当前完整主机回归exit0，80配置/四层42键图/manifest同步PASS；[Actions Run 39569977363](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39569977363>) 的regression、bbp9981 ARM和artifact合并全部成功，deliver按候选条件跳过。** 构建提交 `0526e2fea016fe48ab2a1f8cecd1e1db543397f3`；实际日志确认新增mode/runtime与独立USB对象编译。FLASH306480B、RAM88862B，RAM与基线相同；不以此宣称实际性能提升。

[CMake合同](<../tests/test_refactor_cmake.py>)已在CI实际通过GP/tap/USB/BLE/micro **32组合**及5个版本/dirty、4个SHA负例。本地缺CMake仍明确SKIP，不能把本地token对照冒作CMake执行；云端日志已补齐实证。独立审阅未发现实现回归；发现并补齐了灯光日志oracle缺口，差分现记录日志正文/级别/顺序，新增“将ERROR错改DEBUG必须破坏轨迹”的负例通过。

最终云端 **整个设备树正文逐字节相同**（仅剥离日志时间前缀），四层各42绑定和全部behavior/硬件属性未变；**1166项赋值Kconfig完全相同**。对照基线构建Run 39563315080，详见[云端一致性JSON](</vol1/1000/agent/output/1021/155910_9981Pro全局重构_v2.3.0/14_云端基线一致性核验.json>)。

UF2本地边界/身份校验PASS：613376B、1198块、family `0xada52840`、`[0x1000,0x4be00)`，SHA `81bbd3bb45bac84733ccecc84d35cad423463efe1d6cd968212d919f8d992a45`。27页A5 PDF与带四图DOCX结构/文本检查通过；Word未作独立Office视觉渲染验收。**未刷写、未USB/BT/App实机验收，未发布/归档。** 详细产物与历史件见[交付记录](<../firmware-delivery/XBOX_CURRENT.md>)。

## 5. 后续维护和回退

- 修改一处职责时，不向硬件adapter继续堆状态机；新功能先确定owner、上下文失效、异常出口与测试边界。
- 不因代码重复而合并含不同等待/生命周期/保存语义的路径。SDK前缀和DMA门保持明确、局部、失败关闭。
- 运行 `python3 -B tests/run_host_tests.py`；包含旧回归、机械证据、独立TU链接和差分轨迹。更换行为基线必须是明确产品决策，不能为过测试随意更新BASE。
- 所有结构/config变化也必须同批更新说明书/manifest；图和四层键图本次保持一致，不宣称新增键位。
- 源码回退：在新的回退候选分支从 `f7412ec90c925487617a1e8d40dd886c310142a6` 构建；不要对用户工作树执行hard reset。历史v2.2.9 UF2与SHA见交付记录，不覆盖归档，不自动刷写，也不清蓝牙/设置。
