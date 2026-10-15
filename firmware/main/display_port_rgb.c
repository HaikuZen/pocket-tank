/* display_port_rgb.c — the Freenove FNK0115 (2026-10-14): an 800 x 480 IPS
 * panel on the ESP32-S3's parallel RGB peripheral, its own build (TANK_WIDE).
 *
 * There is no controller to talk to: the LCD peripheral streams a frame
 * buffer out of PSRAM continuously, ~32 times a second. The driver keeps
 * three (NUM_FBS); draw_bitmap of one switches the stream to it at the end
 * of the frame being sent. A buffer switched away from may be written once
 * the refill has left it: the driver's on_frame_buf_complete fires right
 * after the bounce refill switches buffers, so one of those after the switch
 * and the old buffer is never read again (wait_free; it was two VSYNCs
 * before 2026-10-14, ~40 ms of every frame). Writing the buffer being shown -
 * the driver's own path for a foreign buffer - would tear. The frame is the panel, px for px; flipped, it is copied turned
 * 180 degrees. RGB565 in the CPU's byte order, as the tank renders it.
 *
 * The tank renders straight into these buffers (display_port_direct_fb,
 * 2026-10-14): a flush of one is then only the switch, and the tank waits
 * (display_port_direct_wait) only before it writes the next one. The 750 KB
 * copy this replaced took ~60 ms a frame with the refill and the model on
 * PSRAM. Three, not two: with two, every frame waited for the switch before
 * the next scene prefetch could start, and locked to two panel frames (~16
 * fps).
 * Turned 180, the frame is reversed in place (the renderer restores the
 * whole scene into its buffer every frame, so nothing reads it back).
 * A frame from anywhere else (a one-off page) is still copied.
 *
 * The DMA does not read PSRAM itself: it streams from two bounce buffers in
 * internal RAM that the CPU refills from the frame buffer. Streamed straight
 * from PSRAM, the picture drifted sideways on the glass whenever the render
 * or a flash access held PSRAM (the first flash, 2026-10-14); the code runs
 * from PSRAM too (sdkconfig.fnk0115) so flash work never stalls the refill.
 *
 * The backlight is PWM (LEDC) on F_PIN_BL; "brightness" is its duty. The
 * panel itself has no sleep: display_port_sleep turns the backlight off, and
 * a light sleep stops the stream (display_port_wake restarts it).
 *
 * The display owns the I2C bus on the other boards and does here too: the
 * GT911's, on GPIO 19 / 20 (board_i2c_bus; the PMIC, IMU, RTC and codec
 * probes find nothing on it and step aside). Pins, timings: board_pins.h and
 * Agent_FNK0115_new_ESP-IDF.md - the verified Freenove pattern. */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_attr.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

_Static_assert(TANK_W == F_PANEL_W && TANK_H == F_PANEL_H, "the FNK0115's display port wants its own build (TANK_WIDE): the frame is the panel");

static const char *TAG = "rgb";
#define PCLK_HZ        (13 * 1000 * 1000)       /* 812 x 496 a frame with the porches: ~32 Hz. Not lower: at 11 MHz the panel took no picture and cycled its own test colours (2026-10-14) */
#define BOUNCE_PX      (24 * F_PANEL_W)         /* 2 x 38 KB of internal DMA RAM (the dirty mask moved to PSRAM for it); must divide the frame (480 / 24 = 20). 10 lines (0.6 ms a refill) ran late in 6-17% of frames while the advisor computed, 16 in 2.5-8% (2026-10-14) */
#define BL_LEDC_TIMER  LEDC_TIMER_1
#define BL_LEDC_CH     LEDC_CHANNEL_1
#define BL_PWM_HZ      20000                    /* above hearing: a backlight driver can sing */
#define SWAP_WAIT_MS   100                      /* a frame is ~31 ms; longer = the stream stopped (asleep) */

static esp_lcd_panel_handle_t s_panel;
static i2c_master_bus_handle_t s_i2c;
#define NUM_FBS        3                        /* the tank renders the third while one is on the glass and one waits */
static uint16_t *s_fbs[NUM_FBS];                /* the driver's frame buffers */
static int  s_shown = 0;                        /* the one last switched to (on the glass, or next) */
static volatile uint32_t s_fb_done;             /* frame buffers the refill finished since boot */
static uint32_t s_left_at[NUM_FBS];             /* s_fb_done when a switch left buffer i ... */
static bool s_leaving[NUM_FBS];                 /* ... and the refill may still be reading it */
static SemaphoreHandle_t s_fb_sem;
static bool s_inverted;
static uint8_t s_brightness = 0xFF;
static bool s_lit;                              /* the backlight is meant to be on (not asleep) */
static int64_t s_prof_wait, s_prof_send;
/* late refills (2026-10-14, the flicker hunt): a VSYNC with no frame
   completion since the one before means the refill fell behind and the driver
   reset it there - the picture flickers. Logged with the display's 10 s line. */
