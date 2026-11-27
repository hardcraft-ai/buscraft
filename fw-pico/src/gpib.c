// GPIB controller-in-charge: source and acceptor handshakes on open-drain GPIOs.
#include "gpib.h"

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#define DIO_MASK (0xffu << GPIB_DIO1)
#define ALL_MASK (DIO_MASK | (1u << GPIB_DAV) | (1u << GPIB_NRFD) | (1u << GPIB_NDAC) | (1u << GPIB_EOI) | \
                  (1u << GPIB_ATN) | (1u << GPIB_IFC) | (1u << GPIB_SRQ) | (1u << GPIB_REN))

static uint8_t own;
static uint32_t timeout_us = 1200 * 1000;

// Open drain: the output latch stays 0, direction selects asserted (out, low) or released (in, pulled up).
static inline void assert_line(uint pin) { gpio_set_dir(pin, GPIO_OUT); }
static inline void release_line(uint pin) { gpio_set_dir(pin, GPIO_IN); }
static inline bool asserted(uint pin) { return !gpio_get(pin); }

static bool wait_line(uint pin, bool want_asserted, uint64_t deadline) {
    while (asserted(pin) != want_asserted) {
        if (time_us_64() > deadline) return false;
        watchdog_update();
    }
    return true;
}

void gpib_init(uint8_t own_addr) {
    own = own_addr & 0x1f;
    gpio_init_mask(ALL_MASK);
    gpio_clr_mask(ALL_MASK);
    gpio_set_dir_in_masked(ALL_MASK);
    for (uint pin = 0; pin < 32; pin++) {
        if (ALL_MASK & (1u << pin)) gpio_pull_up(pin);
    }
    gpib_ifc();
    gpib_ren(true);
}

void gpib_set_timeout_ms(uint32_t ms) { timeout_us = ms * 1000; }
uint32_t gpib_timeout_ms(void) { return timeout_us / 1000; }

void gpib_ifc(void) {
    release_line(GPIB_ATN);
    assert_line(GPIB_IFC);
    sleep_us(150);
    release_line(GPIB_IFC);
}

void gpib_ren(bool on) {
    if (on) assert_line(GPIB_REN); else release_line(GPIB_REN);
}

bool gpib_srq(void) { return asserted(GPIB_SRQ); }

// Source handshake for one byte. Data is negative logic: bit 1 = line asserted (low).
static gpib_err_t send_byte(uint8_t b, bool eoi) {
    uint64_t deadline = time_us_64() + timeout_us;
    // Acceptors pull NDAC low as soon as they are present; both lines high means nobody listens.
    if (!asserted(GPIB_NRFD) && !asserted(GPIB_NDAC)) {
        sleep_us(2);
        if (!asserted(GPIB_NRFD) && !asserted(GPIB_NDAC)) return GPIB_ERR_NO_LISTENER;
    }
    if (!wait_line(GPIB_NRFD, false, deadline)) return GPIB_ERR_TIMEOUT;
    gpio_set_dir_masked(DIO_MASK, (uint32_t)b << GPIB_DIO1);
    if (eoi) assert_line(GPIB_EOI);
    sleep_us(2);  // data settling before DAV
    assert_line(GPIB_DAV);
    bool ok = wait_line(GPIB_NDAC, false, deadline);
    release_line(GPIB_DAV);
    gpio_set_dir_in_masked(DIO_MASK);
    release_line(GPIB_EOI);
    return ok ? GPIB_OK : GPIB_ERR_TIMEOUT;
}

// Acceptor handshake for one byte. NRFD and NDAC are asserted on entry and on return.
static gpib_err_t recv_byte(uint8_t *b, bool *eoi) {
    uint64_t deadline = time_us_64() + timeout_us;
    release_line(GPIB_NRFD);                              // ready for data
    if (!wait_line(GPIB_DAV, true, deadline)) {
        assert_line(GPIB_NRFD);
        return GPIB_ERR_TIMEOUT;
    }
    assert_line(GPIB_NRFD);                               // busy
    *b = (uint8_t)~(gpio_get_all() >> GPIB_DIO1);
    *eoi = asserted(GPIB_EOI);
    release_line(GPIB_NDAC);                              // data accepted
    bool ok = wait_line(GPIB_DAV, false, deadline);
    assert_line(GPIB_NDAC);
    return ok ? GPIB_OK : GPIB_ERR_TIMEOUT;
}

