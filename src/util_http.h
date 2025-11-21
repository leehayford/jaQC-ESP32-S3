#ifndef UTIL_HTTP_H
#define UTIL_HTTP_H

#include "esp_http_server.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t static_get_handler(httpd_req_t *req);
esp_err_t register_route(const httpd_uri_t *uri_handler);
esp_err_t start_webserver(void);

#ifdef __cplusplus
}
#endif

#endif // UTIL_HTTP_H