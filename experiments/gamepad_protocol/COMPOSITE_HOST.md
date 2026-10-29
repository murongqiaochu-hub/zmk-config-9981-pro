# 键鼠＋手柄共存：隔离主机模型

状态：2026-10-29。**仅本地宿主原型，不接入生产、没有新固件，也不是安卓/iPad/Moonlight成功证明。** 用户当前报告：全平台键鼠正常、所有手柄不可用，并已口头确认当前刷入 `11ee89e`；设备内镜像哈希、各主机应用和USB/BLE传输未独立核实。保持现有键位、设备、配对及回退，不额外请求用户测试。

## 结构与为什么需要补丁

基于固定 stock HOG 196 字节 Map 和此前用户报告安卓手柄曾可用的纯手柄 182 字节 Map。已只读核对 Source 2 ARM ELF 内一个 `zmk_hid_report_desc` 对象与196字节fixture逐字节一致（SHA-256 `5c7e4106019e5baf9027c8a24e334c58f5e09c65cd3ff46b338c551c61da4ee3`）；这不意味着该对象在纯手柄版中被注册成BLE服务。前者鼠标输入是 ID3；后者手柄输入也为 ID3。直接拼接会使输入报告混入鼠标及手柄两种应用集合，不能满足两者各自长度。

只把手柄输入 ID3 改为 ID4 仍不够：HID 全局属性跨 End Collection 保留，stock 鼠标末尾的 Logical Minimum 为 -32768，纯手柄方向帽前只设 Maximum 7，不显式设 Minimum；因此只改ID会使帽错误继承 -32768。本模型在手柄输入 ID4 后增加 `15 00`（Logical Minimum 0），结果保持原始 stock 字节不变，追加段184字节，总 Map 380 字节；手柄输出仍为 ID5。**这是可验证的候选结构，不是设备兼容结论。**

当前预期 Report Reference 按 `(ID, 类型)` 区分：`(1,input)` 键盘8字节、`(1,output)` LED 1字节、`(2,input)` Consumer 12字节、`(3,input)` 鼠标9字节、`(4,input)` 手柄10字节、`(5,output)` 手柄4字节。键盘输入与LED输出共享 ID1 合法；蓝牙报告正文不含ID前缀。当前4字节输出仅沿用纯实验的“接受但忽略”，不承诺振动。

## 代码与测试边界