static void stop_listening(void) {
    release_line(GPIB_NRFD);
    release_line(GPIB_NDAC);
}

gpib_err_t gpib_command(const uint8_t *cmd, size_t n) {
    stop_listening();
    assert_line(GPIB_ATN);
    sleep_us(2);
    gpib_err_t err = GPIB_OK;
    for (size_t i = 0; i < n && err == GPIB_OK; i++) err = send_byte(cmd[i], false);
    release_line(GPIB_ATN);
    return err;
}

gpib_err_t gpib_write(uint8_t addr, const uint8_t *data, size_t n, bool eoi_last) {
    const uint8_t cmd[] = { GPIB_UNL, GPIB_MTA(own), GPIB_MLA(addr) };
    gpib_err_t err = gpib_command(cmd, sizeof cmd);
    for (size_t i = 0; i < n && err == GPIB_OK; i++) err = send_byte(data[i], eoi_last && i == n - 1);
    return err;
}

gpib_err_t gpib_read(uint8_t addr, uint8_t *buf, size_t max, int term, gpib_read_t *res) {
    res->len = 0;
    res->eoi = res->term = false;
    const uint8_t cmd[] = { GPIB_UNL, GPIB_MLA(own), GPIB_MTA(addr) };
    gpib_err_t err = gpib_command(cmd, sizeof cmd);
    if (err != GPIB_OK) return err;
    assert_line(GPIB_NRFD);
    assert_line(GPIB_NDAC);
    while (res->len < max) {
        uint8_t b;
        bool eoi;
        err = recv_byte(&b, &eoi);
        if (err != GPIB_OK) break;
        buf[res->len++] = b;
        if (eoi) { res->eoi = true; break; }
        if (term >= 0 && b == (uint8_t)term) { res->term = true; break; }
    }
    const uint8_t untalk[] = { GPIB_UNT, GPIB_UNL };
    gpib_err_t err2 = gpib_command(untalk, sizeof untalk);
    return err != GPIB_OK ? err : err2;
}

gpib_err_t gpib_spoll(uint8_t addr, uint8_t *status) {
    const uint8_t cmd[] = { GPIB_UNL, GPIB_MLA(own), GPIB_SPE, GPIB_MTA(addr) };
    gpib_err_t err = gpib_command(cmd, sizeof cmd);
    if (err == GPIB_OK) {
        bool eoi;
        assert_line(GPIB_NRFD);
        assert_line(GPIB_NDAC);
        err = recv_byte(status, &eoi);
    }
    const uint8_t end[] = { GPIB_SPD, GPIB_UNT };
    gpib_err_t err2 = gpib_command(end, sizeof end);
    return err != GPIB_OK ? err : err2;
}

gpib_err_t gpib_addressed(uint8_t addr, uint8_t cmd_byte) {
    const uint8_t cmd[] = { GPIB_UNL, GPIB_MLA(addr), cmd_byte, GPIB_UNL };
    return gpib_command(cmd, sizeof cmd);
}

uint32_t gpib_lines(void) { return ~gpio_get_all() & ALL_MASK; }

static uint32_t selftest_lines(int *bad) {
    *bad = -1;
    gpio_set_dir_in_masked(ALL_MASK);   // release everything, including REN
    sleep_us(50);
    uint32_t idle = gpib_lines();
    if (idle) {                         // a line stuck low: shorted to ground or held by an instrument
        *bad = __builtin_ctz(idle);
        return idle;
    }
    for (uint pin = 0; pin < 32; pin++) {
        if (!(ALL_MASK & (1u << pin))) continue;
        assert_line(pin);
        sleep_us(20);
        uint32_t others = gpib_lines() & ~(1u << pin);
        release_line(pin);
        sleep_us(20);
        if (others) {
            *bad = (int)pin;
            return others;
        }
    }
    return 0;
}

uint32_t gpib_selftest(int *bad) {
    uint32_t m = selftest_lines(bad);
    gpib_ren(true);
    return m;
}
