/* SPDX-License-Identifier: Apache-2.0
 * Host-only ABI subset of pinned Zephyr gatt.h:
 * dacab4875df72109b96cc8977547a0dc04875bcd.
 * Attribute/service/characteristic/CCC/descriptor macro EXPANSIONS below are
 * copied faithfully, not a count-only stand-in. Iterable linker placement and
 * UUID storage are the only simplified declaration machinery.
 */
#pragma once
#include <sys/types.h>
#include <zephyr/bluetooth/conn.h>

struct bt_uuid { uint16_t val; };
#define BT_UUID_DECLARE_16(v) ((struct bt_uuid[]){{.val = (v)}})
#define BT_UUID_GATT_PRIMARY BT_UUID_DECLARE_16(0x2800)
#define BT_UUID_GATT_CHRC BT_UUID_DECLARE_16(0x2803)
#define BT_UUID_GATT_CCC BT_UUID_DECLARE_16(0x2902)
#define BT_UUID_HIDS BT_UUID_DECLARE_16(0x1812)
#define BT_UUID_HIDS_INFO BT_UUID_DECLARE_16(0x2a4a)
#define BT_UUID_HIDS_REPORT_MAP BT_UUID_DECLARE_16(0x2a4b)
#define BT_UUID_HIDS_REPORT BT_UUID_DECLARE_16(0x2a4d)
#define BT_UUID_HIDS_REPORT_REF BT_UUID_DECLARE_16(0x2908)
#define BT_UUID_HIDS_CTRL_POINT BT_UUID_DECLARE_16(0x2a4c)
#define BT_GATT_PERM_READ BIT(0)
#define BT_GATT_PERM_WRITE BIT(1)
#define BT_GATT_PERM_READ_ENCRYPT BIT(2)
#define BT_GATT_PERM_WRITE_ENCRYPT BIT(3)
#define BT_GATT_CHRC_READ 0x02
#define BT_GATT_CHRC_WRITE_WITHOUT_RESP 0x04
#define BT_GATT_CHRC_WRITE 0x08
#define BT_GATT_CHRC_NOTIFY 0x10
#define BT_GATT_CCC_NOTIFY 0x0001
#define BT_ATT_ERR_INVALID_OFFSET 0x07
#define BT_ATT_ERR_INVALID_ATTRIBUTE_LEN 0x0d
#define BT_ATT_ERR_UNLIKELY 0x0e
#define BT_GATT_ERR(_att_err) (-(_att_err))

struct bt_gatt_attr;
typedef ssize_t (*bt_gatt_attr_read_func_t)(struct bt_conn *, const struct bt_gatt_attr *,
                                           void *, uint16_t, uint16_t);
typedef ssize_t (*bt_gatt_attr_write_func_t)(struct bt_conn *, const struct bt_gatt_attr *,
                                            const void *, uint16_t, uint16_t, uint8_t);
struct bt_gatt_attr {
    const struct bt_uuid *uuid;
    bt_gatt_attr_read_func_t read;
    bt_gatt_attr_write_func_t write;
    void *user_data;
    uint16_t handle;
    uint16_t perm;
};
struct bt_gatt_service_static { const struct bt_gatt_attr *attrs; size_t attr_count; };
struct bt_gatt_chrc { const struct bt_uuid *uuid; uint16_t value_handle; uint8_t properties; };
#define BT_GATT_CCC_MAX 4
struct bt_gatt_ccc_cfg { uint8_t id; bt_addr_le_t peer; uint16_t value; };
struct _bt_gatt_ccc {
    struct bt_gatt_ccc_cfg cfg[BT_GATT_CCC_MAX];
    uint16_t value;
    void (*cfg_changed)(const struct bt_gatt_attr *, uint16_t);
    ssize_t (*cfg_write)(struct bt_conn *, const struct bt_gatt_attr *, uint16_t);
    bool (*cfg_match)(struct bt_conn *, const struct bt_gatt_attr *);
};

