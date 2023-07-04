#ifndef _PM_LOG_H
#define _PM_LOG_H
#include "esp_system.h"
#include "esp_log.h"

typedef struct {
	uint16_t port;
	char ipv4[20]; // xxx.xxx.xxx.xxx
	TaskHandle_t taskHandle;
} PARAMETER_t;

#define xBufferSizeBytes 2048
#define xItemSize 768

int logging_vprintf(const char *fmt, va_list l);
esp_err_t udp_logging_init(char *ipaddr, unsigned long port, int16_t enableStdout);
void init_logging(void);

#endif