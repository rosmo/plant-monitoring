#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "pm_wifi.h"
#include <math.h>
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "pm_influxdb.h"
#include "pm_config.h"
#include "esp_sleep.h"
#include "esp_mac.h"

#define TAG "plantmonitoring"

#define PM_ADC_BIT_WIDTH ADC_WIDTH_BIT_DEFAULT
#define uS_TO_S_FACTOR 1000000

mac_to_station **stations = NULL;
mac_to_station *config = NULL;
int num_stations = 0;

int calculate_vdc_mv(int reading)
{
    return (reading * 2500) / (pow(2, PM_ADC_BIT_WIDTH) - 1);
}

void sync_time(void)
{
    // Set timezone
    ESP_LOGI(TAG, "Timezone is set to: %s", CONFIG_ESP_NTP_TZ);
    setenv("TZ", CONFIG_ESP_NTP_TZ, 1);
    tzset();
    
    // Synchronize time with NTP
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    ESP_LOGI(TAG, "Getting time via NTP from: %s", CONFIG_ESP_NTP_SERVER);
    esp_sntp_setservername(0, CONFIG_ESP_NTP_SERVER);
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;

    // NTP request is sent every 15 seconds, so wait 25 seconds for sync.
    const int retry_count = 25;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    assert(retry < retry_count);

    time(&now);
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
}

void load_config(void) {
    uint8_t mac[8] = { 0 };
    char macstr[24] = { '\0' };
    esp_err_t err;

    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    for (int i = 0; i < 6; i++) {
        sprintf((char *)&macstr[i*3], "%02x:", mac[i]);
    }
    macstr[17] = '\0';
    ESP_LOGI(TAG, "WiFi MAC address: %s", macstr);

    stations = get_config_from_url(CONFIG_ESP_CONFIG_URL, &num_stations, &err);
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Configuration has %d stations:", num_stations);
    for (int i = 0; i < num_stations; i++) {
        ESP_LOGI(TAG, " %03d MAC: %s Station: %s", i+1, stations[i]->mac, stations[i]->station);
        if (strcasecmp(stations[i]->mac, macstr) == 0) {
            config = stations[i];
        }
    }
    assert(config != NULL);
    ESP_LOGI(TAG, "My station name: %s", config->station);
}

void app_main(void)
{
    printf("Plant monitoring system\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }
    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    // Connect to WiFi
    wifi_init();

    // Synchronize time
    sync_time();

    // Load config
    load_config();

    // Configure ADC channels
    adc1_config_width((adc_bits_width_t)ADC_WIDTH_BIT_DEFAULT);
    adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    
    int adc1_reading[3] = {0xcc};
    const char TAG_CH[][10] = {"plant1", "plant2", "plant3"};

    for (int i = 10; i >= 0; i--) {
        adc1_reading[0] = adc1_get_raw(ADC1_CHANNEL_2);
        adc1_reading[1] = adc1_get_raw(ADC1_CHANNEL_3);
        adc1_reading[2] = adc1_get_raw(ADC1_CHANNEL_4);

        for (int o = 0; o < 3; o++) {
            char value[14] = { '\0' };
            ESP_LOGI(TAG_CH[i], "%d", adc1_reading[o]);        
            snprintf(value, 8, "%u", 4095 - calculate_vdc_mv(adc1_reading[o]));

            write_influxdb(config->station, TAG_CH[o], value);
        }
        printf("Entering deep sleep...\n");
        esp_deep_sleep(15 * 60 * uS_TO_S_FACTOR);
        printf("Woke up from deep sleep. Connecting WiFi and syncing time.\n");

        wifi_init();
        sync_time();
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}