- [composite_map_host.h](<composite_map_host.h>)：显式 `BBP_COMPOSITE_HOST_ONLY` 编译闸；只实现固定 stock Map 与实验 Map 的分片读取和复用现有编码器的安全包装。不会生成任何注册服务、广播、通知、settings、配置或键位变更。该模型仅检查 stock 长度；测试runner另外固定输入哈希。
- [test_composite_map_host.c](<../../tests/test_composite_map_host.c>)：全部偏移与越界、跨段/小块读取、三个交错调用、空参数及canary、原值保持、负值/中立/255轴、阈值，以及Map和编码器的源/目标缓冲重叠拒绝。三个调用是模型交错，并非真实多peer ATT。
- [run_composite_map_host.py](<../../tests/run_composite_map_host.py>)：分别用UBSan、非PIE ASan编译测试；解析**完整拼接字节流**的Logical/Physical范围、单位、Usage、flags、大小、顶层集合，并对源stock和源纯手柄字段逐项对照。按Array的完整Usage范围比较，明确拒绝本oracle未支持的32位Extended Usage，不冒充通用HID解析器。等长度的“只改ID、漏重置Minimum”和“手柄ID仍碰撞鼠标”反例必须失败；漏LED/重复或错误Report Reference、长度/保留位错误也必须失败；另编译实际代码变异负例和无host宏拒绝。
- [composite_hog_host.h](<composite_hog_host.h>) 与 [test_composite_hog_host.c](<../../tests/test_composite_hog_host.c>) 在固定SDK HOG fixture/GATT宏的单个 `hog_svc` 上，**仅在host编译**追加 output5 后 input4；原stock属性及回调保留。固定配置下验证29属性、6个引用及引用与值的正确对应、权限元数据、LED、键鼠发送和手柄10字节输入/4字节忽略输出。引用交换但集合不变的反例也被拒绝。
- [run_composite_hog_host.py](<../../tests/run_composite_hog_host.py>) 执行UBSan及非PIE ASan、独立编译闸与错误配置拒绝。原HID mock只含45字节玩具Map，实际模型Map是将SHA核验过的196字节fixture注入测试，不能说它直接来自该mock。逐peer CCC、ATT与同步通知是**测试替身**，不是实际Zephyr、安全加密、空口或操作系统识别；stock HOG自己的共享全局通知标志也仅保留原样，不宣称该行为被修复。
- [run_composite_hog_snapshot_host.py](<../../tests/run_composite_hog_snapshot_host.py>) 以真实编译器 `-include` 顺序先加载未改的触板snapshot、再加载复合host模型，随后编译固定 HOG fixture；分别验证 steady OFF/ON、已编译配置的鼠标FIFO 20项和diag ON。首次diag ON误预期4次入队诊断而失败，实际还包含一次 `-ESTALE`；修正oracle后8种 sanitizer 组合通过。键盘/Consumer队列各只测试1项，tap/Studio仅定义编译宏未执行完整路径，调度与通知仍为替身，不能推为设备稳定。
- [composite_ble_candidate.h](<composite_ble_candidate.h>) 是**默认未接入且必须显式启用**的实验性源码候选，复用现有runtime发送ABI，保持stock HOG键鼠、LED和控制点源码，尝试附加ID4/ID5。对应 [run_composite_ble_candidate.py](<../../tests/run_composite_ble_candidate.py>) 仅用固定HOG/触板源码与临时扩展的host mock验证；它**明确复现、尚未消除**CCC回调失效epoch到SDK提交新订阅值之间的新代读取旧on窗口，SDK已入队的多帧也不能靠epoch撤回。全局失效会保守清掉另一peer的待输入，断连/重连不一定走逐peer写回调，不保证旧peer收到neutral或ACK。
- [run_composite_candidate_runtime.py](<../../tests/run_composite_candidate_runtime.py>) 将未改的实际手柄runtime、候选发送与固定HOG源码组合，既测试同TU私有worker状态，也测试独立runtime/HOG对象链接；CCC、连接、时间、调度和USB仍为mock。新增19种 action 的真实runtime→候选编码→10B BLE正文独立oracle检查（含按键释放与左右杆路由），但不执行全套ZMK behavior dispatch。真实worker配合插桩的反例再现上述precommit窗口，并显示候选最后门后SDK模拟订阅检查返回 `-EINVAL` 时，现有runtime会终止重试并放弃旧neutral；不能推断设备收到松键。
- [run_ccc_att_order_model.py](<../../tests/run_ccc_att_order_model.py>) 与 [test_ccc_att_order_model.c](<../../tests/test_ccc_att_order_model.c>) 是新加的strict UBSan/ASan顺序模型：复现仅失效epoch仍可在CCC OFF提交前读到旧ON；验证 app-owned 固定队列槽位在同一抽象短锁下的入队/取消、peer隔离、ON仅提交后开放、无槽写0、断连/恢复/加密与旧token。**模型的 admission 定义是应用自己的队列槽位，不是Zephyr ATT队列**；worker先claim即视为已在途，之后即使OFF也可能再调用公开notify API；该API仍可能阻塞/失败。模型是确定性顺序oracle，不跑实际并发、Zephyr栈或设备，不能证明ATT入队原子性或远端收到。
- 对固定 Zephyr `dacab4875df72109b96cc8977547a0dc04875bcd` 的只读核对：`gatt.c:2113-2192` managed `cfg_write`先于 per-peer commit；无CCC槽写0于 `:2136-44` 直接返回；`cfg_changed`在 `:2091-2110` 仅aggregate改变才调用；settings恢复 `:5508-66` 直接写cfg，connected/security resync在 `:3213-75, 5665-5760`。Notify调用 `gatt.c:2409-2473`先分配/复制PDU再 `bt_att_send`；普通通知PDU及ATT meta分配默认可K_FOREVER (`att.c:657-751`)，conn TX callback context在非system-workqueue也可K_FOREVER (`conn.c:394-415`)，即使应用 completion func为空仍走ATT内部发送完成回调 (`att.c:544-587`)。`bt_att_send`将PDU放进ATT队列并尝试发送，但不传播同步处理失败、仍返回0 (`att.c:3886-3904`)；这只说明ATT层接收/排队，不等于conn TX队列、controller、空口或对端接收成功。EATT可能走另一条异步L2CAP路径，实际目标配置未核实。HCI优先断连于 `hci_core.c:822-45` 将state直接转 `DISCONNECT_COMPLETE`，`conn.c:1010-23`先赋state后续才清理；priority与普通RX路由可能处于不同线程，实际交错依HCI配置，该确定性模型不模拟它。现有树没有把 CCC commit 与 ATT队列 admission 放在同一把锁/事务内的应用API；不能把这个host模型或现有候选说成已修复该竞态。另在固定Zephyr副本的 `tests/bluetooth/gatt/src/ccc_order.c` 加入 native GATT ztest；使用仅在 `/dev/shm` 的临时Python venv、解包DTC/gperf及 `west.yml` 精确锁定的 TinyCrypt `3e9a49d2672ec01435ffbf0d788db6d95ef28de0`，以 `native_posix_64`/host GCC 成功配置、编译并运行：原 `test_gatt` 5/5、`test_gatt_ccc_order` 1/1（`ZTEST_EXIT=0`）。隔离测试 `prj.conf` 仅为两个合成peer设 `CONFIG_BT_MAX_CONN=2`。首轮测试暴露夹具漏初始化合连接地址会误命中空CCC槽；补齐role及地址后通过。此处原始CCC用例验证真实 `bt_gatt_attr_write_ccc` 的无槽写0短路、per-peer `cfg_write` 先于value提交、aggregate callback边沿；当时不验证断连/ATT并发或ATT入队阻塞，也不是目标固件或设备验收。测试代码仅在 `/dev/shm` Zephyr副本，不进入产品源码树。
- 2026-10-29在固定提交同一副本中加入 `test_att_send_requeues_at_disconnect_boundary`。
  - 夹具：`bt_conn_add_le` 真实连接池对象，`bt_conn_connected` 创建固定ATT bearer；GNU `--wrap=net_buf_put` 仅对目标PDU在首次入ATT共享FIFO前暂停。
  - 交错：测试线程将连接推进到 `BT_CONN_DISCONNECT_COMPLETE` 后放行发送线程；下游 `bt_conn_send_cb` 拒绝该连接，PDU被还回ATT FIFO，而非 `conn->tx_queue`；`bt_att_send` 仍返回0。之后显式推进 `DISCONNECTED`，执行 `bt_conn_process_tx` 并flush deferred work清理bearer。
  - 结果：最新标准构建运行日志 `run-att-cmake.log` 中 `test_gatt` 5/5、`test_gatt_ccc_order` 2/2、退出0；“not connected!”是预期的失败下游发送日志。
  - 边界：这是直接调用连接状态机的确定性交错，不派发合成HCI事件，不验证自然RX/HCI调度；不走公开GATT notify/CCC提交，也不证明控制器发送、ACK或设备行为。
  - 构建：为避免系统级安装，将 CMake 3.31.6 pip wheel 安装到 `/dev/shm/dsh-runtime/9981-cmake-3.31-isolated`；用其对现有隔离 `native_posix_64` build dir重新配置并标准 `cmake --build` 成功，生成的最终链接命令含 `--wrap=net_buf_put`。配置为 `BT_NO_DRIVER=y`、`BT_RECV_BLOCKING=n`、`BT_RECV_WORKQ_BT=y`、`BT_SMP=n`，不得外推到真实RX线程或加密行为。
