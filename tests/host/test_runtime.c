#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "test_platform.h"
#include "wx_state.h"
#include "tempest_ws.h"
#include "esp_websocket_client.h"
#include "esp_ota_ops.h"
#include "ota_health.h"
static bool connected=true;
static int connects,disconnects,ingested,accepted,rejected;
static esp_event_handler_t handler;
static esp_ota_img_states_t image_state=ESP_OTA_IMG_PENDING_VERIFY;
int esp_crt_bundle_attach(void *unused){return 0;}
bool net_is_connected(void){return connected;}
int tempest_rest_device_id(void){return 1;}
int tempest_rest_ensure_device_id(void){return ESP_OK;}
bool tempest_ingest_message(const char *data,int len){ingested++;return true;}
void *esp_websocket_client_init(const esp_websocket_client_config_t *c){
    assert(strncmp(c->uri,"wss://",6)==0);return (void *)1;
}
int esp_websocket_register_events(void *c,int id,esp_event_handler_t h,void *a){handler=h;return 0;}
int esp_websocket_client_start(void *c){connects++;handler(NULL,NULL,WEBSOCKET_EVENT_CONNECTED,NULL);return 0;}
int esp_websocket_client_stop(void *c){disconnects++;return 0;}
int esp_websocket_client_destroy(void *c){return 0;}
int esp_websocket_client_send_text(void *c,const char *s,int n,int timeout){return n;}
const esp_partition_t *esp_ota_get_running_partition(void){static esp_partition_t p;return &p;}
int esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *s){*s=image_state;return 0;}
int esp_ota_mark_app_valid_cancel_rollback(void){accepted++;image_state=ESP_OTA_IMG_VALID;return 0;}
int esp_ota_mark_app_invalid_rollback_and_reboot(void){rejected++;return 0;}
void test_runtime(void){
    wx_state_init();wx_set_wifi_connected(true);
    assert(tempest_ws_start()==ESP_OK);tempest_ws_poll();
    assert(connects==1 && tempest_ws_is_active());
    esp_websocket_event_data_t event={.op_code=1,.data_ptr="{}",.data_len=2,.payload_len=2,.fin=true};
    handler(NULL,NULL,WEBSOCKET_EVENT_DATA,&event);tempest_ws_poll();
    assert(ingested==1 && disconnects==0);
    wx_note_udp_packet();tempest_ws_poll();assert(disconnects==1);
    test_now+=181;tempest_ws_poll();assert(connects==2);
    connected=false;tempest_ws_poll();assert(disconnects==2);
    for(int flags=0;flags<7;flags++)ota_validate_boot(flags&1,flags&2,flags&4);
    assert(rejected==7 && accepted==0);
    ota_validate_boot(true,true,true);assert(accepted==1);
    ota_validate_boot(false,false,false);assert(rejected==7);
    puts("PASS fallback self-traffic, UDP recovery, Wi-Fi loss and OTA health gate");
}
