/* display_port_dsi.c — the Elecrow CrowPanel Advanced 7" / 10.1" V1.2
 * (2026-10-15): a 1024 x 600 IPS panel (EK79007) on the ESP32-P4's 2-lane
 * MIPI-DSI, its own build (TANK_WIDE_1024).
 *
 * The DPI video stream reads a frame buffer out of PSRAM continuously (DW-GDMA,
 * no bounce buffers: the P4's PSRAM is 16-bit at 200 MHz). The driver keeps
 * three (NUM_FBS); draw_bitmap of one of them only writes it back from the
 * cache and makes it current, and the DMA picks the current one up when it
 * restarts at the end of the frame being sent - on_refresh_done. So a buffer
 * switched away from is free once one refresh_done has passed since the
 * switch (wait_free), as on the FNK0115's RGB panel (display_port_rgb.c,
 * whose shape this file follows). The tank renders straight into these
 * buffers (display_port_direct_fb); a flush of one is the switch. A frame
 * from anywhere else (a one-off page) is copied into one off the glass.
 * Turned 180, the frame is reversed in place. RGB565 in the CPU's byte order.
 *
 * Power: the D-PHY runs from the P4's internal LDO3 at 2.5 V (Elecrow's
 * examples also take LDO4 at 3.3 V before any peripheral; so does this). The
 * panel's reset (GPIO 41) is left alone, as Elecrow's driver leaves it.
 * The backlight is PWM (LEDC) on C_PIN_BL into the board's MT9201; the panel
 * has a sleep (DCS 0x10) but the board does not need it for the grace - the
 * backlight off is the dark, as on the FNK0115.
 *
 * The display owns the I2C bus here too: the GT911's, GPIO 45 / 46
 * (board_i2c_bus; the PMIC, IMU, RTC and codec probes find nothing there and
 * step aside). Pins, timings: board_pins.h, Elecrow's Lesson07 (V1.2). */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_ek79007.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

_Static_assert(TANK_W == C_PANEL_W && TANK_H == C_PANEL_H, "the CrowPanel's display port wants its own build (TANK_WIDE_1024): the frame is the panel");

static const char *TAG = "dsi";
#define DSI_LANES      2
#define DSI_LANE_MBPS  900                      /* Elecrow's verified values (Lesson07, V1.2) */
#define DPI_CLK_MHZ    51                       /* 1414 x 645 a frame with the porches: ~56 Hz */
#define BL_LEDC_TIMER  LEDC_TIMER_1
#define BL_LEDC_CH     LEDC_CHANNEL_1
#define BL_PWM_HZ      30000                    /* Elecrow's; above hearing */
#define BL_DUTY_BITS   LEDC_TIMER_11_BIT
#define BL_DUTY_FLOOR  200                      /* Elecrow's floor (of 2047): below it the MT9201 barely lights */
#define SWAP_WAIT_MS   100                      /* a frame is ~18 ms; longer = the stream stopped (asleep) */

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_dsi_bus_handle_t s_bus;
static esp_lcd_panel_io_handle_t s_dbi;
static i2c_master_bus_handle_t s_i2c;
#define NUM_FBS        3                        /* the tank renders the third while one is on the glass and one waits */
static uint16_t *s_fbs[NUM_FBS];
static int  s_shown = 0;                        /* the one last switched to (on the glass, or next) */
static volatile uint32_t s_fb_done;             /* DMA frame restarts since boot */
static uint32_t s_left_at[NUM_FBS];             /* s_fb_done when a switch left buffer i ... */
static bool s_leaving[NUM_FBS];                 /* ... and the DMA may still be reading it */
static SemaphoreHandle_t s_fb_sem;
static bool s_inverted;
static uint8_t s_brightness = 0xFF;
static bool s_lit;
static int64_t s_prof_wait, s_prof_send;

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_v2(void) { return false; }
bool board_is_round(void) { return false; }
bool board_is_watch(void) { return false; }
bool board_has_expander(void) { return false; }
int  board_pwr_sense_pin(void) { return -1; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FULL; }
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) { *tx = (float)px; *ty = (float)py; }
void display_port_set_inverted(bool inverted) { s_inverted = inverted; }
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) {
    *wait_us = s_prof_wait; *send_us = s_prof_send; s_prof_wait = s_prof_send = 0;
}

/* the DMA finished a frame and restarted on the current buffer */
static IRAM_ATTR bool on_refresh_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *ctx) {
    (void)panel; (void)edata; (void)ctx;
    s_fb_done++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_fb_sem, &woken);
    return woken == pdTRUE;
}

