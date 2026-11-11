/* Host test for src/oled.c's partial flush: a model of the SSD1306 RAM (horizontal addressing, 0x21 / 0x22 windows)
 * must end up holding exactly the framebuffer. `make test` compiles this with a copy of src/oled.c in build/host/,
 * included here so the test can read its framebuffer; its #include "hw.h" then resolves to test/shim/hw.h. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "oled.c"

static uint8_t ram[2][128];
static uint8_t c0, c1 = 127, p0, p1 = 1, col, page;
static long bytes, fail_after = -1;     /* I2C bytes on the wire (address + control + payload) */
static int fails;

static uint8_t xfer(uint8_t ctrl, const uint8_t *b, uint8_t n)
{
    uint8_t i;
    bytes += 2 + n;
    if (fail_after >= 0 && bytes > fail_after) return 1;   /* NACK mid-way: nothing more lands */
    if (ctrl == 0x00) {
        for (i = 0; i < n; i++) {
            if (b[i] == 0x21 && i + 2 < n) { c0 = b[i + 1]; c1 = b[i + 2]; col = c0; i += 2; }
            else if (b[i] == 0x22 && i + 2 < n) { p0 = b[i + 1]; p1 = b[i + 2]; page = p0; i += 2; }
            else if (b[i] == 0x20 || b[i] == 0xD5 || b[i] == 0xA8 || b[i] == 0xD3 || b[i] == 0x8D || b[i] == 0xDA ||
                     b[i] == 0x81 || b[i] == 0xD9 || b[i] == 0xDB) i++;      /* two-byte commands */
        }
    } else {
        for (i = 0; i < n; i++) {
            ram[page & 1][col] = b[i];
            if (col == c1) { col = c0; page = (uint8_t)(page == p1 ? p0 : page + 1); } else col++;
        }
    }
    return 0;
}
uint8_t i2c_write_buf(uint8_t a, uint8_t ctrl, const uint8_t *b, uint8_t n) { (void)a; return xfer(ctrl, b, n); }
uint8_t i2c_write_code(uint8_t a, uint8_t ctrl, const uint8_t *b, uint8_t n) { (void)a; return xfer(ctrl, b, n); }

static void screen(const char *r0, const char *r1)
{
    oled_clear();
    oled_text(0, 0, r0);
    oled_text(1, 0, r1);
}

/* the visible panel RAM (columns 0..95) must equal the framebuffer */
static void check(const char *what)
{
    if (memcmp(ram[0], fb, W) || memcmp(ram[1], fb + W, W)) { fails++; printf("FAIL %s: panel differs from fb\n", what); }
}

int main(void)
{
    long b;
    int i;
    memset(ram, 0x5A, sizeof ram);               /* random RAM at power-up */
    oled_init();
    check("after init");

    screen("20V  ON     45C", "3.21A    64.2W");
    oled_flush();
    check("first frame");

    b = bytes;
    screen("20V  ON     45C", "3.22A    64.4W");
    oled_flush();
    printf("oled: one-digit A and W change -> %ld bytes (full screen %d)\n", bytes - b, 2 * (2 + 6) + 2 * (2 + 96));
    check("digit change");

    b = bytes;
    oled_flush();
    if (bytes != b) { fails++; printf("FAIL unchanged frame sent %ld bytes\n", bytes - b); }

    b = bytes;
    screen("15V OFF     46C", "MAX 3.00A");
    oled_flush();
    printf("oled: voltage step (both rows change) -> %ld bytes\n", bytes - b);
    check("voltage step");

    /* random frames from the UI's own alphabet */
    srand(1);
    for (i = 0; i < 2000; i++) {
        static const char cs[] = " .-0123456789ACDEFHILMNOPRTVWX";
        char r0[17], r1[17];
        int k;
        for (k = 0; k < 16; k++) { r0[k] = cs[rand() % 30]; r1[k] = (rand() % 4) ? r1[k] : cs[rand() % 30]; }
        r0[rand() % 17] = 0; r1[16] = 0;
        if (i == 0) memset(r1, ' ', 16);
        screen(r0, r1);
        oled_flush();
        check("random frame");
        if (fails) break;
    }

    /* a NACK part-way through: the next flush must repair the panel */
    screen("12V  ON     50C", "1.00A    12.0W");
    fail_after = bytes + 30;
    oled_flush();
    fail_after = -1;
    oled_flush();
    check("after a failed transfer");

    printf(fails ? "%d OLED FAILURES\n" : "all oled tests passed\n", fails);
    return fails ? 1 : 0;
}
