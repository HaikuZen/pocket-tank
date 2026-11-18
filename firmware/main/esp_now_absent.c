/* esp_now_absent.c - the CrowPanel P4 only (2026-11-18): its Wi-Fi is the ESP32-C6 over SDIO (esp_hosted +
 * esp_wifi_remote), which carries no ESP-NOW. The tank link (link_port_espnow.c, link_spike.c) links against these
 * instead: every call says ESP_ERR_NOT_SUPPORTED, so the radio never comes up and the link stays off - the tank
 * runs alone, as on a board out of range. (lp_radio_up stops before touching the Wi-Fi on this build.) */
#include "esp_now.h"
esp_err_t esp_now_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_deinit(void) { return ESP_OK; }
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) { (void)cb; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb) { (void)cb; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len) { (void)peer_addr; (void)data; (void)len; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer) { (void)peer; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t esp_now_del_peer(const uint8_t *peer_addr) { (void)peer_addr; return ESP_ERR_NOT_SUPPORTED; }
bool esp_now_is_peer_exist(const uint8_t *peer_addr) { (void)peer_addr; return false; }