static volatile uint32_t s_vsyncs, s_late;
static uint32_t s_done_at_vsync;

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
    static uint32_t v0, l0;                     /* the window: since the last call */
    uint32_t v = s_vsyncs, l = s_late;
    ESP_LOGI(TAG, "refill late in %lu of %lu frames (%lu since boot)", (unsigned long)(l - l0), (unsigned long)(v - v0), (unsigned long)l);
    v0 = v; l0 = l;
}

/* the bounce refill read the last of a buffer and moved to the one draw_bitmap asked for */
static IRAM_ATTR bool on_fb_done(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *ctx) {
    (void)panel; (void)edata; (void)ctx;
    s_fb_done++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_fb_sem, &woken);
    return woken == pdTRUE;
}

static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *ctx) {
    (void)panel; (void)edata; (void)ctx;
    uint32_t d = s_fb_done;
    if (d == s_done_at_vsync && s_vsyncs) s_late++;   /* a whole frame went out and the refill never finished one */
    s_done_at_vsync = d; s_vsyncs++;
    return false;
}

static void backlight_apply(void) {
    uint32_t duty = s_lit ? s_brightness : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH);
}
static void backlight_init(void) {
    gpio_hold_dis(F_PIN_BL);                    /* a deep-sleep wake: the night held it low */
    const ledc_timer_config_t tc = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                                     .timer_num = BL_LEDC_TIMER, .freq_hz = BL_PWM_HZ, .clk_cfg = LEDC_AUTO_CLK };
    const ledc_channel_config_t cc = { .gpio_num = F_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_LEDC_CH,
                                       .timer_sel = BL_LEDC_TIMER, .duty = 0, .hpoint = 0 };
    if (ledc_timer_config(&tc) != ESP_OK || ledc_channel_config(&cc) != ESP_OK) {
        ESP_LOGW(TAG, "backlight PWM unavailable: plain on/off");
        gpio_config_t bl = { .pin_bit_mask = 1ULL << F_PIN_BL, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&bl); gpio_set_level(F_PIN_BL, 1);
    }
}

bool display_port_init(void) {
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = F_PIN_TP_SDA, .scl_io_num = F_PIN_TP_SCL,
                                    .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
                                    .flags.enable_internal_pullup = 1 };
    if (i2c_new_master_bus(&bus, &s_i2c) != ESP_OK) { s_i2c = NULL; ESP_LOGE(TAG, "no I2C bus for the touch"); }
    backlight_init();                           /* dark until the first frame is in */
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = PCLK_HZ, .h_res = F_PANEL_W, .v_res = F_PANEL_H,
            .hsync_pulse_width = 4, .hsync_back_porch = 8, .hsync_front_porch = 4,
            .vsync_pulse_width = 4, .vsync_back_porch = 8, .vsync_front_porch = 4,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        .in_color_format = LCD_COLOR_FMT_RGB565,
#else
        .bits_per_pixel = 16,
#endif
        .num_fbs = NUM_FBS,
        .dma_burst_size = 64,
        .hsync_gpio_num = F_PIN_HSYNC, .vsync_gpio_num = F_PIN_VSYNC, .de_gpio_num = F_PIN_DE,
        .pclk_gpio_num = F_PIN_PCLK, .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {                     /* bit 0 (LSB) .. bit 15 of RGB565: blue, green, red */
            F_PIN_B0, F_PIN_B1, F_PIN_B2, F_PIN_B3, F_PIN_B4,
            F_PIN_G0, F_PIN_G1, F_PIN_G2, F_PIN_G3, F_PIN_G4, F_PIN_G5,
            F_PIN_R0, F_PIN_R1, F_PIN_R2, F_PIN_R3, F_PIN_R4,
        },
        .bounce_buffer_size_px = BOUNCE_PX,     /* the DMA streams from internal RAM, refilled from PSRAM by the CPU */
        .flags.fb_in_psram = 1,
    };
    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) { ESP_LOGE(TAG, "RGB panel: %s", esp_err_to_name(err)); s_panel = NULL; return false; }
    s_fb_sem = xSemaphoreCreateBinary();
    const esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_frame_buf_complete = on_fb_done, .on_vsync = on_vsync };
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, NULL);
    void *f0, *f1, *f2;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, NUM_FBS, &f0, &f1, &f2));
    s_fbs[0] = f0; s_fbs[1] = f1; s_fbs[2] = f2;
    for (int i = 0; i < NUM_FBS; i++) memset(s_fbs[i], 0, (size_t)F_PANEL_W * F_PANEL_H * 2);
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);                /* the stream starts here */
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, F_PANEL_W, F_PANEL_H, s_fbs[0]);   /* black on the glass, written back from the cache */
    s_lit = true; backlight_apply();
    ESP_LOGI(TAG, "FNK0115 RGB panel %dx%d up (%d MHz pclk, %d frame buffers in PSRAM, bounce %d px), touch bus SDA %d SCL %d",
             F_PANEL_W, F_PANEL_H, PCLK_HZ / 1000000, NUM_FBS, BOUNCE_PX, F_PIN_TP_SDA, F_PIN_TP_SCL);
    return true;
}

