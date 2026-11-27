// Wi-Fi station (cyw43, poll mode) and the TCP line socket. Received bytes are queued in the lwIP callback and
// handled from the main loop, so GPIB transfers never run inside lwIP.
#include "net.h"

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "komento.h"
#include "lwip/apps/mdns.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#define HOSTNAME "buscraft"
// Mains/USB powered: radio always awake for low latency. Battery builds turn power saving on (-DBUSCRAFT_WIFI_POWERSAVE=ON).
#if BUSCRAFT_WIFI_POWERSAVE
#define WIFI_PM CYW43_PERFORMANCE_PM
#else
#define WIFI_PM CYW43_NONE_PM
#endif
#define CONFIG_MAGIC 0x42435731u   // "BCW1"
#define CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct {
    uint32_t magic;
    char ssid[33];
    char pass[64];
} config_t;

static config_t cfg;
static bool radio_up, connecting;
static int last_link = CYW43_LINK_DOWN;
static uint32_t retry_at;
static struct tcp_pcb *listener, *client;

// Bytes from the client, filled in the lwIP receive callback and drained by net_poll().
static uint8_t rx[1024];
static volatile size_t rx_head, rx_tail;
static komento_input_t tcp_in;

static void load_config(void) {
    memcpy(&cfg, (const void *)(XIP_BASE + CONFIG_OFFSET), sizeof cfg);
    if (cfg.magic != CONFIG_MAGIC) memset(&cfg, 0, sizeof cfg);
    cfg.ssid[sizeof cfg.ssid - 1] = 0;
    cfg.pass[sizeof cfg.pass - 1] = 0;
}

static void write_config(void *param) {
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof page);
    memcpy(page, param, sizeof(config_t));
    flash_range_erase(CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_OFFSET, page, sizeof page);
}

// ---- TCP socket ----

static void tcp_sink(const void *data, size_t n) {
    const uint8_t *p = data;
    absolute_time_t deadline = make_timeout_time_ms(2000);
    while (n && client) {
        size_t room = tcp_sndbuf(client);
        if (room == 0) {                     // wait for acknowledgements to free the send buffer
            if (time_reached(deadline)) return;
            cyw43_arch_poll();
            continue;
        }
        size_t k = n < room ? n : room;
        if (tcp_write(client, p, (u16_t)k, TCP_WRITE_FLAG_COPY) != ERR_OK) return;
        p += k;
        n -= k;
    }
    if (client) tcp_output(client);
}

static void drop_client(void) {
    if (!client) return;
    tcp_arg(client, NULL);
    tcp_recv(client, NULL);
    tcp_err(client, NULL);
    if (tcp_close(client) != ERR_OK) tcp_abort(client);
    client = NULL;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (!p) {                               // remote closed
        drop_client();
        return ERR_OK;
    }
    for (struct pbuf *q = p; q; q = q->next) {
        const uint8_t *d = q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            size_t next = (rx_head + 1) % sizeof rx;
            if (next == rx_tail) break;     // full: drop the rest, the host is far ahead of the bus
            rx[rx_head] = d[i];
            rx_head = next;
        }
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void on_err(void *arg, err_t err) { client = NULL; }

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    if (err != ERR_OK || !pcb) return ERR_VAL;
    drop_client();                          // one client at a time; the newest wins
    client = pcb;
    rx_head = rx_tail = 0;
    tcp_in.len = 0;
    tcp_in.escape = false;
    tcp_nagle_disable(pcb);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_err);
    return ERR_OK;
}

static void start_server(void) {
    if (listener) return;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb || tcp_bind(pcb, IP_ANY_TYPE, NET_PORT) != ERR_OK) return;
    listener = tcp_listen_with_backlog(pcb, 1);
    tcp_accept(listener, on_accept);
}

// ---- Wi-Fi ----

static void connect(void) {
    if (!cfg.ssid[0]) return;
    if (!radio_up) {
        if (cyw43_arch_init()) return;
        cyw43_arch_enable_sta_mode();
        netif_set_hostname(netif_default, HOSTNAME);
        mdns_resp_init();
        mdns_resp_add_netif(netif_default, HOSTNAME);
        mdns_resp_add_service(netif_default, HOSTNAME, "_scpi-raw", DNSSD_PROTO_TCP, NET_PORT, NULL, NULL);
        radio_up = true;
    }
    cyw43_arch_wifi_connect_async(cfg.ssid, cfg.pass[0] ? cfg.pass : NULL,
                                  cfg.pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
    connecting = true;
}

void net_init(void) {
    tcp_in.sink = tcp_sink;
    load_config();
    connect();
}

void net_poll(void) {
    if (!radio_up) return;
    cyw43_arch_poll();
    int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link != last_link) {
        last_link = link;
        if (link == CYW43_LINK_UP) {
            cyw43_wifi_pm(&cyw43_state, WIFI_PM);
            start_server();
            mdns_resp_announce(netif_default);
        }
    }
    if (connecting && (link == CYW43_LINK_FAIL || link == CYW43_LINK_NONET || link == CYW43_LINK_BADAUTH)) {
        connecting = false;
        retry_at = to_ms_since_boot(get_absolute_time()) + 10000;
    }
    if (!connecting && link != CYW43_LINK_UP && cfg.ssid[0] &&
        (int32_t)(to_ms_since_boot(get_absolute_time()) - retry_at) >= 0) {
        connect();
    }
    while (rx_tail != rx_head) {
        uint8_t c = rx[rx_tail];
        rx_tail = (rx_tail + 1) % sizeof rx;
        komento_feed(&tcp_in, c);
    }
}

bool net_set_wifi(const char *ssid, const char *pass) {
    config_t n = { .magic = CONFIG_MAGIC };
    strncpy(n.ssid, ssid, sizeof n.ssid - 1);
    strncpy(n.pass, pass, sizeof n.pass - 1);
    if (flash_safe_execute(write_config, &n, 1000) != PICO_OK) return false;
    cfg = n;
    if (radio_up) {
        drop_client();
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        last_link = CYW43_LINK_DOWN;
    }
    connecting = false;
    connect();
    return true;
}

void net_status(char *buf, size_t n) {
    if (!cfg.ssid[0]) { snprintf(buf, n, "wifi off\n"); return; }
    int link = radio_up ? cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) : CYW43_LINK_DOWN;
    const char *s = link == CYW43_LINK_UP ? "up" : link == CYW43_LINK_JOIN ? "joining" : link == CYW43_LINK_NOIP ? "no address yet"
                  : link == CYW43_LINK_BADAUTH ? "wrong password" : link == CYW43_LINK_NONET ? "network not found"
                  : link == CYW43_LINK_FAIL ? "failed" : "down";
    if (link == CYW43_LINK_UP)
        snprintf(buf, n, "wifi %s: up, %s port %d (%s.local)%s\n", cfg.ssid, ip4addr_ntoa(netif_ip4_addr(netif_default)),
                 NET_PORT, HOSTNAME, client ? ", client connected" : "");
    else
        snprintf(buf, n, "wifi %s: %s\n", cfg.ssid, s);
}
