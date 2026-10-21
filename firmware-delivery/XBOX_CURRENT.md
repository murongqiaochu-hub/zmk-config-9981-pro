> **2026-10-21 v2.2.6 未发布候选已完成 Actions ARM 构建与 UF2 本地校验。** 功能源码 `7a956e9b1a7fb2dc465bfdb9212cdf7f27977e2b`；构建提交 `b05dee65dc929855c809036c539b14488cd7d4b0`，分支 `codex/xbox-layout-v2.2.6-20261021`；[Actions Run 39540020475](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39540020475>) 的 host regression 与 `bbp9981` ARM 构建 PASS，候选归档 job 跳过。UF2 SHA-256 `9f4daf7b22ab833c01ed5322115b1a8977cbd4aff8701c04e29ddd73b76d155a`，611328 bytes / 1194 blocks / family `0xada52840` / 地址 `[0x1000, 0x4ba00)`；[本地 UF2](</vol1/1000/agent/output/1021/105843_9981ProXbox固件_v2.2.6/09_云端构建产物/bbp9981-zmk.uf2>) 与[校验记录](</vol1/1000/agent/output/1021/105843_9981ProXbox固件_v2.2.6/09_云端构建产物/validation.json>)。只推新候选分支，未归档至发布分支；未刷写、未清配对、未擦设置或实机验收。v2.2.4 仍为最后已发布 / 归档版本。

# Xbox 布局标准 HID：v2.2.6 候选状态

## v2.2.6 实现范围

- MEDIA：P0/P1=LB/RB，P2/P6=LT/RT，A/P17=D-pad 左，O/K/Backspace/$=Y/X/B/A，D/L=L3/R3；只留 SYM/P40 与右 aA/P41 两个出口。
- 锁轮：锁定且未按 aA 时，P3 三击左→上、P5 三击右→上；锁定期间 P3 永不选槽1，P5 非三击吞掉。按住右 aA 时 P5 双击切换 volatile 鼠标模式，不发槽2。
- 鼠标模式：普通指针 Y 反向；DEFAULT 肩键 LCLK/RCLK 角色互换；X、滚轮、手柄及 SYM/UPPER/MEDIA 键位/肩键功能不变，重启关闭。
- 完整 `python3 -B tests/run_host_tests.py` exit 0；scope gate 保持 45 份旧配置逐字节相同。Actions Run 39540020475 的 `regression` 与 `bbp9981` ARM build PASS；候选分支 `deliver` job 按分支条件跳过，UF2 只在本次输出目录下载并本地校验，未归档。

> **历史候选 v2.2.5（未发布 / 未归档）。** 功能源码 `3c5235b21f8452630850053620635d4aea7cb47d`；构建提交 `dcfabda69c9ed81a7fb92aaf0df29e74566be763`，分支 `codex/xbox-layout-v2.2.5-20261021`；[GitHub Actions Run 39520450915](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39520450915>) 的 host regressions 与 `bbp9981` ARM 构建 PASS，候选归档 job 跳过。UF2 SHA-256 `6b562773e714d564da1b4351db185e63adeefbf3ad6f2139be9311249fde5b5f`，608256 bytes / 1188 blocks / family `0xada52840` / 地址 `[0x1000, 0x4b400)`；未刷写，v2.2.4 仍为最后已发布 / 归档版本。

# Xbox 布局标准 HID：v2.2.4 最后已发布 / 归档交付记录

## 最后已发布 / 归档版本 v2.2.4

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

## 更早的历史 ARM 构建（仅历史，键图不同）

以下旧 UF2 对应 **S=↓、Z/K 未分配**，不可用于 v2.2.4：

- 实际 ARM 构建源：`1c61b817f7e2d52fd2bbea888d1867f354e4c2a8`。
- [GitHub Actions 39361865259](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39361865259>)：主机回归、ARM 构建、UF2 边界验证及归档全部成功。
- 首次归档提交：`f377ce5733c8193cc1afd6695ea3b0a25920d16a`。
- [原始 UF2](<1c61b817f7e2d52fd2bbea888d1867f354e4c2a8/bbp9981-zmk.uf2>)；SHA-256 `3b0b5124cd740b63889bcaaf720dfa532ee5cf1ad6574685a4ffbb767e02289c`；604672 bytes、1181 blocks、family `0xada52840`，地址 `[0x1000, 0x4ad00)`。
- 该旧 payload 的 141-byte Xbox HID 描述符与旧版生产源码一致；旧构建 FLASH 302168 B / 844 KB、RAM 88694 B / 256 KB。它只说明旧版，不能替代 v2.2.4 的构建/验证。
