#include "app.h"
#include "SetupAccess.h"
#include <cstring>
#include <algorithm>
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static Config saved;
static char ap_name[32], ap_password[16];
static std::string read_string(nvs_handle_t h, const char *key) {
    size_t size = 0;
    if (nvs_get_str(h, key, nullptr, &size) != ESP_OK || size == 0) return {};
    std::vector<char> value(size);
    if (nvs_get_str(h, key, value.data(), &size) != ESP_OK) return {};
    return value.data();
}
Config config_load() {
    nvs_handle_t h;
    if (nvs_open("panel", NVS_READONLY, &h) != ESP_OK) return {};
    Config c{read_string(h,"ssid"),read_string(h,"password"),read_string(h,"uri"),read_string(h,"token"),read_string(h,"entity")};
    nvs_close(h); return c;
}
esp_err_t config_save(const Config &c) {
    nvs_handle_t h; esp_err_t err = nvs_open("panel", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    const char *keys[] = {"ssid","password","uri","token","entity"};
    const std::string *values[] = {&c.ssid,&c.password,&c.uri,&c.token,&c.entity};
    for (int i=0; i<5 && err==ESP_OK; ++i) err=nvs_set_str(h,keys[i],values[i]->c_str());
    if (err==ESP_OK) err=nvs_commit(h);
    nvs_close(h); return err;
}
static void wifi_event(void *, esp_event_base_t base, int32_t id, void *) {
    if (base==WIFI_EVENT && (id==WIFI_EVENT_STA_START || id==WIFI_EVENT_STA_DISCONNECTED)) esp_wifi_connect();
}
void network_start(const Config &c, bool setup) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_mode(setup ? WIFI_MODE_APSTA : WIFI_MODE_STA));
    wifi_config_t sta{};
    memcpy(sta.sta.ssid,c.ssid.data(),std::min(c.ssid.size(),sizeof(sta.sta.ssid)));
    memcpy(sta.sta.password,c.password.data(),std::min(c.password.size(),sizeof(sta.sta.password)));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&sta));
    if(setup) network_setup_ap(true);
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_sntp_config_t timeConfig=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&timeConfig));
}
std::string network_ip() {
    auto netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info{};
    wifi_ap_record_t connected{};
    if(!netif || esp_wifi_sta_get_ap_info(&connected)!=ESP_OK ||
       esp_netif_get_ip_info(netif,&info)!=ESP_OK || !info.ip.addr) return {};
    char ip[16]; snprintf(ip,sizeof(ip),IPSTR,IP2STR(&info.ip)); return ip;
}
void network_setup_ap(bool enabled) {
    if(!enabled) { ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); return; }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    // Fresh name and password each time; the QR on screen carries both.
    uint32_t random=esp_random();
    snprintf(ap_name,sizeof(ap_name),"ONX-%04X",unsigned(random&0xffff));
    snprintf(ap_password,sizeof(ap_password),"%08lX%04X",(unsigned long)esp_random(),unsigned(random>>16));
    wifi_config_t ap{};
    strcpy((char*)ap.ap.ssid,ap_name); strcpy((char*)ap.ap.password,ap_password);
    ap.ap.authmode=WIFI_AUTH_WPA2_PSK; ap.ap.max_connection=2; ap.ap.channel=1;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ap));
}
const char *setup_ssid() { return ap_name; }
const char *setup_password() { return ap_password; }
static bool allowed(httpd_req_t *req) {
    // Server exists only during onboarding or physically requested configuration.
    // Accept both AP and station addresses, including dual-stack mapped addresses.
    sockaddr_storage address{}; socklen_t size=sizeof(address);
    if(getsockname(httpd_req_to_sockfd(req),(sockaddr*)&address,&size)!=0) {
        httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Nieznany interfejs"); return false;
    }
    for(const char *key:{"WIFI_AP_DEF","WIFI_STA_DEF"}) {
        auto netif=esp_netif_get_handle_from_ifkey(key);
        esp_netif_ip_info_t info{};
        if(!netif || !esp_netif_is_netif_up(netif) || esp_netif_get_ip_info(netif,&info)!=ESP_OK ||
           !info.ip.addr || !is_setup_address(address,size,info.ip.addr)) continue;
        char ip[16],host[64]{},origin[96]{};
        snprintf(ip,sizeof(ip),IPSTR,IP2STR(&info.ip));
        httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host));
        // Only the IP shown on screen is accepted; this also rejects DNS rebinding.
        if(std::string(host)!=ip && std::string(host)!=std::string(ip)+":80") break;
        if(httpd_req_get_hdr_value_len(req,"Origin")) {
            if(httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK ||
               std::string(origin)!="http://"+std::string(host)) break;
        }
        return true;
    }
    httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Otwórz adres IP widoczny na ekranie panelu");
    return false;
}
static cJSON *body(httpd_req_t *req) {
    if (!allowed(req)) return nullptr;
    char type[40]{};
    httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type));
    if (strcmp(type,"application/json") || req->content_len<=0 || req->content_len>4096) {
        httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Oczekiwano JSON (maks. 4096 bajtów)"); return nullptr;
    }
    std::string text(req->content_len,'\0'); size_t offset=0;
    while (offset<text.size()) {
        int n=httpd_req_recv(req,&text[offset],text.size()-offset);
        if(n<=0) { httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"Przekroczono czas odczytu"); return nullptr; }
        offset+=n;
    }
    cJSON *json=cJSON_Parse(text.c_str());
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Niepoprawny JSON"); return nullptr; }
    return json;
}
static std::string field(cJSON *j,const char *key) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
// Leave the network cleanly first: an access point that still holds the old
// session (PMF) may stop delivering unicast frames to the panel after reboot.
void panel_restart() {
    esp_wifi_disconnect(); vTaskDelay(pdMS_TO_TICKS(200)); esp_restart();
}
static void reboot_task(void *) { vTaskDelay(pdMS_TO_TICKS(1200)); panel_restart(); }
static esp_err_t save_reply(httpd_req_t *r,const Config &c,bool finish=false) {
    if(config_save(c)!=ESP_OK) return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Nie udało się zapisać konfiguracji");
    nvs_handle_t h;
    if(nvs_open("panel",NVS_READWRITE,&h)==ESP_OK) {
        nvs_set_u8(h,"setup",finish ? 0 : 1); nvs_commit(h); nvs_close(h);
    }
    httpd_resp_sendstr(r,"Zapisano. Panel uruchamia się ponownie; sprawdź ekran.");
    xTaskCreate(reboot_task,"restart",2048,nullptr,1,nullptr); return ESP_OK;
}
void portal_start(const Config &config) {
    saved=config;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG(); cfg.stack_size=8192; cfg.max_uri_handlers=10;
    httpd_handle_t server; ESP_ERROR_CHECK(httpd_start(&server,&cfg));
    httpd_uri_t root{}; root.uri="/"; root.method=HTTP_GET;
    root.handler=[](httpd_req_t *r)->esp_err_t {
        if(!allowed(r)) return ESP_OK;
        extern const char html[] asm("_binary_portal_html_start");
        httpd_resp_set_type(r,"text/html; charset=utf-8");
        httpd_resp_set_hdr(r,"Cache-Control","no-store");
        httpd_resp_set_hdr(r,"X-Frame-Options","DENY");
        return httpd_resp_sendstr(r,html);
    }; ESP_ERROR_CHECK(httpd_register_uri_handler(server,&root));
    httpd_uri_t state{}; state.uri="/state"; state.method=HTTP_GET;
    state.handler=[](httpd_req_t *r)->esp_err_t {
        if(!allowed(r)) return ESP_OK;
        auto ip=network_ip();
        cJSON *j=cJSON_CreateObject();
        cJSON_AddBoolToObject(j,"wifi_ready",!ip.empty());
        cJSON_AddBoolToObject(j,"ha_configured",saved.connectedSetup());
        cJSON_AddStringToObject(j,"ip",ip.c_str());
        cJSON_AddStringToObject(j,"ssid",saved.ssid.c_str());
        cJSON_AddStringToObject(j,"version",esp_app_get_description()->version);
        std::string url=saved.uri;
        if(url.rfind("wss://",0)==0) url.replace(0,6,"https://");
        else if(url.rfind("ws://",0)==0) url.replace(0,5,"http://");
        auto suffix=url.find("/api/websocket"); if(suffix!=std::string::npos) url.erase(suffix);
        cJSON_AddStringToObject(j,"ha_url",url.c_str());
        char *text=cJSON_PrintUnformatted(j);
        httpd_resp_set_type(r,"application/json"); httpd_resp_set_hdr(r,"Cache-Control","no-store");
        httpd_resp_sendstr(r,text); cJSON_free(text); cJSON_Delete(j); return ESP_OK;
    }; ESP_ERROR_CHECK(httpd_register_uri_handler(server,&state));
    httpd_uri_t wifi{}; wifi.uri="/wifi"; wifi.method=HTTP_POST;
    wifi.handler=[](httpd_req_t *r)->esp_err_t {
        cJSON *j=body(r); if(!j) return ESP_OK;
        Config next=saved; next.ssid=field(j,"ssid"); next.password=field(j,"password"); cJSON_Delete(j);
        if(next.ssid.empty() || next.ssid.size()>32 || next.password.size()>63 ||
           (!next.password.empty() && next.password.size()<8))
            return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Sprawdź nazwę i hasło Wi-Fi (8–63 znaki lub puste dla sieci otwartej)");
        return save_reply(r,next); // HA settings are preserved when changing Wi-Fi.
    }; ESP_ERROR_CHECK(httpd_register_uri_handler(server,&wifi));
    httpd_uri_t connection{}; connection.uri="/config"; connection.method=HTTP_POST;
    connection.handler=[](httpd_req_t *r)->esp_err_t {
        cJSON *j=body(r); if(!j) return ESP_OK;
        Config next=saved; next.uri=ha_websocket_uri(field(j,"uri"));
        auto token=field(j,"token"); if(!token.empty()) next.token=token;
        cJSON_Delete(j);
        if(!next.wifiConfigured() || next.uri.empty() || next.token.empty() || next.token.size()>2048)
            return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Podaj adres Home Assistant (http:// lub https://) i token");
        if(next.uri!=saved.uri) next.entity.clear();
        return save_reply(r,next);
    }; ESP_ERROR_CHECK(httpd_register_uri_handler(server,&connection));
    httpd_uri_t list{}; list.uri="/entities"; list.method=HTTP_GET;
    list.handler=[](httpd_req_t *r)->esp_err_t {
        if(!allowed(r)) return ESP_OK;
        cJSON *j=cJSON_CreateObject(), *a=cJSON_AddArrayToObject(j,"entities");
        for(const auto &entity:ha_entities()) {
            auto item=cJSON_CreateObject(); cJSON_AddStringToObject(item,"id",entity.id.c_str());
            cJSON_AddStringToObject(item,"name",entity.name.c_str()); cJSON_AddItemToArray(a,item);
        }
        cJSON_AddStringToObject(j,"status",ha_state().status.c_str());
        char *text=cJSON_PrintUnformatted(j); httpd_resp_set_type(r,"application/json");
        httpd_resp_set_hdr(r,"Cache-Control","no-store"); httpd_resp_sendstr(r,text);
        cJSON_free(text); cJSON_Delete(j); return ESP_OK;
    }; ESP_ERROR_CHECK(httpd_register_uri_handler(server,&list));
    httpd_uri_t select{}; select.uri="/select"; select.method=HTTP_POST;
    select.handler=[](httpd_req_t *r)->esp_err_t {
        cJSON *j=body(r); if(!j) return ESP_OK;
        auto id=field(j,"entity"); cJSON_Delete(j);
        bool found=false; for(auto &e:ha_entities()) if(e.id==id) found=true;
        if(!found || !ha_state().authenticated) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Wybierz termostat z aktualnej listy HA");
        Config next=saved; next.entity=id; return save_reply(r,next,true);
    }; ESP_ERROR_CHECK(httpd_register_uri_handler(server,&select));
    // Firmware upload: only while the portal runs (onboarding or 3 s knob hold).
    httpd_uri_t update{}; update.uri="/update"; update.method=HTTP_POST;
    update.handler=[](httpd_req_t *r)->esp_err_t {
        if(!allowed(r)) return ESP_OK;
        char type[40]{};
        httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type));
        auto partition=esp_ota_get_next_update_partition(nullptr);
        if(strcmp(type,"application/octet-stream") || !partition || r->content_len<=0 || r->content_len>partition->size)
            return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Wybierz plik firmware .bin (maks. 4 MB)");
        esp_ota_handle_t ota;
        if(esp_ota_begin(partition,OTA_WITH_SEQUENTIAL_WRITES,&ota)!=ESP_OK)
            return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Nie można rozpocząć aktualizacji");
        std::vector<char> buffer(4096);
        size_t left=r->content_len; int timeouts=0; esp_err_t err=ESP_OK;
        while(left && err==ESP_OK) {
            int n=httpd_req_recv(r,buffer.data(),std::min(left,buffer.size()));
            if(n==HTTPD_SOCK_ERR_TIMEOUT && ++timeouts<5) continue;
            if(n<=0) { err=ESP_FAIL; break; }
            timeouts=0; err=esp_ota_write(ota,buffer.data(),n); left-=n;
        }
        if(err!=ESP_OK) {
            esp_ota_abort(ota);
            return httpd_resp_send_err(r,HTTPD_500_INTERNAL_SERVER_ERROR,"Przerwano zapis firmware; panel działa na poprzedniej wersji");
        }
        // esp_ota_end validates the image; the project name rejects firmware for other devices.
        esp_app_desc_t next{};
        err=esp_ota_end(ota);
        if(err==ESP_OK) err=esp_ota_get_partition_description(partition,&next);
        if(err==ESP_OK && strcmp(next.project_name,esp_app_get_description()->project_name)) err=ESP_ERR_INVALID_VERSION;
        if(err==ESP_OK) err=esp_ota_set_boot_partition(partition);
        if(err!=ESP_OK)
            return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Plik nie jest poprawnym firmware tego panelu");
        std::string reply="Wgrano wersję "+std::string(next.version)+
            ". Panel uruchamia się ponownie; jeśli nowa wersja nie wystartuje, wróci poprzednia.";
        httpd_resp_sendstr(r,reply.c_str());
        xTaskCreate(reboot_task,"restart",2048,nullptr,1,nullptr); return ESP_OK;
    }; ESP_ERROR_CHECK(httpd_register_uri_handler(server,&update));
}
