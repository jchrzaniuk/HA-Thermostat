#include "EntityTracker.h"
#include <cassert>
#include <cstring>
using U=EntityTracker::Update;
static U apply(EntityTracker &t,const char *json) {
    cJSON *event=cJSON_Parse(json); assert(event);
    auto result=t.apply(event); cJSON_Delete(event); return result;
}
static double number(const EntityTracker &t,const char *key) {
    return cJSON_GetObjectItemCaseSensitive(t.attributes(),key)->valuedouble;
}
int main() {
    EntityTracker t("climate.room");
    assert(apply(t,R"({"a":{"climate.room":{"s":"heat","a":{"temperature":21,"hvac_action":"idle"},"lc":1}}})")==U::changed);
    assert(t.present() && t.state()=="heat" && number(t,"temperature")==21);
    assert(apply(t,R"({"c":{"climate.room":{"+":{"a":{"temperature":21.5,"preset_mode":"eco"},"lu":2}}}})")==U::changed);
    assert(t.state()=="heat" && number(t,"temperature")==21.5);
    assert(cJSON_HasObjectItem(t.attributes(),"preset_mode"));
    assert(apply(t,R"({"c":{"climate.room":{"+":{"s":"off"},"-":{"a":["preset_mode"]}}}})")==U::changed);
    assert(t.state()=="off" && !cJSON_HasObjectItem(t.attributes(),"preset_mode"));
    assert(apply(t,R"({"c":{"climate.other":{"+":{"s":"heat"}}}})")==U::none);
    assert(apply(t,R"({"r":["climate.room"]})")==U::removed && !t.present());
    // A diff without a known base state is ignored rather than invented.
    assert(apply(t,R"({"c":{"climate.room":{"+":{"s":"heat"}}}})")==U::none && !t.present());
    // The first event after (re)subscribing reports a missing entity.
    t.reset();
    assert(apply(t,R"({"a":{}})")==U::removed);
    assert(apply(t,R"({"a":{"climate.room":{"s":"auto"}}})")==U::changed);
    assert(t.state()=="auto" && t.attributes() && !t.attributes()->child);
}
