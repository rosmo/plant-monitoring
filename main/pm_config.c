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
#include "pm_config.h" 

#define TAG "Config"
#define MAX_CONFIG_SIZE 2048

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    copy_len = MIN(evt->data_len, (MAX_CONFIG_SIZE - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    const int buffer_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(buffer_len);
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (buffer_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        default:
            break;
    }
    return ESP_OK;
}

mac_to_station **get_config_from_url(const char *config_url, int *num_stations, esp_err_t *err)
{
    mac_to_station **stations = NULL;
    char output_buffer[MAX_CONFIG_SIZE] = {0}; 
    esp_http_client_config_t config = {
        .url = config_url,
        .event_handler = _http_event_handler,
        .user_data = output_buffer,
        .buffer_size = MAX_CONFIG_SIZE,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_LOGI(TAG, "Loading configuration from: %s", config_url);

    *err = esp_http_client_perform(client);
    if (*err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %"PRIu64,
        esp_http_client_get_status_code(client),
        esp_http_client_get_content_length(client));
        
        char *saveptr = output_buffer; 
        char *line = strtok_r(saveptr, "\n", &saveptr);

        *num_stations = 0;
        while (line != NULL) {
            ESP_LOGI(TAG, "Got line (%s)", line);
            
            char *line_to_split = strdup(line);
            char *inner_saveptr = line_to_split;
            // Trim whitespace from start
            while (strlen(line_to_split) > 0 && *line_to_split == ' ') {
                line_to_split++;
            }

            // Trim whitespace from the end
            while (strlen(line_to_split) > 0 && line_to_split[strlen(line_to_split)] == ' ') {
                line_to_split[strlen(line_to_split)] = '\0';
            }
            
            // Check string is not empty
            if (strcmp(line_to_split, "") != 0) {
                char *mac = strtok_r(inner_saveptr, " ", &inner_saveptr);
                if (mac != NULL) {
                    char *station = strtok_r(NULL, " ", &inner_saveptr);
                    *num_stations += 1;
                    stations = (mac_to_station **)realloc(stations, *num_stations * sizeof(mac_to_station *));
                    assert(stations != NULL);
    
                    stations[*num_stations - 1] = malloc(sizeof(mac_to_station));
                    assert(stations[*num_stations - 1] != NULL);
    
                    stations[*num_stations - 1]->mac = strdup(mac);
                    stations[*num_stations - 1]->station = strdup(station);
                    ESP_LOGI(TAG, "Got MAC=%s, station=%s", stations[*num_stations - 1]->mac, stations[*num_stations - 1]->station);
                }
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(*err));
    }
    esp_http_client_cleanup(client);
    return stations;
}


