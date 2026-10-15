/* board_pins.h — the boards one image runs on, told apart at boot over I2C
 * (display_port_init; SDA / SCL, the QSPI data lines and LCD_CS are shared):
 *
 * Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1: SH8601 + FT3168; V2: CO5300 +
 * CST816). Sources: Waveshare esp-idf examples and the official Arduino
 * variant. VERIFY I2C SDA/SCL on the bench: Waveshare's own code says
 * SDA=15/SCL=14, the Arduino variant says the reverse.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-1.75C (2026-10-01; ROUND 466x466 CO5300 +
 * CST9217). Sources: resources/ESP32-S3-Touch-AMOLED-1.75C/ (the schematic's
 * GPIO table) and Waveshare's BSP (waveshare/esp32_s3_touch_amoled_1_75c).
 * No IO expander - the resets are GPIOs - no RTC chip, no SD card; an ES7210
 * microphone ADC the 1.8 does not have (its I2C address is how the board is
 * recognized). PMIC, IMU, codec, amp and every audio pin: as on the 1.8.
 *
 * Waveshare ESP32-S3-Touch-AMOLED-2.06, the WATCH (2026-10-02; 410x502
 * CO5300 + FT3168, the 1.8's panel family a size up; worn, so its own build
 * is a PORTRAIT 410x502 tank, the panel unturned). Sources: resources/ESP32-S3-Touch-AMOLED-2.06-Watch/ (schematic,
 * wiki page) and Waveshare's BSP (waveshare/esp32_s3_touch_amoled_2_06).
 * No IO expander: the resets are GPIOs 8 and 9 - the pins the 1.8 plays its
 * I2S bit clock and data on, so the AUDIO PINS differ here (audio_port) and
 * the 1.8's would hold the panel in reset. An ES7210 like the 1.75C's, and an
 * RTC chip unlike it: no expander + ES7210 + RTC = the watch. DSI_PWR_EN is
 * not a GPIO: it is pulled up to ALDO2, so that rail IS the panel's power
 * switch. PWR key sense on GPIO 10 (SYS_OUT), an SD slot (1/2/3/17) and a
 * vibration motor (GPIO 18, fed from ALDO3) the tank does not use.
 *
 * The Freenove FNK0115 (2026-10-14) is not told apart at boot: it has its own
 * build (CONFIG_POCKET_TANK_FNK0115, TANK_WIDE) and its own display port,
 * display_port_rgb.c - its pins are at the foot of this file.
 *
 * The Elecrow CrowPanel Advanced 7" / 10.1" V1.2 (ESP32-P4, 2026-10-15)
 * likewise: its own build (CONFIG_POCKET_TANK_CROWP4, TANK_WIDE_1024) and
 * display port, display_port_dsi.c. */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H
#include "sdkconfig.h"
#define PIN_LCD_CS        12
#define PIN_LCD_PCLK      11
#define PIN_LCD_DATA0     4
#define PIN_LCD_DATA1     5
#define PIN_LCD_DATA2     6
#define PIN_LCD_DATA3     7
#define PIN_I2C_SDA       15
#define PIN_I2C_SCL       14
#define PIN_TP_INT        21
#define I2C_ADDR_EXPANDER 0x20     /* TCA9554: bit0 LCD_RST, bit1 DSI_PWR_EN, bit2 TOUCH_RST, bit7 SD_CS */
#define I2C_ADDR_FT3168   0x38     /* V1 touch */
#define I2C_ADDR_CST816   0x15     /* V2 touch (probe => V2 board) */
#define PANEL_W           368      /* native portrait */
#define PANEL_H           448
#define V2_PANEL_X_GAP    0x10

/* ---- the 1.75C ---- */
#define R_PIN_LCD_PCLK    38
#define R_PIN_LCD_RST     1
#define R_PIN_LCD_TE      13       /* unused */
#define R_PIN_TP_RST      2
#define R_PIN_TP_INT      11
#define R_PIN_IMU_INT1    21       /* unused */
#define R_PIN_PWR_SENSE   3        /* SYS_OUT: high while the PWR key is down (unused: the PMIC is asked, as on the 1.8) */
#define I2C_ADDR_ES7210   0x40     /* the microphone ADC (probe, with no expander => the 1.75C) */
#define I2C_ADDR_CST9217  0x5A
#define R_PANEL           466      /* round: 466 across, the corners of the square are not there */
#define R_PANEL_X_GAP     6

/* ---- the 2.06 watch ---- */
#define W_PIN_LCD_RST     8
#define W_PIN_TP_RST      9
#define W_PIN_TP_INT      38       /* unused: polled, like the others */
#define W_PIN_LCD_TE      13       /* unused */
#define W_PIN_PWR_SENSE   10       /* SYS_OUT: high while the PWR key is down */
#define W_PIN_I2S_BCLK    41
#define W_PIN_I2S_DOUT    40       /* ESP -> codec DSDIN */
#define W_PIN_MOTOR       18       /* unused (pulled off on the board) */
#define I2C_ADDR_RTC      0x51     /* PCF85063: the 1.8 and the watch have one, the 1.75C does not */
#define W_PANEL_W         410      /* native portrait */
#define W_PANEL_H         502
#define W_PANEL_X_GAP     0x16

