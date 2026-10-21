# AA UPPER owner / pinned sticky regression

[主测试](<../test_aa_layer_owner.c>)直接组合实际 AA owner 行为与原始 SDK sticky、momentary-layer、to-layer 源码。

## 证据与限制

- 真实执行对象：SDK sticky 的 slot、timer 调度决定、quick-release、keycode `RAISE_AFTER`、旧实例清理，以及实际 AA owner 的 press/release/layer listener。
- Mock：内核时钟和 delayable work、DT/device 实例化、layer bit 操作、同步事件投递及其订阅顺序。
- `sticky_tap()` **只替代 hold-tap 的 tap-child 分派**；没有编译真实 hold-tap/keymap，也没有矩阵、RTOS 并发、settings、HID、USB/蓝牙或设备 I/O。不能把本测试通过表述为完整 ZMK 事件链或实机通过。
- 订阅宏没有清空：实际源码的监听器通过 host constructor 注册。layer bit 改变会同步发 layer event，故 pin 的 UPPER false → true 会真实进入 AA owner listener，防止遗漏 transition guard。
- `CONFIG_ZMK_BEHAVIOR_METADATA=0`，这里只验证时序/所有权，不验证 Studio 参数 metadata。

## 固定 SDK 来源

ZMK commit：`edf5c0814fd3ea202e43aad2d68fd32e882a518c`（v0.3.0）。SDK fixture 不加测试声明或修改实现；测试说明放在本文件。

| 本地 fixture | 原始源码 | 原始大小 | Git blob SHA-1 |
| --- | --- | ---: | --- |
| [sticky-key](<../sdk_v0_3_0_sticky_key.c>) | [原始 sticky-key](https://raw.githubusercontent.com/zmkfirmware/zmk/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/behaviors/behavior_sticky_key.c) | 16254 | `babdeb7da2406f7789f006fd094908b20b76245b` |
| [momentary-layer](<sdk_v0_3_0_momentary_layer.c>) | [原始 momentary-layer](https://raw.githubusercontent.com/zmkfirmware/zmk/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/behaviors/behavior_momentary_layer.c) | 1901 | `78bf7a3b28dc3576a3f400ef69c1f094c3e8ac57` |
| [to-layer](<sdk_v0_3_0_to_layer.c>) | [原始 to-layer](https://raw.githubusercontent.com/zmkfirmware/zmk/edf5c0814fd3ea202e43aad2d68fd32e882a518c/app/src/behaviors/behavior_to_layer.c) | 1989 | `dae19ebccc5b5acbb450552d2f8eb278b3c1d3fe` |

上述大小与 Git blob 身份来自固定 commit 的 GitHub Contents API。集成时已执行下方身份核验，三份原始字节全部一致；实际 AA owner 与三份 fixture 在 `-Wall/-Wextra/-Werror` + UBSan 下编译并运行 12 个测试通过。完整主机 runner 也固定校验这三份 blob；这些结果仍只是上述 host mock 边界内的软件证据。

## 从仓库根目录核验、编译及执行

先核对原字节（只读，不联网）：

```bash
python3 -B - <<'PY'
from pathlib import Path
import hashlib
fixtures = {
    'tests/sdk_v0_3_0_sticky_key.c': (16254, 'babdeb7da2406f7789f006fd094908b20b76245b'),
    'tests/aa_layer_mocks/sdk_v0_3_0_momentary_layer.c': (1901, '78bf7a3b28dc3576a3f400ef69c1f094c3e8ac57'),
    'tests/aa_layer_mocks/sdk_v0_3_0_to_layer.c': (1989, 'dae19ebccc5b5acbb450552d2f8eb278b3c1d3fe'),
}
for name, (size, expected) in fixtures.items():
    data = Path(name).read_bytes()
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    assert len(data) == size and actual == expected, (name, len(data), actual)
    print('PASS original SDK bytes', name, actual)
PY
```

编译只需 C11 编译器和 UBSan，不需要下载或安装 SDK；实际 AA owner 源码须已由主实现提交到约定路径：

```bash
cc -std=c11 -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable \
  -fsanitize=undefined -fno-sanitize-recover=all \
  -I tests/aa_layer_mocks tests/test_aa_layer_owner.c \
  -o /tmp/test_aa_layer_owner
/tmp/test_aa_layer_owner
```

## 覆盖

1. 旧 SL2 + `to UPPER` 在真实 SDK sticky timeout / quick-release 后退层的反例。
2. fixed owner 经同步 layer2 false → true 后仍有效，旧 sticky timeout 与 quick-release 均不可撤销它。
3. momentary owner 阻止旧 sticky UP 越权退层。
4. 外部退出清 owner；旧 SDK UP 不撤销后来由外部激活的 UPPER，也不改变新 SYM 层。
5. 无关 SYM1 false 不清 AA owner。
6. 普通一次性层 3000ms 超时、键码 quick-release、modifier 消费、绑定按住期间有键码、绑定 UP 才启动 timer、同位置重按替换旧 sticky slot。
7. MO/sticky/fixed release 幂等、P41 位置限制、失败 activate 无幽灵 owner。