uint16_t *display_port_direct_fb(int i) { return s_panel && i >= 0 && i < NUM_FBS ? s_fbs[i] : NULL; }

static int fb_index(const uint16_t *fb) {
    for (int i = 0; i < NUM_FBS; i++) if (fb == s_fbs[i]) return i;
    return -1;
}

/* turned 180 in place: the frame reversed, two px a word (the frame is 4-aligned, n even) */
static void reverse_in_place(uint16_t *fb, size_t n) {
    uint32_t *a = (uint32_t *)fb, *b = (uint32_t *)fb + n / 2 - 1;
    while (a < b) { uint32_t va = *a, vb = *b; *a++ = (vb >> 16) | (vb << 16); *b-- = (va >> 16) | (va << 16); }
}

/* until the refill has left buffer i: one on_frame_buf_complete after the
 * switch away from it, and the driver reads it no more. The buffer last
 * switched to is never free; a caller writing it (a one-off page drawn over
 * and over into fb[0]) draws on the glass, as before. */
static void wait_free(int i) {
    if (i < 0 || i == s_shown || !s_leaving[i]) return;
    while ((uint32_t)(s_fb_done - s_left_at[i]) < 1)
        if (xSemaphoreTake(s_fb_sem, pdMS_TO_TICKS(SWAP_WAIT_MS)) != pdTRUE) break;   /* no stream (asleep): nothing is reading it */
    s_leaving[i] = false;
}

static void switch_to(int i) {
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, F_PANEL_W, F_PANEL_H, s_fbs[i]);   /* our own buffer: written back from the cache, switched to */
    if (i == s_shown) return;
    s_left_at[s_shown] = s_fb_done;             /* after the draw: a completion from here on took the new one */
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
    const size_t n = (size_t)F_PANEL_W * F_PANEL_H;
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
    else {                                      /* turned 180: the last px first, two a word (the frame is 4-aligned, n even) */
        const uint32_t *s = (const uint32_t *)fb + n / 2 - 1;
        uint32_t *o = (uint32_t *)s_fbs[back];
        for (size_t i = 0; i < n / 2; i++) { uint32_t v = s[-(int)i]; o[i] = (v >> 16) | (v << 16); }
    }
    switch_to(back);
    s_prof_send += esp_timer_get_time() - t1;
}

/* the grace (a light sleep follows): the backlight off - the panel has no
 * sleep of its own, and a light sleep stops the stream anyway */
void display_port_sleep(void) {
    s_lit = false;
    if (s_panel) backlight_apply();
}
void display_port_wake(void) {
    if (!s_panel) return;
    esp_lcd_rgb_panel_restart(s_panel);         /* the stream, from the top of the frame after a light sleep */
    for (int i = 0; i < NUM_FBS; i++) s_leaving[i] = false;   /* restarted on the buffer last switched to */
    s_lit = true; backlight_apply();
}
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    if (s_panel) backlight_apply();
}
uint8_t display_port_brightness(void) { return s_brightness; }

/* the night's deep sleep: the backlight held off; the rest of the panel's
 * pins are isolated by the deep-sleep hold, and a wake is a boot */
void display_port_deep_sleep_pins(bool tp_awake_high) {
    (void)tp_awake_high;
    gpio_reset_pin(F_PIN_BL);
    gpio_set_direction(F_PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(F_PIN_BL, 0);
    gpio_hold_en(F_PIN_BL);
}
void display_port_deep_sleep_bus(void) { }
void display_port_deep_standby(void) { }
