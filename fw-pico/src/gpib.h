// GPIB (IEEE-488) controller-in-charge on plain GPIOs.
// All bus lines are driven open-drain: asserted = pulled low, released = input with pull-up.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Pico GPIO numbers. DIO1..DIO8 are contiguous so the data byte is one masked read/write.
#define GPIB_DIO1 0   // DIO1..DIO8 = GP0..GP7
#define GPIB_DAV  8
#define GPIB_NRFD 9
#define GPIB_NDAC 10
#define GPIB_EOI  11
#define GPIB_ATN  12
#define GPIB_IFC  13
#define GPIB_SRQ  14
#define GPIB_REN  15

// Multiline (ATN) commands
#define GPIB_GTL 0x01
#define GPIB_SDC 0x04
#define GPIB_GET 0x08
#define GPIB_LLO 0x11
#define GPIB_DCL 0x14
#define GPIB_SPE 0x18
#define GPIB_SPD 0x19
#define GPIB_MLA(a) (0x20 | ((a) & 0x1f))
#define GPIB_MTA(a) (0x40 | ((a) & 0x1f))
#define GPIB_UNL 0x3f
#define GPIB_UNT 0x5f

typedef enum {
    GPIB_OK = 0,
    GPIB_ERR_TIMEOUT = -1,
    GPIB_ERR_NO_LISTENER = -2,
} gpib_err_t;

// Read stops on EOI, on the terminator character (if >= 0) or when the buffer is full.
typedef struct {
    size_t len;
    bool eoi;        // last byte carried EOI
    bool term;       // stopped on the terminator character
} gpib_read_t;

void gpib_init(uint8_t own_addr);
void gpib_set_timeout_ms(uint32_t ms);
uint32_t gpib_timeout_ms(void);

void gpib_ifc(void);                 // interface clear pulse (>100 us)
void gpib_ren(bool on);
bool gpib_srq(void);                 // true when some device requests service

gpib_err_t gpib_command(const uint8_t *cmd, size_t n);      // bytes with ATN asserted
gpib_err_t gpib_write(uint8_t addr, const uint8_t *data, size_t n, bool eoi_last);
gpib_err_t gpib_read(uint8_t addr, uint8_t *buf, size_t max, int term, gpib_read_t *res);
gpib_err_t gpib_spoll(uint8_t addr, uint8_t *status);
gpib_err_t gpib_addressed(uint8_t addr, uint8_t cmd);      // UNL, MLA addr, cmd, UNL (SDC, GET, GTL)

// Diagnostics
uint32_t gpib_lines(void);           // bit per GPIO (GP0..GP15), 1 = line asserted (low)
uint32_t gpib_selftest(int *bad);    // asserts each line alone; returns the mask of other lines that followed it,
                                     // *bad = GPIO of the first faulty line or -1
