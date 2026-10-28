# 键鼠＋手柄共存：隔离主机模型

状态：2026-10-28。**仅本地宿主原型，不接入生产、没有新固件，也不是安卓/iPad/Moonlight成功证明。** 用户最新更正：全平台键鼠正常、所有手柄不可用；当前设备最终固件、各主机应用和传输未逐一确认。保持现有键位、设备、配对及回退，不额外请求用户测试。

## 结构与为什么需要补丁

基于固定 stock HOG 196 字节 Map 和此前用户报告安卓手柄曾可用的纯手柄 182 字节 Map。前者鼠标输入是 ID3；后者手柄输入也为 ID3。直接拼接会使输入报告混入鼠标及手柄两种应用集合，不能满足两者各自长度。

只把手柄输入 ID3 改为 ID4 仍不够：HID 全局属性跨 End Collection 保留，stock 鼠标末尾的 Logical Minimum 为 -32768，纯手柄方向帽前只设 Maximum 7，不显式设 Minimum；因此只改ID会使帽错误继承 -32768。本模型在手柄输入 ID4 后增加 `15 00`（Logical Minimum 0），结果保持原始 stock 字节不变，追加段184字节，总 Map 380 字节；手柄输出仍为 ID5。**这是可验证的候选结构，不是设备兼容结论。**

当前预期 Report Reference 按 `(ID, 类型)` 区分：`(1,input)` 键盘8字节、`(1,output)` LED 1字节、`(2,input)` Consumer 12字节、`(3,input)` 鼠标9字节、`(4,input)` 手柄10字节、`(5,output)` 手柄4字节。键盘输入与LED输出共享 ID1 合法；蓝牙报告正文不含ID前缀。当前4字节输出仅沿用纯实验的“接受但忽略”，不承诺振动。

## 代码与测试边界

- [composite_map_host.h](<composite_map_host.h>)：显式 `BBP_COMPOSITE_HOST_ONLY` 编译闸；只实现固定 stock Map 与实验 Map 的分片读取和复用现有编码器的安全包装。不会生成任何注册服务、广播、通知、settings、配置或键位变更。该模型仅检查 stock 长度；测试runner另外固定输入哈希。
- [test_composite_map_host.c](<../../tests/test_composite_map_host.c>)：全部偏移与越界、跨段/小块读取、三个交错调用、空参数及canary、原值保持、负值/中立/255轴、阈值，以及Map和编码器的源/目标缓冲重叠拒绝。三个调用是模型交错，并非真实多peer ATT。
- [run_composite_map_host.py](<../../tests/run_composite_map_host.py>)：分别用UBSan、非PIE ASan编译测试；解析**完整拼接字节流**的Logical/Physical范围、单位、Usage、flags、大小、顶层集合，并对源stock和源纯手柄字段逐项对照。按Array的完整Usage范围比较，明确拒绝本oracle未支持的32位Extended Usage，不冒充通用HID解析器。等长度的“只改ID、漏重置Minimum”和“手柄ID仍碰撞鼠标”反例必须失败；漏LED/重复或错误Report Reference、长度/保留位错误也必须失败；另编译实际代码变异负例和无host宏拒绝。
- [composite_hog_host.h](<composite_hog_host.h>) 与 [test_composite_hog_host.c](<../../tests/test_composite_hog_host.c>) 在固定SDK HOG fixture/GATT宏的单个 `hog_svc` 上，**仅在host编译**追加 output5 后 input4；原stock属性及回调保留。固定配置下验证29属性、6个引用及引用与值的正确对应、权限元数据、LED、键鼠发送和手柄10字节输入/4字节忽略输出。引用交换但集合不变的反例也被拒绝。
- [run_composite_hog_host.py](<../../tests/run_composite_hog_host.py>) 执行UBSan及非PIE ASan、独立编译闸与错误配置拒绝。原HID mock只含45字节玩具Map，实际模型Map是将SHA核验过的196字节fixture注入测试，不能说它直接来自该mock。逐peer CCC、ATT与同步通知是**测试替身**，不是实际Zephyr、安全加密、空口或操作系统识别；stock HOG自己的共享全局通知标志也仅保留原样，不宣称该行为被修复。

分别运行 `PYTHONDONTWRITEBYTECODE=1 python3 -B tests/run_composite_map_host.py` 和 `PYTHONDONTWRITEBYTECODE=1 python3 -B tests/run_composite_hog_host.py`；两项也由 `tests/run_gamepad_tests.py` 自动调用。原有纯手柄 GATT fixture、原复合 Xbox Map 检查和旧 codec 穷举回归也分别运行，详见本批专项测试记录；这均属于 host 范围。

## 未来集成门槛

不得把纯手柄 service 直接和 stock HOG 同时注册，也不能复用旧9字节发送或硬编码 `pure_transport.h` 的属性位置。当前两个文件都严格host-only；距实际生产接线还需验证现有 `trackpad_hog_snapshot` 前置链、真实SDK/Zephyr的per-peer CCC、队列与peer/epoch隔离、异步发送和在途断连、控制点/挂起、加密、边界回退。USB报告、键位、灯效、配对及settings必须不变。只有测试和授权充分时才可考虑候选ARM构建；苹果 `GCController.extendedGamepad` 和官方Moonlight仍需由用户决定是否实测。
