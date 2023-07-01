#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "pm_influxdb.h" 

#define TAG "InfluxDB"

esp_err_t write_influxdb(const char *station_id, const char *sensor_id, const char *value)
{
    // Construct path
    char query_buf[64] = { '\0' };
    snprintf(query_buf, 512, "bucket=%s&precision=s", CONFIG_ESP_INFLUXDB_BUCKET);

    esp_http_client_config_t config = {
        .host = CONFIG_ESP_INFLUXDB_HOST,
        .path = CONFIG_ESP_INFLUXDB_PATH,
        .port = CONFIG_ESP_INFLUXDB_PORT,
        .query = query_buf,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set authentication header
    char auth_buf[64] = { '\0' };
    snprintf(auth_buf, 512, "Token %s:%s", CONFIG_ESP_INFLUXDB_USERNAME, CONFIG_ESP_INFLUXDB_PASSWORD);
    esp_http_client_set_header(client, "Authorization", auth_buf);
    
    // Create post data 
    time_t now = 0;
    time(&now);
    char post_buf[256] = { '\0' };
    snprintf(post_buf, 1024, "soil_moisture,station=%s,sensor=%s value=%s %lld", station_id, sensor_id, value, now);

    ESP_LOGI(TAG, "Posting data to http://%s:%d%s?%s: %s", CONFIG_ESP_INFLUXDB_HOST, CONFIG_ESP_INFLUXDB_PORT, CONFIG_ESP_INFLUXDB_PATH, query_buf, post_buf);

    esp_err_t err; 
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_buf, strlen(post_buf));
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %"PRIu64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

