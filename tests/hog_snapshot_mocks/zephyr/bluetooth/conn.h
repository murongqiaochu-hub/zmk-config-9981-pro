/* SPDX-License-Identifier: MIT -- dedicated host test mock */
#ifndef HOG_SNAPSHOT_MOCK_CONN_H
#define HOG_SNAPSHOT_MOCK_CONN_H
#include <stdint.h>
typedef struct { uint8_t val[6]; } bt_addr_t;
typedef struct { uint8_t type; bt_addr_t a; } bt_addr_le_t;
enum bt_conn_state {
    BT_CONN_STATE_DISCONNECTED, BT_CONN_STATE_CONNECTING,
    BT_CONN_STATE_CONNECTED, BT_CONN_STATE_DISCONNECTING,
};
typedef enum { BT_SECURITY_L0, BT_SECURITY_L1, BT_SECURITY_L2 } bt_security_t;
struct bt_conn {
    bt_addr_le_t dst;
    enum bt_conn_state state;
    int refs, info_error;
};
struct bt_conn_info { enum bt_conn_state state; };
struct bt_conn *bt_conn_ref(struct bt_conn *conn);
void bt_conn_unref(struct bt_conn *conn);
const bt_addr_le_t *bt_conn_get_dst(const struct bt_conn *conn);
int bt_conn_get_info(const struct bt_conn *conn, struct bt_conn_info *info);
int bt_conn_set_security(struct bt_conn *conn, bt_security_t security);
#endif
