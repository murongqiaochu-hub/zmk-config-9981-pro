> **本地候选状态（2026-10-20）：v2.2.5 功能源码提交 `3c5235b21f8452630850053620635d4aea7cb47d`。** 锁轮默认不旋转；锁定时右肩三击按“左→上”步进，左肩三击按“右→上”反向步进；每组三击仅旋转一次，解锁 / 重锁回正。该候选尚未推送、未 ARM 构建、无新版 UF2；此前一次性授权不延伸到此版本。下方保留 v2.2.4 最近一次已推送 / 已构建交付记录，其锁轮默认方向不同；如需 v2.2.5 行为，请勿刷写下方旧 UF2。未刷写、未清配对、未擦设置。

# Xbox 布局标准 HID：v2.2.4 最近一次已发布 / 构建交付记录

## 最近一次已发布 / 构建版本 v2.2.4

- 硬件目标：9981 Pro / P9981，`bbp9981`，nRF52840；不是 Air。
- 协议：Xbox 布局标准 HID，**不是原生 XInput**。
- 功能源码提交：`9f41197bbafea13eab61ce926378dc13c8bacc69`；用户确认的 E/X/S/F=D-pad、D/K=L3/R3、触板点击与空格=Guide，并含 aA 锁轮旋转/肩键半速。
- 构建源提交：`3343a5005eb97b34728822db0422b1a7c5e178c5`；相对功能源码仅追加同步说明书/记录，`config/` 无差异。
- [GitHub Actions Run 39428313318](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39428313318>)：主机回归、ARM 构建、UF2 边界验证及归档全部成功。
- 目标分支：`codex/xbox-layout-20261020`；Actions 归档提交：`89311aa0d70e7219d8b51b07a9f14bbd683d1c03`。
- [v2.2.4 UF2](<3343a5005eb97b34728822db0422b1a7c5e178c5/bbp9981-zmk.uf2>)；[SHA256SUMS](<3343a5005eb97b34728822db0422b1a7c5e178c5/SHA256SUMS>)；[云端验证记录](<3343a5005eb97b34728822db0422b1a7c5e178c5/validation.json>)。

### UF2 独立校验摘要

```text
SHA-256  dc7027b0706d299e015c6d7646fa13ed7347bb32865d33d164296e8e386a8521
UF2 bytes 607232
Blocks    1186
Family    0xada52840 (nRF52840)
Addresses [0x1000, 0x4b200) — contiguous application blocks only
Source run 39428313318; device_tested=false
```

本地重新运行 `tools/verify_firmware.py` 的 UF2 校验函数并对照云端 `validation.json`、`SHA256SUMS`，长度、块数、MCU family、起止地址与 SHA-256 一致；地址位于应用区间 `0x1000–0xD4000`，不含 bootloader / NVS 块。构建日志使用 ZMK `build-user-config.yml@v0.3.0`，target 为 `bbp9981`。

**未刷写、未 USB / BT 真实枚举、未目标 App 实玩。** 不把主机模拟或 ARM 编译成功当成真实 IRQ / RTOS 行为、触板手感、矩阵鬼键或应用兼容验收；没有清配对、擦设置或修改主机配置。是否刷写由用户后续决定。

## 上一份实际 ARM 构建（仅历史，键图不同）

以下旧 UF2 对应 **S=↓、Z/K 未分配**，不可用于 v2.2.4：

- 实际 ARM 构建源：`1c61b817f7e2d52fd2bbea888d1867f354e4c2a8`。
- [GitHub Actions 39361865259](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39361865259>)：主机回归、ARM 构建、UF2 边界验证及归档全部成功。
- 首次归档提交：`f377ce5733c8193cc1afd6695ea3b0a25920d16a`。
- [原始 UF2](<1c61b817f7e2d52fd2bbea888d1867f354e4c2a8/bbp9981-zmk.uf2>)；SHA-256 `3b0b5124cd740b63889bcaaf720dfa532ee5cf1ad6574685a4ffbb767e02289c`；604672 bytes、1181 blocks、family `0xada52840`，地址 `[0x1000, 0x4ad00)`。
- 该旧 payload 的 141-byte Xbox HID 描述符与旧版生产源码一致；旧构建 FLASH 302168 B / 844 KB、RAM 88694 B / 256 KB。它只说明旧版，不能替代 v2.2.4 的构建/验证。
