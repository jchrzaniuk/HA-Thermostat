#include "app.h"
#include "EntityTracker.h"
#include "TextMessageBuffer.h"
#include <mutex>
#include <atomic>
#include <cstring>
#include "cJSON.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {
constexpr int64_t commandTimeoutUs=8000000, noticeUs=3000000;
Config config;
PanelState state;
std::vector<EntityChoice> entities;
EntityTracker tracked;
bool listEntities=false;
std::mutex mutex;
esp_websocket_client_handle_t client=nullptr;
QueueHandle_t inbox;
struct Message { char *text; int event; };
std::atomic<bool> overflow{false};
TextMessageBuffer incoming(1024*1024);
int nextId=1, configId=0, statesId=0, subscribeId=0, pendingId=0;
int64_t pendingAt=0, noticeAt=0;
std::string steady; // Status shown once a notice expires.
std::string str(const cJSON *j,const char *key,const char *fallback="") {
    auto v=cJSON_GetObjectItemCaseSensitive(j,key);
    return cJSON_IsString(v) ? v->valuestring : fallback;
}
float number(const cJSON *j,const char *key,float fallback=NAN) {
    auto v=cJSON_GetObjectItemCaseSensitive(j,key);
    return cJSON_IsNumber(v) && std::isfinite(v->valuedouble) ? v->valuedouble : fallback;
}
bool send(cJSON *j) {
    char *text=cJSON_PrintUnformatted(j); cJSON_Delete(j);
    if(!text) return false;
    int len=strlen(text);
    int sent=esp_websocket_client_send_text(client,text,len,pdMS_TO_TICKS(500));
    cJSON_free(text); return sent==len;
}
cJSON *request(const char *type,int &id) {
    auto j=cJSON_CreateObject(); id=nextId++;
    cJSON_AddNumberToObject(j,"id",id); cJSON_AddStringToObject(j,"type",type);
    return j;
}
int command(const char *type) {
    int id; return send(request(type,id)) ? id : 0;
}
void status(const char *text) { steady=text; if(!noticeAt) state.status=text; }
// Short-lived status; ha_poll() returns to the steady status afterwards.
void notice(const char *text) { state.status=text; noticeAt=esp_timer_get_time(); }
void invalidate(const char *reason) {
    state.authenticated=false; state.climate.available=false;
    noticeAt=0; status(reason); pendingId=0; state.commandPending=false;
    entities.clear(); tracked.reset();
}
void parse_state(const std::string &value,const cJSON *a) {
    state.mode=value; state.action=str(a,"hvac_action");
    state.name=str(a,"friendly_name",config.entity.c_str());
    state.climate.available=state.mode!="unavailable" && state.mode!="unknown" && !state.mode.empty();
    state.climate.current=number(a,"current_temperature"); state.climate.target=number(a,"temperature");
    state.climate.minimum=number(a,"min_temp",NAN); state.climate.maximum=number(a,"max_temp",NAN);
    state.climate.step=number(a,"target_temp_step",number(a,"target_temperature_step",state.unit=="°F" ? 1 : 0.5f));
    int features=(int)number(a,"supported_features",0);
    state.climate.singleTarget=(features & 1)!=0;
    state.preset=str(a,"preset_mode");
    state.presets.clear(); cJSON *preset=nullptr;
    if(features & 16) { // ClimateEntityFeature.PRESET_MODE
        cJSON_ArrayForEach(preset,cJSON_GetObjectItemCaseSensitive(a,"preset_modes"))
            if(cJSON_IsString(preset)) state.presets.emplace_back(preset->valuestring);
    }
    state.modes.clear(); cJSON *m=nullptr;
    cJSON_ArrayForEach(m,cJSON_GetObjectItemCaseSensitive(a,"hvac_modes")) if(cJSON_IsString(m)) state.modes.emplace_back(m->valuestring);
    status(state.climate.available ? statusConnected : "Termostat niedostępny");
}
void handle(const char *text) {
    cJSON *j=cJSON_Parse(text); if(!j) { invalidate("Niepoprawna odpowiedź HA"); return; }
    auto type=str(j,"type");
    if(type=="auth_required") {
        nextId=1; auto auth=cJSON_CreateObject(); cJSON_AddStringToObject(auth,"type","auth");
        cJSON_AddStringToObject(auth,"access_token",config.token.c_str()); send(auth);
    } else if(type=="auth_invalid") invalidate("Nieprawidłowy token HA");
    else if(type=="auth_ok") {
        state.authenticated=true; noticeAt=0; status("Pobieranie danych HA");
        configId=command("get_config"); // Temperature unit belongs to the HA configuration.
        if(!config.entity.empty()) {
            auto subscription=request("subscribe_entities",subscribeId);
            auto ids=cJSON_AddArrayToObject(subscription,"entity_ids");
            cJSON_AddItemToArray(ids,cJSON_CreateString(config.entity.c_str()));
            if(!send(subscription)) subscribeId=0;
        }
        // The full state dump can be large; it is only needed to pick an entity.
        if(listEntities) statesId=command("get_states");
    } else if(type=="result") {
        int id=(int)number(j,"id",0); bool success=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"success"));
        auto result=cJSON_GetObjectItemCaseSensitive(j,"result");
        if(id==configId && success) {
            auto units=cJSON_GetObjectItemCaseSensitive(result,"unit_system"); state.unit=str(units,"temperature","°C");
        } else if(id==statesId) {
            entities.clear();
            if(success && cJSON_IsArray(result)) {
                cJSON *item=nullptr;
                cJSON_ArrayForEach(item,result) {
                    auto entity=str(item,"entity_id"); if(entity.rfind("climate.",0)!=0) continue;
                    auto attributes=cJSON_GetObjectItemCaseSensitive(item,"attributes");
                    entities.push_back({entity,str(attributes,"friendly_name",entity.c_str())});
                }
                if(config.entity.empty()) status("Wybierz termostat");
            } else invalidate("Błąd pobierania encji");
        } else if(id==subscribeId && !success) invalidate("Błąd subskrypcji HA");
        else if(id==pendingId) {
            pendingId=0; state.commandPending=false;
            // The subscription delivers the authoritative state if HA applies it.
            notice(success ? "Zapisano w HA" : "HA odrzucił polecenie");
        }
    } else if(type=="event" && (int)number(j,"id",0)==subscribeId && subscribeId) {
        switch(tracked.apply(cJSON_GetObjectItemCaseSensitive(j,"event"))) {
        case EntityTracker::Update::changed: parse_state(tracked.state(),tracked.attributes()); break;
        case EntityTracker::Update::removed: state.climate.available=false; status("Brak encji w HA"); break;
        case EntityTracker::Update::none: break;
        }
    }
    cJSON_Delete(j);
}
void ws_event(void *,esp_event_base_t,int32_t event,void *data) {
    Message message{};
    if(event==WEBSOCKET_EVENT_DISCONNECTED) { incoming.clear(); message.event=1; }
    else if(event==WEBSOCKET_EVENT_DATA) {
        auto d=(esp_websocket_event_data_t*)data;
        auto result=incoming.feed(d->op_code,d->data_ptr,d->data_len,
                                  d->payload_offset,d->payload_len,d->fin);
        if(result==TextMessageBuffer::Result::invalid) { overflow=true; return; }
        if(result!=TextMessageBuffer::Result::complete) return;
        message.text=strdup(incoming.text().c_str()); incoming.clear();
        if(!message.text) { overflow=true; return; }
    } else return;
    if(xQueueSend(inbox,&message,0)!=pdTRUE) { free(message.text); overflow=true; }
}
bool service(const char *name,const char *key,const char *value,float temperature) {
    if(!state.authenticated || !state.climate.available || pendingId || !esp_websocket_client_is_connected(client)) return false;
    auto j=request("call_service",pendingId);
    cJSON_AddStringToObject(j,"domain","climate"); cJSON_AddStringToObject(j,"service",name);
    auto target=cJSON_AddObjectToObject(j,"target"); cJSON_AddStringToObject(target,"entity_id",config.entity.c_str());
    auto data=cJSON_AddObjectToObject(j,"service_data");
    if(value) cJSON_AddStringToObject(data,key,value); else cJSON_AddNumberToObject(data,key,temperature);
    if(!send(j)) { pendingId=0; notice("Błąd wysyłania"); return false; }
    pendingAt=esp_timer_get_time(); state.commandPending=true; state.status="Wysyłanie do HA"; noticeAt=0; return true;
}
}
void ha_start(const Config &c,bool setup) {
    config=c; listEntities=setup; inbox=xQueueCreate(8,sizeof(Message)); configASSERT(inbox);
    tracked.track(config.entity);
    if(!c.connectedSetup()) { status("Skonfiguruj połączenie"); return; }
    esp_websocket_client_config_t cfg{};
    cfg.uri=config.uri.c_str(); cfg.crt_bundle_attach=esp_crt_bundle_attach;
    cfg.reconnect_timeout_ms=5000; cfg.network_timeout_ms=2000; cfg.buffer_size=4096;
    cfg.task_stack=8192; cfg.ping_interval_sec=10; cfg.pingpong_timeout_sec=10;
    client=esp_websocket_client_init(&cfg); configASSERT(client);
    ESP_ERROR_CHECK(esp_websocket_register_events(client,WEBSOCKET_EVENT_ANY,ws_event,nullptr));
    ESP_ERROR_CHECK(esp_websocket_client_start(client));
}
void ha_poll() {
    std::lock_guard<std::mutex> guard(mutex);
    if(overflow.exchange(false)) {
        invalidate("Przeciążenie połączenia");
        if(client) { esp_websocket_client_stop(client); }
        Message old{}; while(xQueueReceive(inbox,&old,0)==pdTRUE) free(old.text);
        if(client) esp_websocket_client_start(client);
    }
    Message m{};
    while(xQueueReceive(inbox,&m,0)==pdTRUE) {
        if(m.event==1) invalidate("Brak połączenia z HA"); else if(m.text) handle(m.text);
        free(m.text);
    }
    int64_t now=esp_timer_get_time();
    if(pendingId && now-pendingAt>commandTimeoutUs) {
        pendingId=0; state.commandPending=false; notice("Brak potwierdzenia HA");
    }
    if(noticeAt && now-noticeAt>noticeUs) { noticeAt=0; state.status=steady; }
}
PanelState ha_state() { std::lock_guard<std::mutex> guard(mutex); return state; }
std::vector<EntityChoice> ha_entities() { std::lock_guard<std::mutex> guard(mutex); return entities; }
bool ha_set_temperature(float t) {
    std::lock_guard<std::mutex> guard(mutex);
    if(!state.climate.adjustable() || !std::isfinite(t) || t<state.climate.minimum || t>state.climate.maximum) return false;
    return service("set_temperature","temperature",nullptr,t);
}
bool ha_set_mode(const std::string &mode) {
    std::lock_guard<std::mutex> guard(mutex);
    if(std::find(state.modes.begin(),state.modes.end(),mode)==state.modes.end()) return false;
    return service("set_hvac_mode","hvac_mode",mode.c_str(),0);
}

bool ha_set_preset(const std::string &preset) {
    std::lock_guard<std::mutex> guard(mutex);
    if(std::find(state.presets.begin(),state.presets.end(),preset)==state.presets.end()) return false;
    return service("set_preset_mode","preset_mode",preset.c_str(),0);
}
