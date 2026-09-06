#pragma once
#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>
#define pdMS_TO_TICKS(n) (n)
typedef void *esp_websocket_client_handle_t;
typedef enum {WEBSOCKET_EVENT_CONNECTED,WEBSOCKET_EVENT_DISCONNECTED,WEBSOCKET_EVENT_DATA,WEBSOCKET_EVENT_ERROR,WEBSOCKET_EVENT_ANY} esp_websocket_event_id_t;
#define WS_TRANSPORT_OPCODES_TEXT 1
typedef struct { int op_code,data_len,payload_len,payload_offset; bool fin; const char *data_ptr; } esp_websocket_event_data_t;
typedef struct {const char *uri;int (*crt_bundle_attach)(void *);int network_timeout_ms,reconnect_timeout_ms;} esp_websocket_client_config_t;
esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *);
int esp_websocket_register_events(void *,int,esp_event_handler_t,void *);
int esp_websocket_client_start(void *);
int esp_websocket_client_stop(void *);
int esp_websocket_client_destroy(void *);
int esp_websocket_client_send_text(void *,const char *,int,int);
