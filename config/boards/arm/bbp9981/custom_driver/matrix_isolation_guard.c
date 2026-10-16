/* SPDX-License-Identifier: MIT
 * Compile-time final-DT guard + boot identity only. No GPIO reads/writes here.
 * OPEN_SOURCE is nRF D0S1: logical0 disconnected, logical1 STANDARD high.
 * This does not establish PCB diode topology, analogue settling or NKRO.
 */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_gpio.h>
LOG_MODULE_REGISTER(matrix_isolation, LOG_LEVEL_INF);
#define KS DT_NODELABEL(kscan0)
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_NRF52840) || IS_ENABLED(CONFIG_SOC_NRF52840_QIAA), "nRF52840 only");
BUILD_ASSERT(IS_ENABLED(CONFIG_GPIO_NRFX), "Require the reviewed nrfx GPIO driver");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_KSCAN_MATRIX_POLLING), "Preserve interrupt scanning");
BUILD_ASSERT(DT_NODE_HAS_COMPAT(KS, zmk_kscan_gpio_matrix), "Keep native matrix");
BUILD_ASSERT(DT_SAME_NODE(DT_CHOSEN(zmk_kscan), KS), "chosen matrix changed");
BUILD_ASSERT(DT_PROP_LEN(KS, row_gpios) == 6 && DT_PROP_LEN(KS, col_gpios) == 10, "Matrix dimensions changed");
BUILD_ASSERT(DT_ENUM_IDX(KS, diode_direction) == 1, "col2row required");
BUILD_ASSERT(DT_PROP(KS, debounce_scan_period_ms) == 1, "Preserve native1ms scan");
BUILD_ASSERT(CONFIG_ZMK_KSCAN_MATRIX_WAIT_BEFORE_INPUTS == 0 && CONFIG_ZMK_KSCAN_MATRIX_WAIT_BETWEEN_OUTPUTS == 0, "Do not change column wait in this single-variable experiment");
BUILD_ASSERT(CONFIG_ZMK_KSCAN_DEBOUNCE_PRESS_MS == 10 && CONFIG_ZMK_KSCAN_DEBOUNCE_RELEASE_MS == 10, "Do not hide input breaks with debounce changes");
BUILD_ASSERT(GPIO_OPEN_SOURCE == 2 && GPIO_PULL_DOWN == 32 && NRF_GPIO_PIN_D0S1 == 4, "Reviewed SDK flag/drive ABI changed");
#define COL(n) BUILD_ASSERT(DT_GPIO_FLAGS_BY_IDX(KS, col_gpios, n) == GPIO_OPEN_SOURCE, "Column must be active-high, open-source, standard drive, no pulls")
COL(0); COL(1); COL(2); COL(3); COL(4); COL(5); COL(6); COL(7); COL(8); COL(9);
#define ROW(n) BUILD_ASSERT(DT_GPIO_FLAGS_BY_IDX(KS, row_gpios, n) == GPIO_PULL_DOWN, "Keep active-high pulldown inputs")
ROW(0); ROW(1); ROW(2); ROW(3); ROW(4); ROW(5);
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(KS, row_gpios, 4), DT_NODELABEL(gpio0)) && DT_GPIO_PIN_BY_IDX(KS, row_gpios, 4) == 12, "row4 moved");
#ifndef BBP_ISO_ZEPHYR_REV
#error "CMake must supply the actual resolved Zephyr revision"
#endif
static int matrix_isolation_identity(void) {
    LOG_INF("MATRIX_ISO col_hiz_20261017 zephyr=" BBP_ISO_ZEPHYR_REV " D0S1=4 cols=10 wait=0/0");
    return 0;
}
SYS_INIT(matrix_isolation_identity, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
