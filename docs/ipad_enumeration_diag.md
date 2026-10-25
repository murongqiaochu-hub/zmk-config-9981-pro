# v2.2.1 iPad枚举诊断：使用与证据边界

2026-10-25；基于元数据候选17192e6，仅增加观测；不是已修复版。用户已授权本轮专属分支构建、交付并自行刷测，不授权工具自动刷写、清配对/槽/settings或改主机配置。原v2.3.3不动。

## 本轮要解决的观测缺口

旧Mac采集已证实实际BLE路由、部分输入进入固件、三组鼠标notify返回0。原生构建启用强制订阅检查，因此当时固件鼠标CCC检查通过，但可能来自旧配对恢复。缺少键盘CCC、安全级别、Map读取和真实发送handle；继续重复旧MDIAG采集不足以定位。

本轮保持USB/BLE键鼠和手柄、报告内容与描述表、HIDS属性顺序、DIS软件版本、身份与安全策略不变。只增加IPDIAG，不另建HIDS或自定义GATT标记服务。诊断采样和缓冲有界，不能承诺对时序绝对零影响。

## 用户刷测顺序

1. 只刷本轮最终校验交付的普通应用UF2，不刷settings_reset，不清配对，不使用任何旧包冒充新构建。用户自行决定并执行刷写。
2. 刷完可以先测原iPad键鼠，告知“正常／仍无输入／其他现象”。**诊断版未针对功能修复，因此仍失败并不意外。**
3. 如仍失败，让Mac agent读取本页和新采集脚本；旧采集器只保留MDIAG，会丢掉本轮IPDIAG，不能原样复用。
4. Mac agent先只读重新识别新映像debug CDC，再核对cu/tty占用。本轮最终b929f2f映像已离线核验debug=control3/data4、Studio=control0/data1；Mac仍须按当前IORegistry将data4对应到实际端口，不能仅套用串口后缀。不要打开Studio RPC。
5. 每轮临时termios/DTR操作仍须单独取得用户确认：保存termios、DTR低、115200/8N1/raw、DTR高；结束尝试恢复termios、DTR低、关闭。不是恢复原DTR状态；不发送串口协议数据，不用1200、不TIOCEXCL。
6. 取得确认后先开始120秒监听，看到持续推进的IPDIAG状态后再让用户右aA+Q，确认active连接selected=BLE；在空白页面少量固定键、滑动、机械点击。
7. 需要观察连接过程时，Mac agent应另外说明并让用户确认手动断开/重连这一个已配对设备；不忘记设备、不清槽。先启动监听，后重连。重连也不保证iPad重读缓存的Map，缺读取不能直接当故障。
8. 日志与用户观察一起交回。通知ret0、CCC=1均不等于iPad解析/显示成功。无字段则标未知，不因未知盲改元数据。

## 安全采集脚本

将[IPDIAG采集器](<../tools/capture_ipdiag.py>)与它复用的[串口会话库](<../tools/capture_menu_diag.py>)放在同一目录。脚本是Python标准库，不安装依赖。**下列命令只能在确认真实debug端口及明确授权后执行；占位路径不能直接用。**

```bash
python3 -B capture_ipdiag.py \
  --port /dev/cu.usbmodem已确认的日志口 \
  --confirm-debug-cdc --allow-session-config \
  --seconds 120 --output 本轮全新日志.jsonl
```

采集器先检查cu/tty占用，只开一个读取会话，拒绝覆盖输出。只保留固定IPDIAG数值schema与构建SHA身份行；旧MDIAG和其他普通日志都丢弃，不保留原文、哈希或输入载荷。它不会自动切BLE、配对、重连或执行按键。LISTENING仅代表串口已打开，**不是新鲜度、BLE路由或HID成功认证**；请用连续增长的固件时间/序号、当前epoch、active/selected状态核对，初期可能读到积压诊断。

## IPDIAG v1字段表

固定build token为 `ipad_enum_diag1_20261025`，它是诊断schema版本，不独自证明完整运行固件SHA。另有 `IPDIAG_ID v=1 build=… config=<40位提交> zmk=<40位提交> zephyr=<40位提交>` 身份行；config取云构建实际checkout提交，可与交付构建提交核对，但这仍不是设备Flash全量哈希回读。每条数值行包含固件t、全局seq、kind k、匿名连接索引c、连接代际ep，以及六个整数a/b/d/e/f/g；不含实际输入内容。

| k | 事件 | a / b / d / e / f / g |
| --- | --- | --- |
| 1 | 周期质量 | 累计ring dropped / 当前queued / TX抽样省略累计 / 无内容position累计 / Map总长337 / ring容量64 |
| 2 | 连接事件 | 1连接、2断开、3安全变化 / 错误或reason / security / 0 / 0 / 0 |
| 3 | 当前连接 | security / MTU / CCC mask / selected transport / active profile / 本连接是否active |
| 4 | 每report当前状态 | ID / type=1 Input / value handle / CCC bool或-1未知 / 本epoch调用累计 / 错误累计 |
| 5 | HOG读取 | 1 Map或2 ReportRef / handle / offset / requested length / 实际返回值 / 总长337或id|(type<<8) |
| 6 | 通知抽样 | ID / value handle / length / 原API返回值 / 本epoch调用累计 / security |

新IPDIAG使用严格行格式与seq检查，不含旧MDIAG CRC；采集器不声称验证了原始串口字节完整性。序号缺口、ring dropped及时间推进应一并分析。

calls是已返回的notify API累计次数，不是主机收包数；跨epoch返回不会计入新epoch，k1.d同时包含这类省略和正常TX节流省略。周期快照由多次只读查询组成，并非所有字段的原子快照。

CCC mask bit0..3分别对应ID1键盘、ID2消费者键、ID3鼠标、ID4手柄。selected transport 0=USB、1=BLE。c=255表示无连接；索引不是地址，也不是iPad身份认证。ReportRef type1=Input，type2=Output。各连接日志应按(c,ep)组合分析，重启后不能直接沿用epoch。

周期状态与计数不依赖旧MDIAG的30秒激活门。CCC是**当前快照**，不区分本次写入与旧bond恢复；没有write事件时不可宣称刚订阅。通知日志是抽样，不是所有报告；累计calls/errors及抽样省略、ring dropped必须一起看。Map覆盖需要按实际成功返回区间合并，不能用最大offset冒充读完。GATT回调之前被ATT权限拒绝的请求未必进入读取hook，缺读取仍不是没请求的证明。

## 诊断不改变的事

- 没有改变现有键位、轻拍判据、灯光、滚轮、指针和游戏层行为；所有168绑定沿用v2.2.1。
- 不降低安全、不主动请求新配对、不改变HID报告或通知参数，不以diagnostic输出发模拟键。
- 所有UF2、构建Run、完整SHA和设备反馈以本轮最终交付记录为准。主机测试和ARM编译只能证明软件/构建结果。
