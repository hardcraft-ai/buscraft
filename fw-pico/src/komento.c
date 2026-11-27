// "++" command interpreter (controller mode). Replies are text lines ending in "\n".
#include "komento.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpib.h"
#include "net.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#define OWN_ADDR 0

static struct {
    uint8_t addr;       // instrument primary address
    bool autoread;      // read the reply after every write
    bool eoi;           // assert EOI with the last byte written
    uint8_t eos;        // 0 CR+LF, 1 CR, 2 LF, 3 nothing appended to writes
    bool eot;           // append eot_char to output when EOI was seen
    char eot_char;
} st = { .addr = 1, .autoread = false, .eoi = true, .eos = 0, .eot = false, .eot_char = '\n' };

static komento_sink_t sink;   // replies go to the stream whose line is being handled
static bool trusted;          // the stream may change Wi-Fi settings

static void out(const void *p, size_t n) { sink(p, n); }
static void reply(const char *s) { out(s, strlen(s)); }

static void error(gpib_err_t e) {
    reply(e == GPIB_ERR_NO_LISTENER ? "error: no listener\n" : "error: timeout\n");
}

static void reply_int(int v) {
    char b[16];
    snprintf(b, sizeof b, "%d\n", v);
    reply(b);
}

// Reads until EOI (or until the terminator character, when term >= 0) and passes the bytes to the host.
static void do_read(int term) {
    static uint8_t buf[256];
    gpib_read_t r;
    gpib_err_t err;
    bool first = true;
    do {
        // Long replies come in buffer-sized pieces; the instrument stays addressed as talker between them.
        err = gpib_read(st.addr, buf, sizeof buf, term, &r);
        if (r.len) out(buf, r.len);
        if (err != GPIB_OK) {
            if (first || r.len == 0) error(err);
            break;
        }
        first = false;
    } while (!r.eoi && !r.term && r.len == sizeof buf);
    if (r.eoi && st.eot) out(&st.eot_char, 1);
}

static void do_write(const char *data, size_t n) {
    static uint8_t buf[512];
    if (n > sizeof buf - 2) n = sizeof buf - 2;
    memcpy(buf, data, n);
    if (st.eos == 0 || st.eos == 1) buf[n++] = '\r';
    if (st.eos == 0 || st.eos == 2) buf[n++] = '\n';
    gpib_err_t err = gpib_write(st.addr, buf, n, st.eoi);
    if (err != GPIB_OK) { error(err); return; }
    if (st.autoread) do_read(-1);
}

// "++name [arg]": returns true when the command name matches and puts the argument (or NULL) in *arg.
static bool is_cmd(const char *line, const char *name, const char **arg) {
    size_t n = strlen(name);
    if (strncmp(line, name, n) != 0 || (line[n] != 0 && line[n] != ' ')) return false;
    const char *a = line + n;
    while (*a == ' ') a++;
    *arg = *a ? a : NULL;
    return true;
}

static void set_or_show_bool(bool *v, const char *arg) {
    if (arg) *v = atoi(arg) != 0; else reply_int(*v);
}

static const char *const LINE_NAMES[16] = {
    "DIO1", "DIO2", "DIO3", "DIO4", "DIO5", "DIO6", "DIO7", "DIO8",
    "DAV", "NRFD", "NDAC", "EOI", "ATN", "IFC", "SRQ", "REN",
};

// One line: every bus signal with its state, asserted lines in upper case (e.g. "dio1 ... NDAC ... REN").
static void print_lines(uint32_t m) {
    char b[128];
    size_t n = 0;
    for (int i = 0; i < 16; i++) {
        for (const char *s = LINE_NAMES[i]; *s; s++)
            b[n++] = (m & (1u << i)) ? *s : (char)(*s >= 'A' && *s <= 'Z' ? *s + 32 : *s);
        b[n++] = i == 15 ? '\n' : ' ';
    }
    b[n] = 0;
    reply(b);
}

static void selftest(void) {
    int bad;
    uint32_t m = gpib_selftest(&bad);
    if (bad < 0) { reply("selftest ok: no shorts between lines, none stuck low\n"); return; }
    char b[64];
    if (m & (1u << bad)) snprintf(b, sizeof b, "selftest: %s stuck low, all asserted lines:\n", LINE_NAMES[bad]);
    else snprintf(b, sizeof b, "selftest: %s drags other lines along:\n", LINE_NAMES[bad]);
    reply(b);
    print_lines(m);
}

static void help(void) {
    reply("++addr [0-30]  ++auto [0|1]  ++eoi [0|1]  ++eos [0-3]  ++eot_enable [0|1]  ++eot_char [n]\n"
          "++read [eoi|n]  ++read_tmo_ms [ms]  ++spoll [addr]  ++srq  ++clr  ++trg  ++loc  ++llo  ++ifc\n"
          "++ren [0|1]  ++mode  ++ver  ++rst  ++help\n"
          "++wifi [ssid password | off]  ++led [0|1]  ++lines (bus state, asserted in CAPITALS)  ++selftest (shorts, lines stuck low; run without instrument)\n");
}

