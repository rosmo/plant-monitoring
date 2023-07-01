#ifndef _PM_CONFIG_H
#define _PM_CONFIG_H
#include "esp_system.h"

typedef struct _mac_to_station {
    char *mac;
    char *station;
} mac_to_station;

mac_to_station **get_config_from_url(const char *config_url, int *num_stations, esp_err_t *err);

#endif

