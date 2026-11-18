/* host stub: sim/touchhost/host_esp.h (--selftest-touch-port) - the FNK0115's / CrowPanel's GT911 (TANK_WIDE) */
#include "host_esp.h"
typedef struct { uint32_t dev_addr; } esp_lcd_touch_io_gt911_config_t;
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS 0x5D
#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() { .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS }
esp_err_t esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *cfg, esp_lcd_touch_handle_t *tp);