static void backlight_apply(void) {
    uint32_t duty = s_lit && s_brightness ? BL_DUTY_FLOOR + (uint32_t)s_brightness * (2047 - BL_DUTY_FLOOR) / 255 : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH);
}
static void backlight_init(void) {
    gpio_hold_dis(C_PIN_BL);                    /* a deep-sleep wake: the night held it low */
    const ledc_timer_config_t tc = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = BL_DUTY_BITS,
                                     .timer_num = BL_LEDC_TIMER, .freq_hz = BL_PWM_HZ, .clk_cfg = LEDC_AUTO_CLK };
    const ledc_channel_config_t cc = { .gpio_num = C_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_LEDC_CH,
                                       .timer_sel = BL_LEDC_TIMER, .duty = 0, .hpoint = 0 };
    if (ledc_timer_config(&tc) != ESP_OK || ledc_channel_config(&cc) != ESP_OK) {
        ESP_LOGW(TAG, "backlight PWM unavailable: plain on/off");
        gpio_config_t bl = { .pin_bit_mask = 1ULL << C_PIN_BL, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&bl); gpio_set_level(C_PIN_BL, 1);
    }
}

/* the speaker amps off from the first moment (active low, pulled up on the
   board): audio_port_es8311.c takes the pin over and lets them on per cue */
static void amp_off(void) {
    gpio_config_t sd = { .pin_bit_mask = 1ULL << C_PIN_AMP_SD, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&sd); gpio_set_level(C_PIN_AMP_SD, 1);   /* active low */
}

static bool ldo_up(int chan, int mv) {
    esp_ldo_channel_handle_t h;
    esp_ldo_channel_config_t c = { .chan_id = chan, .voltage_mv = mv };
    esp_err_t err = esp_ldo_acquire_channel(&c, &h);
    if (err != ESP_OK) ESP_LOGE(TAG, "LDO%d %d mV: %s", chan, mv, esp_err_to_name(err));
    return err == ESP_OK;
}

bool display_port_init(void) {
    amp_off();
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = C_PIN_TP_SDA, .scl_io_num = C_PIN_TP_SCL,
                                    .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                    .flags.enable_internal_pullup = 1 };
    if (i2c_new_master_bus(&bus, &s_i2c) != ESP_OK) { s_i2c = NULL; ESP_LOGE(TAG, "no I2C bus for the touch"); }
    if (s_i2c) {                                /* who is on the bus (the GT911; the 10.1"'s ES8311 at 0x18) - bring-up's first question */
        char seen[96] = ""; int n = 0;
        for (int a = 0x08; a < 0x78 && n < (int)sizeof seen - 6; a++)
            if (i2c_master_probe(s_i2c, a, 20) == ESP_OK) n += snprintf(seen + n, sizeof seen - n, " 0x%02x", a);
        ESP_LOGI(TAG, "I2C (SDA %d SCL %d):%s", C_PIN_TP_SDA, C_PIN_TP_SCL, n ? seen : " nothing answered");
    }
    backlight_init();                           /* dark until the first frame is in */
    if (!ldo_up(C_LDO_DSI_PHY_CHAN, C_LDO_DSI_PHY_MV)) return false;
    ldo_up(4, 3300);
    esp_lcd_dsi_bus_config_t bc = { .bus_id = 0, .num_data_lanes = DSI_LANES,
                                    .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT, .lane_bit_rate_mbps = DSI_LANE_MBPS };
    esp_err_t err = esp_lcd_new_dsi_bus(&bc, &s_bus);
    if (err != ESP_OK) { ESP_LOGE(TAG, "DSI bus: %s", esp_err_to_name(err)); return false; }
    esp_lcd_dbi_io_config_t dbi = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    err = esp_lcd_new_panel_io_dbi(s_bus, &dbi, &s_dbi);
    if (err != ESP_OK) { ESP_LOGE(TAG, "DBI io: %s", esp_err_to_name(err)); return false; }
    const esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLK_MHZ,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = NUM_FBS,
        .video_timing = {
            .h_size = C_PANEL_W, .v_size = C_PANEL_H,
            .hsync_back_porch = 160, .hsync_pulse_width = 70, .hsync_front_porch = 160,
            .vsync_back_porch = 23, .vsync_pulse_width = 10, .vsync_front_porch = 12,
        },
    };
    ek79007_vendor_config_t vendor = { .mipi_config = { .dsi_bus = s_bus, .dpi_config = &dpi } };
    const esp_lcd_panel_dev_config_t pc = { .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                            .bits_per_pixel = 16, .vendor_config = &vendor };
    err = esp_lcd_new_panel_ek79007(s_dbi, &pc, &s_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "EK79007 panel: %s", esp_err_to_name(err)); s_panel = NULL; return false; }
    s_fb_sem = xSemaphoreCreateBinary();
    const esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_refresh_done = on_refresh_done };
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL);
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);                /* the stream starts here */
    void *f0, *f1, *f2;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(s_panel, NUM_FBS, &f0, &f1, &f2));
    s_fbs[0] = f0; s_fbs[1] = f1; s_fbs[2] = f2;
    for (int i = 0; i < NUM_FBS; i++) memset(s_fbs[i], 0, (size_t)C_PANEL_W * C_PANEL_H * 2);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, C_PANEL_W, C_PANEL_H, s_fbs[0]);   /* black on the glass, written back from the cache */
    s_lit = true; backlight_apply();
    ESP_LOGI(TAG, "CrowPanel P4 DSI panel %dx%d up (%d lanes x %d Mbps, %d MHz dpi, %d frame buffers in PSRAM), touch bus SDA %d SCL %d",
             C_PANEL_W, C_PANEL_H, DSI_LANES, DSI_LANE_MBPS, DPI_CLK_MHZ, NUM_FBS, C_PIN_TP_SDA, C_PIN_TP_SCL);
    return true;
}

