/* Renders the firmware's screens on the host: src/logic.c ui_lines() -> src/oled.c -> a model of the panel RAM.
 * Prints each screen as "name|row0|row1" then 2 lines of 96 hex bytes (page 0,
 * page 1; bit 0 = top row of the page). tools/screens.py turns this into PNGs. */
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

static void shot(const char *name, const struct ui *u)
{
    char r0[17], r1[17];
    int p, c;
    ui_lines(r0, r1, u);
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
    struct ui u;
    oled_init();
    memset(&u, 0, sizeof u);
    u.set_mv = 20000; u.lim_ma = 3250; u.out_on = 1; u.mv = 20012; u.ma = 3217; u.t_dc = 452;
    shot("on", &u);
    u.set_mv = 15000; u.lim_ma = 3000; u.out_on = 0; u.ma = 0; u.t_dc = 318;
    shot("off", &u);
    memset(&u, 0, sizeof u);
    u.pps = 1; u.set_mv = 12300; u.lim_ma = 3000; u.out_on = 1; u.show_set = 1; u.mv = 12240; u.ma = 2950; u.t_dc = 512;
    shot("pps-set-v", &u);
    u.show_set = 0;
    shot("pps-on", &u);
    u.edit_i = 1; u.show_set = 1; u.lim_ma = 3250;
    shot("pps-set-i", &u);
    u.out_on = 0; u.status = ST_UVP;
    shot("pps-uvp", &u);
    memset(&u, 0, sizeof u);
    u.legacy = 1; u.set_mv = 5000; u.t_dc = 254;
    shot("no-pd", &u);
    memset(&u, 0, sizeof u);
    u.set_mv = 20000; u.lim_ma = 5000; u.status = ST_HOT; u.t_dc = 872;
    shot("hot", &u);
    u.set_mv = 9000; u.lim_ma = 3000; u.status = ST_OCP; u.t_dc = 391;
    shot("overcurrent", &u);
    return 0;
}
