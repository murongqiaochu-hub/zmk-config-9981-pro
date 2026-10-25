# MEDIA / Xbox 布局标准 HID：完整修复候选说明

2026-10-25，与[手册 v2.2.1.7](<9981Pro_使用说明书.md>)同步。功能源 `85893971ddecfa32bd21a2309ff16e7925940c9b`，分支 `codex/ipad-full-repair-20261025`。恢复原 `1c61b81` 完整功能和 v2.2.1 旧映射，不混 v2.3.3；标准 HID 非 XInput，不冒充微软 VID/PID。

## 当前修正与证据边界

- runtime 将最高活动层 INDEX 转 stable ID 后再判断 MEDIA，避免层重排时将索引直接当稳定标识。重排反事实旧代码失败 / 新代码通过；**设备实际重排尚无证据，不能称已确定 iPad 故障根因**。
- BLE 改为第二独立 0x1812 HIDS，stock HOG 源码 / Map 不改；stock 196B 与 gamepad 141B Report Map 分开，不再在 stock Map 尾部追加手柄 TLC。Input body9 无 ID。
- gamepad Control Point 每 peer 独立；Suspend 只 gate 该 peer GP，不暂停 stock 键鼠；Exit Suspend 先 neutral，无 remote wake。
- 独立服务仍共用蓝牙身份、连接与主机 cache，多 HIDS 不保证 iPad / 游戏支持。不将“源码未改 stock HOG”写成“新 ARM 原生布局已验收”。
- 原 v2、USB-only、runtime-only 用户实机均通过。本轮原完整 gamepad suite、独立真 196+141 服务、重排反事实及 12 负向专项 PASS；**完整 host、ARM / UF2、真实枚举、App 实玩仍待验**。本说明未自行执行测试，状态来自父实现交接。

## 保留的完整功能与 ABI

普通 / SYM / UPPER 各 42 键及原参数保留。MEDIA 17 个 gamepad 绑定为 `P0=10, P1=11, P2=8, P3=17, P4=18, P6=9, P8=0, P11=12, P12=13, P14=7, P17=2, P18=1, P19=3, P23=6, P25=5, P34=4, P39=14`；P35 / P40 / P41 三出口回 DEFAULT，其余 22 个 None。WASD 为 D-pad，I/J/L/M 为 Y/X/B/A；黑莓按住右杆，机械触板 DOWN 捕获 L3/R3，UP 只释放原 owner。

报告字段固定为 `buttons_lo, buttons_hi, hat, x, y, z, rz, lt, rt`。USB 独立 non-Boot HID_1，ID4＋body9＝10 bytes；BLE body9 不带 ID。16-bit buttons 仅 13 个真实 usages：1、2、4、5、7、8、9、10、11、12、13、14、15；3/6/16 为 Constant。hat 0..7、Null=8，相反方向相消、斜向合并。四轴 signed [-127,127]；LT/RT 只 0/255，并非半按。

触板 velocity 只用当前相对 raw：gain8、deadzone4、每分量限幅127、75% 新＋25% 旧对称舍入；反向先清旧分量。最大连续同向输入由归中依次95/119/125/127，首帧不满杆。有效位移刷新计时；零 / 死区不刷新；120ms quiet 或 GPIO/I2C 故障清轴，不代替按钮 UP。单触板不能同时独立控制双杆，停住手指不能保持实体杆偏转。

## 生命周期与不能省略的限制

USB 私有 staging 为12 bytes、实际 TX10 bytes，EP0 snapshot 独立。固定 SDK 下 busy 门保护缓冲生命周期，不等于 host ACK 或全面修复旧键鼠 DMA。已 admitted / SDK 包不能撤回，不承诺绝不卡键。

runtime 保留32帧 FIFO、每位置 owner、250ms 帧寿命；旧目标最多40次 / 400ms释放尝试，新目标先 neutral，不重播旧按住状态。旧目标拒绝期间新主机首键可能过期，须松开重按；断线或重试耗尽不保证旧主机收到 UP。软件定时不等于真实 RTOS / 无线到达保证。

MEDIA raw 独占＋晚发送门阻断鼠标 / 滚轮 / 轻拍，退出恢复原路径。P37 slow、P41 滚轮及 P40+P41 Ctrl 的原监听不由改键自动删除；P37 不是摇杆加速。轻拍仅临时阻断 / 取消旧候选，不改开机 OFF、三击开关值与原参数；同 poll 进退不复活旧候选，必要 UP 保留。

矩阵三键鬼键风险仍在；移动＋modifier＋开火、方向＋面键＋肩须实测。Guide 可能被系统 / 客户端截获；无振动、Share 或真实模拟半按；XInput-only / Xbox 主机及各 App 不保证支持。Studio 持久覆盖、缓存和具体平台应分别核验，不能靠清配对 / settings 排错。

## 原参考与维护

[原完整实现笔记（历史）](</vol1/1000/projects/output/1025/190300_iPad二分第一刀/01_固件源码/docs/gamepad_implementation_notes.md>)保留原 USB、纯核、bridge 与固定 SDK 细节；其“向 stock HOG 追加”、CP 限制、旧构建授权和旧完整回归状态不适用于本轮，以本页和手册第23–25章为准。旧 GBA `abe65ea` / `89fbe9d` 已撤回；原 v2 回退 SHA 见手册第19章。

恢复 MEDIA 后可完整验收，不再限制只测普通层。父会话负责 manifest、四层图与 PDF / DOCX / HTML 导出 QA、完整 host 与构建结果更新。绝不自动刷写、清配对 / settings、擦闪存或修改主机设置。
