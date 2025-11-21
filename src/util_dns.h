#ifndef UTIL_DNS_H
#define UTIL_DNS_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DNS_PORT 53
// #define ESP_IP_ADDR "192.168.4.1"
#define ESP_IP_ADDR 0x0104A8C0  // 192.168.4.1 in hex (little endian)

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;


// static TaskHandle_t s_dns_task;
// static uint32_t s_ip_be; // network byte order

void start_dns_server(void);


#ifdef __cplusplus
}
#endif

#endif // MODEL_CFG_H