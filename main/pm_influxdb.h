#ifndef _PM_INFLUXDB_H
#define _PM_INFLUXDB_H
#include "esp_system.h"

esp_err_t write_influxdb(const char *station_id, const char *sensor_id, const char *value);

#endif