- [stage_composite_candidate.py](<../../tools/stage_composite_candidate.py>) 仅创建新目录中的隔离源码，不运行构建、不触碰设备；要求显式确认HID数据库变化。**暂存副本的板级入口会拒绝常规固件CMake配置，只允许明确的CMake `-P` host记录测试**；这是防误用，不抵御人为修改源码或伪造环境。host契约要求SDK额外订阅检查、EATT关闭，对候选、快照、键位和关键源码加锁，并对固定Zephyr的ATT/GATT三文件做真实字节校验。Git重定向环境在首个SDK检查之前隔离，仓库本地fsmonitor/hook禁用；工作树根先规范化，工作树以下源路径的各级symlink被拒绝；CMake配置依赖可在普通源码时间戳变化时触发重验，但不能保证识别被保留的mtime或Git元数据变化，未来若重新设计构建闸，必须使用新建构建目录/完整fresh configure。`CANDIDATE.json` 的Map哈希为期望值，不是从设备或ARM产物读取的结果；其中明确标记当前不具备固件构建或设备测试条件。即使PnP/地址配置不变，HID数据库变化仍可能触发缓存问题，不自动清配对。

上述host模型、候选mock、实际runtime隔离联测与暂存测试均由 `tests/run_gamepad_tests.py` 调用。原有纯手柄 GATT fixture、原复合 Xbox Map 检查和旧 codec 穷举回归也分别运行，详见本批专项测试记录；这均属于 host 范围。

## 未来集成门槛

不得把纯手柄 service 直接和 stock HOG 同时注册，也不能复用旧9字节发送或硬编码 `pure_transport.h` 的属性位置。前三套模型严格host-only；新增候选是默认未接入的实验源码，触板snapshot前置链仍只在host固定fixture验证。距可考虑设备验证还需解决或明确评估CCC未线性化窗口。固定Zephyr现有 `bt_gatt_notify_cb`/`bt_att_send` 不提供可在 app gate 锁内安全调用的非阻塞ATT admission；要证明更强语义需另行设计栈内 try-reserve/入队事务及资源/断连所有权，或将合同明确收窄为app自有队列 admission 并接受已claim项可能晚于OFF调用公开notify。上面的新模型只验证后一种抽象合同，不是栈补丁。还需核实真实SDK/Zephyr的连接和队列、peer/epoch隔离、异步发送和在途断连、控制点/挂起、加密、缓存与安全回退。USB报告、键位、灯效、配对及settings必须保留。只有测试和授权充分时才可考虑候选ARM构建；苹果 `GCController.extendedGamepad` 和官方Moonlight仍需由用户决定是否实测。