uint16_t *display_port_direct_fb(int i) { return s_panel && i >= 0 && i < NUM_FBS ? s_fbs[i] : NULL; }

static int fb_index(const uint16_t *fb) {
    for (int i = 0; i < NUM_FBS; i++) if (fb == s_fbs[i]) return i;
    return -1;
}

/* turned 180 in place: the frame reversed, two px a word (n even) */
static void reverse_in_place(uint16_t *fb, size_t n) {
    uint32_t *a = (uint32_t *)fb, *b = (uint32_t *)fb + n / 2 - 1;
    while (a < b) { uint32_t va = *a, vb = *b; *a++ = (vb >> 16) | (vb << 16); *b-- = (va >> 16) | (va << 16); }
}

/* until the DMA has left buffer i: one refresh_done after the switch away
 * from it. The buffer last switched to is never free; a caller writing it
 * (a one-off page drawn over and over into fb[0]) draws on the glass. */
static void wait_free(int i) {
    if (i < 0 || i == s_shown || !s_leaving[i]) return;
    while ((uint32_t)(s_fb_done - s_left_at[i]) < 1)
        if (xSemaphoreTake(s_fb_sem, pdMS_TO_TICKS(SWAP_WAIT_MS)) != pdTRUE) break;   /* no stream: nothing is reading it */
    s_leaving[i] = false;
}

static void switch_to(int i) {
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, C_PANEL_W, C_PANEL_H, s_fbs[i]);   /* our own buffer: written back from the cache, made current */
    if (i == s_shown) return;
    s_left_at[s_shown] = s_fb_done;             /* after the draw: a restart from here on took the new one */
    s_leaving[s_shown] = true;
    s_shown = i;
}

void display_port_direct_wait(const uint16_t *fb) {
    if (!s_panel) return;
    int64_t t0 = esp_timer_get_time();
    wait_free(fb_index(fb));
    s_prof_wait += esp_timer_get_time() - t0;
}

void display_port_flush(const uint16_t *fb) {
    if (!s_panel || !fb) return;
    const size_t n = (size_t)C_PANEL_W * C_PANEL_H;
    int own = fb_index(fb);
    int64_t t0 = esp_timer_get_time(), t1;
    if (own >= 0) {                             /* the tank drew straight into a panel buffer: switch to it, no copy */
        if (s_inverted) reverse_in_place(s_fbs[own], n);
        switch_to(own);
        s_prof_send += esp_timer_get_time() - t0;
        return;
    }
    int back = (s_shown + 1) % NUM_FBS;         /* a foreign frame (a one-off page): copied into a buffer off the glass */
    wait_free(back);
    t1 = esp_timer_get_time();
    s_prof_wait += t1 - t0;
    if (!s_inverted) memcpy(s_fbs[back], fb, n * 2);
    else {
        const uint32_t *s = (const uint32_t *)fb + n / 2 - 1;
        uint32_t *o = (uint32_t *)s_fbs[back];
        for (size_t i = 0; i < n / 2; i++) { uint32_t v = s[-(int)i]; o[i] = (v >> 16) | (v << 16); }
    }
    switch_to(back);
    s_prof_send += esp_timer_get_time() - t1;
}

/* the grace: the backlight off; the stream runs on (the panel shows black-lit) */
void display_port_sleep(void) {
    s_lit = false;
    if (s_panel) backlight_apply();
}
void display_port_wake(void) {
    if (!s_panel) return;
    for (int i = 0; i < NUM_FBS; i++) s_leaving[i] = false;
    s_lit = true; backlight_apply();
}
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    if (s_panel) backlight_apply();
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* the night's deep sleep: the backlight held off (the amps: audio_port_deep_sleep_pins); a wake is a boot */
void display_port_deep_sleep_pins(bool tp_awake_high) {
    (void)tp_awake_high;
    gpio_reset_pin(C_PIN_BL);
    gpio_set_direction(C_PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(C_PIN_BL, 0);
    gpio_hold_en(C_PIN_BL);
}
void display_port_deep_sleep_bus(void) { }
void display_port_deep_standby(void) { }
