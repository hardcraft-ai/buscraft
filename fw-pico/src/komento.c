// "++" command interpreter (controller mode). Replies are text lines ending in "\n".
#include "komento.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpib.h"
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

static void reply(const char *s) { fputs(s, stdout); fflush(stdout); }

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
        if (r.len) fwrite(buf, 1, r.len, stdout);
        if (err != GPIB_OK) {
            if (first || r.len == 0) error(err);
            break;
        }
        first = false;
    } while (!r.eoi && !r.term && r.len == sizeof buf);
    if (r.eoi && st.eot) putchar(st.eot_char);
    fflush(stdout);
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

static void help(void) {
    reply("++addr [0-30]  ++auto [0|1]  ++eoi [0|1]  ++eos [0-3]  ++eot_enable [0|1]  ++eot_char [n]\n"
          "++read [eoi|n]  ++read_tmo_ms [ms]  ++spoll [addr]  ++srq  ++clr  ++trg  ++loc  ++llo  ++ifc\n"
          "++ren [0|1]  ++mode  ++ver  ++rst  ++help\n");
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
    } else if (is_cmd(line, "help", &a)) {
        help();
    } else {
        reply("error: unknown command, ++help lists them\n");
    }
}

void komento_init(void) {
    gpib_init(OWN_ADDR);
}

void komento_line(const char *line, size_t len) {
    if (len >= 2 && line[0] == '+' && line[1] == '+') plus_command(line + 2);
    else if (len) do_write(line, len);
}
