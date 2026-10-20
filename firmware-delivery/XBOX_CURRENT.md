# 当前 Xbox 布局标准 HID 候选交付

- 硬件：9981 Pro / P9981，`bbp9981`，nRF52840；不是 Air。
- 协议：Xbox 布局标准 HID，**不是原生 XInput**。
- 实际 ARM 构建源：`1c61b817f7e2d52fd2bbea888d1867f354e4c2a8`。
- [GitHub Actions 39361865259](<https://github.com/murongqiaochu-hub/zmk-config-9981-pro/actions/runs/39361865259>)：主机回归、ARM 构建、UF2 边界验证及归档全部成功。
- 首次归档提交：`f377ce5733c8193cc1afd6695ea3b0a25920d16a`。
- [原始 UF2](<1c61b817f7e2d52fd2bbea888d1867f354e4c2a8/bbp9981-zmk.uf2>)：[SHA256SUMS](<1c61b817f7e2d52fd2bbea888d1867f354e4c2a8/SHA256SUMS>)、[云端验证记录](<1c61b817f7e2d52fd2bbea888d1867f354e4c2a8/validation.json>)、[实际描述符 / 配置复核](<1c61b817f7e2d52fd2bbea888d1867f354e4c2a8/gamepad_validation.json>)。

## 校验摘要

```text
SHA-256  3b0b5124cd740b63889bcaaf720dfa532ee5cf1ad6574685a4ffbb767e02289c
UF2 bytes 604672
Blocks    1181
Family    0xada52840
Addresses [0x1000, 0x4ad00)
```

本地下载件与 Git 归档件逐字节相同；1181 块均在应用分区，不含 bootloader / NVS 块。实际 ARM payload 中的 141-byte Xbox HID 描述符与生产源码完全一致，分别出现于 `0x444ed`、`0x46255`；ROM 策略标记确认 ID4 / body9 / USB10、13 个真实按钮、四杆轴与数字 LT/RT。

构建日志确认 ZMK `edf5c0814fd3ea202e43aad2d68fd32e882a518c`、Zephyr `dacab4875df72109b96cc8977547a0dc04875bcd`、hal_nordic `884c4d61746bc35fbd379c169fc87ddb56c6461d`；`CONFIG_BBP9981_GAMEPAD=y`、`CONFIG_A320_MICROTAP=y`、USB HID 实例2 / reports4。FLASH 302168 B / 844 KB，RAM 88694 B / 256 KB。

## 仍未验证、未授权的事项

**未刷写、未 USB / BT 真实枚举、未目标 App 实玩**，不把编译成功或 host mock 作为真实 IRQ / RTOS 行为、触板手感、矩阵三键或应用兼容性的验收。LT/RT 仅 0/255；单触板不能同时独立双杆；Guide 可能被截获。没有执行清配对、擦设置或主机配置变更。

默认布局、操作和逐项验收见[使用说明书](<../docs/9981Pro_使用说明书.md>)。旧 GBA 候选及其祖先历史保留在本地，未进入本发布分支。
