/* Renders the firmware's screens on the host: src/logic.c ui_lines() -> src/oled.c -> a model of the panel RAM.
 * Prints each screen as "name" then 2 lines of 96 hex bytes (page 0, page 1; bit 0 = top row of the page).
 * tools/screens.py turns this into PNGs; ../tools/oled_3d.py uses one frame for the board's 3D model. */
#include <stdio.h>
#include <string.h>
#include "oled.c"
#include "logic.h"

static uint8_t ram[2][128];
static uint8_t c0, c1 = 127, p0, p1 = 1, col, page;

static uint8_t xfer(uint8_t ctrl, const uint8_t *b, uint8_t n)
{
    uint8_t i;
    if (ctrl == 0x00) {
        for (i = 0; i < n; i++) {
            if (b[i] == 0x21 && i + 2 < n) { c0 = b[i + 1]; c1 = b[i + 2]; col = c0; i += 2; }
            else if (b[i] == 0x22 && i + 2 < n) { p0 = b[i + 1]; p1 = b[i + 2]; page = p0; i += 2; }
            else if (b[i] == 0x20 || b[i] == 0xD5 || b[i] == 0xA8 || b[i] == 0xD3 || b[i] == 0x8D || b[i] == 0xDA ||
                     b[i] == 0x81 || b[i] == 0xD9 || b[i] == 0xDB) i++;
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
uint8_t df_read(uint8_t off) { (void)off; return 0xFF; }
void df_write(uint8_t off, uint8_t v) { (void)off; (void)v; }

static void shot(const char *name, uint8_t vidx, uint8_t status, uint8_t on, uint8_t ina, uint16_t max_ma,
                 uint16_t mv, int16_t ma, int16_t t_dc)
{
    struct ui u;
    char r0[17], r1[17];
    int p, c;
    u.vidx = vidx; u.status = status; u.out_on = on; u.ina_ok = ina; u.max_ma = max_ma;
    u.mv = mv; u.ma = ma; u.t_dc = t_dc;
    ui_lines(r0, r1, &u);
    oled_clear();
    oled_text(0, 0, r0);
    oled_text(1, 0, r1);
    oled_flush();
    printf("%s|%s|%s\n", name, r0, r1);
    for (p = 0; p < 2; p++) {
        for (c = 0; c < 96; c++) printf("%02x", ram[p][c]);
        printf("\n");
    }
}

int main(void)
{
    oled_init();
    shot("on",          5, ST_OK,          1, 1, 5000, 20012, 3217, 452);
    shot("off",         3, ST_OK,          0, 1, 3000, 15000,    0, 318);
    shot("no-pd",       0, ST_NOPD,        0, 1,    0,  5050,    0, 254);
    shot("hot",         5, ST_HOT,         0, 1, 5000, 20000,    0, 872);
    shot("overcurrent", 1, ST_OVERCURRENT, 0, 1, 3000,  9000,    0, 391);
    return 0;
}
