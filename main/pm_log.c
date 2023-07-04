/*
  Logging code borrowed from: https://github.com/nopnop2002/esp-idf-net-logging

  And modified to add syslog protocol format (RFC 5426)
*/

#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/message_buffer.h"
#include "lwip/sockets.h"

#include "esp_system.h"
#include "esp_log.h"
#include "pm_log.h"

#define TAG "plantmonitoring"

MessageBufferHandle_t xMessageBufferTrans;
bool writeToStdout;
char wifiIpAddress[16] = { '-', '\0' };

void udp_client(void *pvParameters) {
	PARAMETER_t *task_parameter = pvParameters;
	PARAMETER_t param;
	memcpy((char *)&param, task_parameter, sizeof(PARAMETER_t));
	
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(param.port);
	addr.sin_addr.s_addr = inet_addr(param.ipv4);

	int fd;
	int ret;
	fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	LWIP_ASSERT("fd >= 0", fd >= 0);

	char buffer[xItemSize];
	xTaskNotifyGive(param.taskHandle);

	while(1) {
		size_t received = xMessageBufferReceive(xMessageBufferTrans, buffer, sizeof(buffer), portMAX_DELAY);
		if (received > 0) {
			ret = lwip_sendto(fd, buffer, received, 0, (struct sockaddr *)&addr, sizeof(addr));
		} else {
			printf("xMessageBufferReceive fail\n");
			break;
		}
	}

	ret = lwip_close(fd);
	LWIP_ASSERT("ret == 0", ret == 0);
	vTaskDelete(NULL);
}

esp_err_t udp_logging_init(char *ipaddr, unsigned long port, int16_t enableStdout) {
    if (strcmp(ipaddr, "") == 0) {
        return ESP_OK;
    }
	printf("Start UDP syslog logging: ipaddr=[%s] port=%ld\n", ipaddr, port);

	xMessageBufferTrans = xMessageBufferCreate(xBufferSizeBytes);
	configASSERT(xMessageBufferTrans);

	PARAMETER_t param;
	param.port = port;
	strcpy(param.ipv4, ipaddr);
	param.taskHandle = xTaskGetCurrentTaskHandle();
	xTaskCreate(udp_client, "UDP", 1024*6, (void *)&param, 2, NULL);

	ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	
	// Set function used to output log entries.
	writeToStdout = enableStdout;
	esp_log_set_vprintf(logging_vprintf);
	return ESP_OK;
}

int logging_vprintf( const char *fmt, va_list l ) {
	char *buffer;
    char *msg;
    time_t now;
    struct tm now_utc;
    char now_str[25];

    buffer = calloc(xItemSize, sizeof(char));
    assert(buffer != NULL);

	int buffer_len = vsprintf(buffer, fmt, l);
	if (buffer_len > 0) {
        msg = calloc(xItemSize, sizeof(char));
        assert(msg != NULL);
        
        now = time(NULL);
        gmtime_r(&now, &now_utc);
        strftime(now_str, sizeof(now_str), "%Y-%m-%dT%H:%M:%SZ", &now_utc);
    
        // Fixed facility 1 (user messages) + notice priority (5)
        int msg_len = snprintf(msg, xItemSize, "<13>1 %s %s %s - 0001 - %s", now_str, wifiIpAddress, TAG, buffer);
   
		// Send MessageBuffer
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;

		size_t sended = xMessageBufferSendFromISR(xMessageBufferTrans, msg, msg_len, &xHigherPriorityTaskWoken);
		assert(sended == msg_len);
        
        free(msg);
	}
    free(buffer);

	// Write to stdout
	if (writeToStdout) {
		return vprintf(fmt, l);
	} else {
		return 0;
	}
}

void init_logging(void) 
{
#ifdef CONFIG_ESP_SYSLOG_SERVER
    udp_logging_init(CONFIG_ESP_SYSLOG_SERVER, 514, 1);
#endif
}