static void plus_command(const char *line) {
    const char *a;
    if (is_cmd(line, "addr", &a)) {
        if (!a) { reply_int(st.addr); return; }
        int v = atoi(a);
        if (v < 0 || v > 30) reply("error: address 0-30\n"); else st.addr = (uint8_t)v;
    } else if (is_cmd(line, "auto", &a)) {
        set_or_show_bool(&st.autoread, a);
    } else if (is_cmd(line, "eoi", &a)) {
        set_or_show_bool(&st.eoi, a);
    } else if (is_cmd(line, "eos", &a)) {
        if (!a) { reply_int(st.eos); return; }
        int v = atoi(a);
        if (v < 0 || v > 3) reply("error: eos 0-3\n"); else st.eos = (uint8_t)v;
    } else if (is_cmd(line, "eot_enable", &a)) {
        set_or_show_bool(&st.eot, a);
    } else if (is_cmd(line, "eot_char", &a)) {
        if (a) st.eot_char = (char)atoi(a); else reply_int(st.eot_char);
    } else if (is_cmd(line, "read", &a)) {
        do_read(a && strcmp(a, "eoi") != 0 ? atoi(a) : -1);
    } else if (is_cmd(line, "read_tmo_ms", &a)) {
        if (a) gpib_set_timeout_ms((uint32_t)atoi(a)); else reply_int((int)gpib_timeout_ms());
    } else if (is_cmd(line, "spoll", &a)) {
        uint8_t status;
        gpib_err_t err = gpib_spoll(a ? (uint8_t)atoi(a) : st.addr, &status);
        if (err != GPIB_OK) error(err); else reply_int(status);
    } else if (is_cmd(line, "srq", &a)) {
        reply_int(gpib_srq());
    } else if (is_cmd(line, "clr", &a)) {
        gpib_err_t err = gpib_addressed(st.addr, GPIB_SDC);
        if (err != GPIB_OK) error(err);
    } else if (is_cmd(line, "trg", &a)) {
        gpib_err_t err = gpib_addressed(st.addr, GPIB_GET);
        if (err != GPIB_OK) error(err);
    } else if (is_cmd(line, "loc", &a)) {
        gpib_err_t err = gpib_addressed(st.addr, GPIB_GTL);
        if (err != GPIB_OK) error(err);
    } else if (is_cmd(line, "llo", &a)) {
        const uint8_t c = GPIB_LLO;
        gpib_err_t err = gpib_command(&c, 1);
        if (err != GPIB_OK) error(err);
    } else if (is_cmd(line, "ifc", &a)) {
        gpib_ifc();
    } else if (is_cmd(line, "ren", &a)) {
        if (a) gpib_ren(atoi(a) != 0);
    } else if (is_cmd(line, "mode", &a)) {
        reply("1\n");  // controller only
    } else if (is_cmd(line, "ver", &a)) {
        reply("BusCraft GPIB adapter, fw-pico " BUSCRAFT_VERSION "\n");
    } else if (is_cmd(line, "rst", &a)) {
        watchdog_reboot(0, 0, 10);
    } else if (is_cmd(line, "wifi", &a)) {
        char b[96];
        if (a && !trusted) {
            reply("error: Wi-Fi settings can only be changed over USB or Bluetooth\n");
            return;
        }
        if (a) {
            // "++wifi off" forgets the network; otherwise the first word is the name, the rest the password
            char ssid[33] = "", pass[64] = "";
            if (strcmp(a, "off") != 0) {
                // ssid is the first word, or a quoted name when it contains spaces: ++wifi "My Net" password
                const char *s = a, *e;
                if (*s == '"') { s++; e = strchr(s, '"'); if (!e) e = s + strlen(s); }
                else { e = strchr(s, ' '); if (!e) e = s + strlen(s); }
                size_t n = (size_t)(e - s);
                if (n >= sizeof ssid) n = sizeof ssid - 1;
                memcpy(ssid, s, n);
                const char *p = *e == '"' ? e + 1 : e;
                while (*p == ' ') p++;
                strncpy(pass, p, sizeof pass - 1);
            }
            bool ok = net_set_wifi(ssid, pass);
            memset(pass, 0, sizeof pass);
            if (!ok) { reply("error: could not store the network settings\n"); return; }
        }
        net_status(b, sizeof b);
        reply(b);
    } else if (is_cmd(line, "led", &a)) {
        if (a) net_led(atoi(a) != 0); else reply_int(net_led_state());
    } else if (is_cmd(line, "lines", &a)) {
        print_lines(gpib_lines());
    } else if (is_cmd(line, "selftest", &a)) {
        selftest();
    } else if (is_cmd(line, "help", &a)) {
        help();
    } else {
        reply("error: unknown command, ++help lists them\n");
    }
}

void komento_init(void) {
    gpib_init(OWN_ADDR);
}

static void komento_line(const char *line, size_t len) {
    if (len >= 2 && line[0] == '+' && line[1] == '+') plus_command(line + 2);
    else if (len) do_write(line, len);
}

void komento_feed(komento_input_t *in, int c) {
    if (in->escape) {
        in->escape = false;
    } else if (c == 27) {
        in->escape = true;
        return;
    } else if (c == '\r' || c == '\n') {
        in->line[in->len] = 0;
        sink = in->sink;
        trusted = in->trusted;
        komento_line(in->line, in->len);
        memset(in->line, 0, in->len);   // the line may have carried a password
        in->len = 0;
        return;
    }
    if (in->len < sizeof in->line - 1) in->line[in->len++] = (char)c;
}
