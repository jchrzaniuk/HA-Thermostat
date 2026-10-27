#pragma once
#include <string>
#include "cJSON.h"

// Follows one entity through HA `subscribe_entities` events: the compressed
// initial state ("a"), attribute diffs ("c" with "+"/"-") and removals ("r").
// Only the selected entity is sent by HA, unlike a global state_changed stream.
class EntityTracker {
public:
    enum class Update { none, changed, removed };
    explicit EntityTracker(std::string id="") : id_(std::move(id)) {}
    ~EntityTracker() { cJSON_Delete(attributes_); }
    EntityTracker(const EntityTracker &)=delete;
    EntityTracker &operator=(const EntityTracker &)=delete;
    void track(std::string id) { reset(); id_=std::move(id); }
    void reset() { cJSON_Delete(attributes_); attributes_=nullptr; state_.clear(); received_=false; }
    bool present() const { return attributes_!=nullptr; }
    const std::string &state() const { return state_; }
    const cJSON *attributes() const { return attributes_; }
    Update apply(const cJSON *event) {
        bool first=!received_; received_=true;
        Update result=Update::none;
        const cJSON *added=item(cJSON_GetObjectItemCaseSensitive(event,"a"));
        if(cJSON_IsObject(added)) {
            reset(); received_=true;
            auto s=cJSON_GetObjectItemCaseSensitive(added,"s");
            state_=cJSON_IsString(s) ? s->valuestring : "";
            auto a=cJSON_GetObjectItemCaseSensitive(added,"a");
            attributes_=cJSON_IsObject(a) ? cJSON_Duplicate(a,true) : cJSON_CreateObject();
            result=Update::changed;
        }
        const cJSON *changed=item(cJSON_GetObjectItemCaseSensitive(event,"c"));
        if(cJSON_IsObject(changed) && present()) {
            auto plus=cJSON_GetObjectItemCaseSensitive(changed,"+");
            auto s=cJSON_GetObjectItemCaseSensitive(plus,"s");
            if(cJSON_IsString(s)) state_=s->valuestring;
            const cJSON *attribute=nullptr;
            cJSON_ArrayForEach(attribute,cJSON_GetObjectItemCaseSensitive(plus,"a")) {
                if(!attribute->string) continue;
                auto copy=cJSON_Duplicate(attribute,true);
                if(cJSON_HasObjectItem(attributes_,attribute->string))
                    cJSON_ReplaceItemInObjectCaseSensitive(attributes_,attribute->string,copy);
                else cJSON_AddItemToObject(attributes_,attribute->string,copy);
            }
            auto minus=cJSON_GetObjectItemCaseSensitive(changed,"-");
            cJSON_ArrayForEach(attribute,cJSON_GetObjectItemCaseSensitive(minus,"a"))
                if(cJSON_IsString(attribute)) cJSON_DeleteItemFromObjectCaseSensitive(attributes_,attribute->valuestring);
            result=Update::changed;
        }
        const cJSON *removed=nullptr;
        cJSON_ArrayForEach(removed,cJSON_GetObjectItemCaseSensitive(event,"r"))
            if(cJSON_IsString(removed) && id_==removed->valuestring) { reset(); received_=true; result=Update::removed; }
        // The first event lists every subscribed entity that exists.
        if(first && !present()) result=Update::removed;
        return result;
    }
private:
    const cJSON *item(const cJSON *group) const { return cJSON_GetObjectItemCaseSensitive(group,id_.c_str()); }
    std::string id_, state_;
    cJSON *attributes_=nullptr;
    bool received_=false;
};