ssize_t bt_gatt_attr_read(struct bt_conn *, const struct bt_gatt_attr *, void *, uint16_t,
                          uint16_t, const void *, uint16_t);
ssize_t bt_gatt_attr_read_service(struct bt_conn *, const struct bt_gatt_attr *, void *,
                                  uint16_t, uint16_t);
ssize_t bt_gatt_attr_read_chrc(struct bt_conn *, const struct bt_gatt_attr *, void *,
                               uint16_t, uint16_t);
ssize_t bt_gatt_attr_read_ccc(struct bt_conn *, const struct bt_gatt_attr *, void *,
                              uint16_t, uint16_t);
ssize_t bt_gatt_attr_write_ccc(struct bt_conn *, const struct bt_gatt_attr *, const void *,
                               uint16_t, uint16_t, uint8_t);

/* Only iterable linker placement is omitted on the host. */
#define STRUCT_SECTION_ITERABLE(type, name) struct type name
#define BT_GATT_SERVICE_DEFINE(_name, ...) \
    const struct bt_gatt_attr attr_##_name[] = { __VA_ARGS__ }; \
    const STRUCT_SECTION_ITERABLE(bt_gatt_service_static, _name) = \
        BT_GATT_SERVICE(attr_##_name)
#define BT_GATT_SERVICE(_attrs) \
    { .attrs = _attrs, .attr_count = ARRAY_SIZE(_attrs), }
#define BT_GATT_PRIMARY_SERVICE(_service) \
    BT_GATT_ATTRIBUTE(BT_UUID_GATT_PRIMARY, BT_GATT_PERM_READ, \
                      bt_gatt_attr_read_service, NULL, _service)
#define BT_GATT_CHRC_INIT(_uuid, _handle, _props) \
    { .uuid = _uuid, .value_handle = _handle, .properties = _props, }
#define BT_GATT_CHARACTERISTIC(_uuid, _props, _perm, _read, _write, _user_data) \
    BT_GATT_ATTRIBUTE(BT_UUID_GATT_CHRC, BT_GATT_PERM_READ, \
                      bt_gatt_attr_read_chrc, NULL, \
                      ((struct bt_gatt_chrc[]) { BT_GATT_CHRC_INIT(_uuid, 0U, _props), })), \
    BT_GATT_ATTRIBUTE(_uuid, _perm, _read, _write, _user_data)
#define BT_GATT_CCC_INITIALIZER(_changed, _write, _match) \
    { .cfg = {}, .cfg_changed = _changed, .cfg_write = _write, .cfg_match = _match, }
#define BT_GATT_CCC_MANAGED(_ccc, _perm) \
    BT_GATT_ATTRIBUTE(BT_UUID_GATT_CCC, _perm, \
                      bt_gatt_attr_read_ccc, bt_gatt_attr_write_ccc, _ccc)
#define BT_GATT_CCC(_changed, _perm) \
    BT_GATT_CCC_MANAGED(((struct _bt_gatt_ccc[]) \
        {BT_GATT_CCC_INITIALIZER(_changed, NULL, NULL)}), _perm)
#define BT_GATT_DESCRIPTOR(_uuid, _perm, _read, _write, _user_data) \
    BT_GATT_ATTRIBUTE(_uuid, _perm, _read, _write, _user_data)
#define BT_GATT_ATTRIBUTE(_uuid, _perm, _read, _write, _user_data) \
    { .uuid = _uuid, .read = _read, .write = _write, .user_data = _user_data, \
      .handle = 0, .perm = _perm, }

typedef void (*bt_gatt_complete_func_t)(struct bt_conn *, void *);
struct bt_gatt_notify_params {
    const struct bt_uuid *uuid;
    const struct bt_gatt_attr *attr;
    const void *data;
    uint16_t len;
    bt_gatt_complete_func_t func;
    void *user_data;
};
int bt_gatt_notify_cb(struct bt_conn *, struct bt_gatt_notify_params *);
bool bt_gatt_is_subscribed(struct bt_conn *, const struct bt_gatt_attr *, uint16_t);
