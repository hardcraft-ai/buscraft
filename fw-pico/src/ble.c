// BLE GATT service (BTstack). Writes are queued in the BTstack callback and handled from the main loop; replies are
// queued and sent as notifications whenever the stack can send.
#include "ble.h"

#include <string.h>

#include "btstack.h"
#include "buscraft.h"
#include "komento.h"
#include "net.h"

#define H_COMMAND  ATT_CHARACTERISTIC_B72B0002_CE61_48A9_8A39_81A476335192_01_VALUE_HANDLE
#define H_RESPONSE ATT_CHARACTERISTIC_B72B0003_CE61_48A9_8A39_81A476335192_01_VALUE_HANDLE
#define H_RESP_CCC ATT_CHARACTERISTIC_B72B0003_CE61_48A9_8A39_81A476335192_01_CLIENT_CONFIGURATION_HANDLE
#define H_STATUS   ATT_CHARACTERISTIC_B72B0005_CE61_48A9_8A39_81A476335192_01_VALUE_HANDLE

// Flags, complete local name "BusCraft" and the 128-bit service UUID (little endian): 31 bytes.
static const uint8_t adv_data[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x09, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'B', 'u', 's', 'C', 'r', 'a', 'f', 't',
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x92, 0x51, 0x33, 0x76, 0xa4, 0x81, 0x39, 0x8a, 0xa9, 0x48, 0x61, 0xce, 0x01, 0x00, 0x2b, 0xb7,
};

static btstack_packet_callback_registration_t hci_cb;
static hci_con_handle_t con = HCI_CON_HANDLE_INVALID;
static bool notify_on;

static uint8_t rx[1024];
static volatile size_t rx_head, rx_tail;
static uint8_t tx[4096];
static size_t tx_head, tx_tail;
static komento_input_t ble_in;

static void rx_put(uint8_t c) {
    size_t next = (rx_head + 1) % sizeof rx;
    if (next != rx_tail) { rx[rx_head] = c; rx_head = next; }
}

static void ble_sink(const void *data, size_t n) {
    if (con == HCI_CON_HANDLE_INVALID || !notify_on) return;
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) {
        size_t next = (tx_head + 1) % sizeof tx;
        if (next == tx_tail) break;          // client not reading fast enough: drop the rest
        tx[tx_head] = p[i];
        tx_head = next;
    }
    att_server_request_can_send_now_event(con);
}

static void send_some(void) {
    if (con == HCI_CON_HANDLE_INVALID || tx_tail == tx_head) return;
    uint8_t chunk[244];
    uint16_t max = att_server_get_mtu(con) - 3;
    if (max > sizeof chunk) max = sizeof chunk;
    uint16_t n = 0;
    size_t t = tx_tail;
    while (n < max && t != tx_head) { chunk[n++] = tx[t]; t = (t + 1) % sizeof tx; }
    if (att_server_notify(con, H_RESPONSE, chunk, n) == ERROR_CODE_SUCCESS) tx_tail = t;
    if (tx_tail != tx_head) att_server_request_can_send_now_event(con);
}

static uint16_t att_read(hci_con_handle_t c, uint16_t handle, uint16_t offset, uint8_t *buf, uint16_t size) {
    if (handle == H_STATUS) {
        char s[128];
        net_status(s, sizeof s);
        return att_read_callback_handle_blob((const uint8_t *)s, (uint16_t)strlen(s), offset, buf, size);
    }
    return 0;
}

static int att_write(hci_con_handle_t c, uint16_t handle, uint16_t mode, uint16_t offset, uint8_t *buf, uint16_t size) {
    if (handle == H_RESP_CCC) {
        notify_on = little_endian_read_16(buf, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
        con = c;
        return 0;
    }
    if (handle == H_COMMAND) {
        for (uint16_t i = 0; i < size; i++) rx_put(buf[i]);
        // BLE terminal apps do not send a newline: every write ends a line
        if (size && buf[size - 1] != '\n' && buf[size - 1] != '\r') rx_put('\n');
        return 0;
    }
    return 0;
}

static void packet_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            bd_addr_t null_addr = {0};
            gap_advertisements_set_params(0x0030, 0x0030, 0, 0, null_addr, 0x07, 0x00);
            gap_advertisements_set_data(sizeof adv_data, (uint8_t *)adv_data);
            gap_advertisements_enable(1);
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        con = HCI_CON_HANDLE_INVALID;
        notify_on = false;
        tx_head = tx_tail = 0;
        ble_in.len = 0;
        ble_in.escape = false;
        break;
    case ATT_EVENT_CONNECTED:
        con = att_event_connected_get_handle(packet);
        break;
    case ATT_EVENT_CAN_SEND_NOW:
        send_some();
        break;
    default:
        break;
    }
}

void ble_init(void) {
    ble_in.sink = ble_sink;
    ble_in.trusted = true;      // Command needs an encrypted link (WRITE_ENCRYPTED in buscraft.gatt)
    l2cap_init();
    sm_init();
    // LE Secure Connections, "just works": no display or keypad on the adapter. The link is encrypted with an ECDH key,
    // so a passive listener cannot read the Wi-Fi password; bonding keeps the keys in BTstack's flash bank.
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);
    sm_set_secure_connections_only_mode(true);
    att_server_init(profile_data, att_read, att_write);
    hci_cb.callback = &packet_handler;
    hci_add_event_handler(&hci_cb);
    att_server_register_packet_handler(packet_handler);
    hci_power_control(HCI_POWER_ON);
}

void ble_poll(void) {
    while (rx_tail != rx_head) {
        uint8_t c = rx[rx_tail];
        rx_tail = (rx_tail + 1) % sizeof rx;
        komento_feed(&ble_in, c);
    }
}