/* ---- the Freenove FNK0115 (2026-10-14; its own build, TANK_WIDE) ----
 * The IPS variants, FNK0115L (4.3") and FNK0115Q (5.0"): an 800 x 480 RGB565
 * panel on the ESP32-S3's parallel RGB peripheral (no controller to talk to:
 * the frame is streamed from PSRAM), a GT911 on I2C, a PWM-able backlight.
 * No PMIC, no IMU, no RTC chip, no codec. The TN variants (FNK0115B, N) have
 * an XPT2046 on SPI instead of the GT911: not supported. Source: Freenove's
 * TFT_Config / esp_lcd pattern (Agent_FNK0115_new_ESP-IDF.md). The touch's
 * I2C takes GPIO 19 / 20 - the S3's native USB pins - so this board's
 * console and director are on UART0 through its USB-UART bridge. */
#define F_PIN_BL          2        /* backlight, LEDC PWM */
#define F_PIN_DE          40
#define F_PIN_VSYNC       41
#define F_PIN_HSYNC       39
#define F_PIN_PCLK        42
#define F_PIN_R0 45
#define F_PIN_R1 48
#define F_PIN_R2 47
#define F_PIN_R3 21
#define F_PIN_R4 14
#define F_PIN_G0 5
#define F_PIN_G1 6
#define F_PIN_G2 7
#define F_PIN_G3 15
#define F_PIN_G4 16
#define F_PIN_G5 4
#define F_PIN_B0 8
#define F_PIN_B1 3
#define F_PIN_B2 46
#define F_PIN_B3 9
#define F_PIN_B4 1
#define F_PIN_TP_SDA      19
#define F_PIN_TP_SCL      20
#define F_PIN_TP_INT      18
#define F_PIN_TP_RST      38
#define F_PANEL_W         800
#define F_PANEL_H         480

/* ---- the Elecrow CrowPanel Advanced 7" / 10.1", V1.2 (2026-10-15; its own build, TANK_WIDE_1024) ----
 * An ESP32-P4 (rev 1.3, 32 MB PSRAM, 16 MB flash) with a 1024 x 600 IPS
 * panel on 2-lane MIPI-DSI (EK79007), a GT911 on I2C, a PWM backlight, and an
 * ESP32-C6 for Wi-Fi over SDIO (esp_hosted). The pins are the same on both
 * sizes and on hardware V1.0 / V1.1 / V1.2; only the audio differs (the 7":
 * the 7": two NS4168 on plain I2S; the 10.1" V1.2 schematic draws an ES8311 +
 * an analog amp, but the bench 10.1" has no codec on its bus and Elecrow's
 * firmware drives it as the 7": plain I2S, which is what this build plays). Sources: Elecrow's repos for both sizes
 * (Eagle_SCH&PCB/1.2/readme.md, example/V1.2/idf-code Lesson05 / 07) and the
 * factory firmware's boot log. The console is UART0 (GPIO 37 / 38) through the
 * board's CH340; the P4's own USB is a separate port. */
#define C_PIN_BL          31       /* LCD_BK_EN -> the MT9201 backlight driver, LEDC PWM */
#define C_PIN_LCD_RST     41       /* wired, but Elecrow's driver never touches it (reset_gpio_num -1) */
#define C_PIN_TP_SDA      45
#define C_PIN_TP_SCL      46
#define C_PIN_TP_RST      40
#define C_PIN_TP_INT      42
#define C_PIN_AMP_SD      30       /* the speaker amps' enable, ACTIVE LOW (10k pull-up): off until a cue plays */
#define C_PIN_I2S_BCLK    22       /* plain I2S into the amps, no MCLK (audio_port_es8311.c) */
#define C_PIN_I2S_WS      21
#define C_PIN_I2S_DOUT    23
#define C_PIN_C6_EN       32       /* the ESP32-C6's enable (esp_hosted resets the C6 with it) */
#define C_LDO_DSI_PHY_CHAN 3       /* the P4's internal LDO3 at 2.5 V feeds the MIPI D-PHY */
#define C_LDO_DSI_PHY_MV   2500
#define C_PANEL_W         1024
#define C_PANEL_H         600

/* the GT911 boards (touch_port_ft3168.c): this build's pins and panel */
#if CONFIG_POCKET_TANK_CROWP4
#define GT_PIN_TP_RST     C_PIN_TP_RST
#define GT_PIN_TP_INT     C_PIN_TP_INT
#define GT_PANEL_W        C_PANEL_W
#define GT_PANEL_H        C_PANEL_H
#else
#define GT_PIN_TP_RST     F_PIN_TP_RST
#define GT_PIN_TP_INT     F_PIN_TP_INT
#define GT_PANEL_W        F_PANEL_W
#define GT_PANEL_H        F_PANEL_H
#endif
#endif
