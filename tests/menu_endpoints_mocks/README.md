# MDIAG v2 独立端点/日志主机 fixture

这些头只模拟 SDK 状态/调用计数、日志、IRQ 与调度，不证明电气、USB 主机或蓝牙实测；不读取地址、配对信息或凭证。

以下文件来自 `zmkfirmware/zmk` 的 `v0.3.0`，字节原样保存；其余同目录头是测试 mock。

| 本地文件 | SHA-256 | 上游 |
|---|---|---|
| [端点源 fixture](<../sdk_v0_3_0_endpoints.c>) | `0ff5c15c0e07092586df7b40ba67f1c7cfddbe34c86242d6d0f72ed9d72b9d8e` | [endpoints.c](https://raw.githubusercontent.com/zmkfirmware/zmk/v0.3.0/app/src/endpoints.c) |
| [端点接口](<zmk/endpoints.h>) | `d328ffd8472d12cc4f10562d9461372dbab577177c27c23a019ce8b3d57b85d8` | [endpoints.h](https://raw.githubusercontent.com/zmkfirmware/zmk/v0.3.0/app/include/zmk/endpoints.h) |
| [真实枚举与结构](<zmk/endpoints_types.h>) | `736852811dbc99b9d4b1523bf574374513c55e8c97c65c15ff9b1e36c16e088c` | [endpoints_types.h](https://raw.githubusercontent.com/zmkfirmware/zmk/v0.3.0/app/include/zmk/endpoints_types.h) |
| [输出参数](<dt-bindings/zmk/outputs.h>) | `fb0e1937478ee16aad86da0108882b9fc64fc20e4bd2383883411811d8b35614` | [outputs.h](https://raw.githubusercontent.com/zmkfirmware/zmk/v0.3.0/app/include/dt-bindings/zmk/outputs.h) |

## 边界与协议

- USB=0 / BLE=1 由真实 `endpoints_types.h` 验证；OUT_BLE 参数与 route enum 并非同一个常量。
- 每行 `MDIAG v=2 build=tap200_route_20261016 t=<u32> seq=<u32> k=<u> a=<i32> b=<i32> c=<i32> d=<i32> crc=<8hex>`。
- CRC 是对 `[t,seq,k,a,b,c,d]` 的每个 u32 小端字节依次 FNV-1a32（初值 2166136261、乘数 16777619）；有符号值取 two's complement。非密码学、非设备镜像 hash。
- kind11 与 kind15 在同一锁内成对入 FIFO，同 t、相邻 seq（u32 回绕）；kind15.b 为成对 push 前 queued，可 0。满 FIFO/64 次关键事件额度时整对丢弃，下一 tick 重试，绝不产生半对。
- kind12 phase0 的 b=0 仅表示“尚无返回”，phase1 为原函数真实返回；公开 toggle 内部调用重命名 stock select，不生成重复观察。
- 普通 Q7 不记录；只有右 aA41 物理按住起 5000ms 内才记 Q 的位置/运行 binding 候选，释放立即关闭。kind13 的 layer 是当前最高活动层 index 转换成的 ID。`zmk_keymap_get_layer_binding_at_idx` 可读当前映射，但公共 API 不提供 hold-tap 最终派发或 release 绑定捕获，因此不能把 kind13 候选断言为已调用 OUT_BLE；kind12 才证明公开选路函数调用。缺失/无设备/透明候选记录 unknown。
- 路由心跳包含 HID selected/current_instance、preferred、USB HID ready、active BLE connected、active BLE index；这些是多个只读 getter 的采样，不宣称原子无线状态。
- 128 FIFO，raw/debounce 共 32/s，其余含心跳共 64/s。每 tick 最多打印4条，active 25ms / inactive 250ms。饱和丢弃/队列延迟可由质量心跳看见，不用无 CRC 的旧 status 行。

## 主机测试命令

```sh
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -fsanitize=undefined -fno-sanitize-recover=all -I tests/menu_endpoints_mocks tests/test_menu_diag.c -o .host-test-build/test_menu_diag
.host-test-build/test_menu_diag
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -fsanitize=undefined -fno-sanitize-recover=all -I tests/menu_endpoints_mocks tests/test_menu_endpoints.c -o .host-test-build/test_menu_endpoints
.host-test-build/test_menu_endpoints
```

可附 `-DCONFIG_SETTINGS=0` 验证无 settings 版本，或 `-DMD_TEST_WRAPPER_FAILURE=1` 用独立故障 stock mock 验证非零返回透传。真实 SDK fixture 测试与故障 mock 测试不能互称。
