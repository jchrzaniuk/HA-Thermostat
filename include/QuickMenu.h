#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "ClimateOptions.h"

// Nest-style two-level menu driven by the knob: a row of icons (back, mode,
// profile, eco), then a short list of options. Turning never sends anything;
// only a press returns a command. Hardware-independent for host tests.
enum class MenuIcon { back, heat, cool, off, automatic, preset, eco };
inline MenuIcon mode_icon(const std::string &mode) {
    if(mode=="heat") return MenuIcon::heat;
    if(mode=="cool") return MenuIcon::cool;
    if(mode=="off") return MenuIcon::off;
    return MenuIcon::automatic;
}
struct MenuEntry {
    MenuIcon icon;
    std::string label, detail, value;
    bool active=false; // Currently set in HA.
};
struct MenuCommand {
    enum Kind { none, close, mode, preset } kind=none;
    std::string value;
};

class QuickMenu {
public:
    enum class Level { closed, top, list };
    Level level() const { return level_; }
    bool isOpen() const { return level_!=Level::closed; }
    size_t selected() const { return selected_; }
    const std::vector<MenuEntry> &entries() const { return entries_; }
    const std::string &heading() const { return heading_; }

    void open(const std::vector<std::string> &modes,const std::vector<std::string> &presets,
              const std::string &mode,const std::string &preset) {
        modes_=modes; presets_=presets; mode_=mode; preset_=preset;
        show_top(1);
    }
    void close() { level_=Level::closed; entries_.clear(); heading_.clear(); }
    void rotate(int ticks) {
        if(!isOpen() || entries_.empty()) return;
        long next=long(selected_)+ticks;
        selected_=size_t(std::max(0L,std::min(long(entries_.size())-1,next)));
    }
    MenuCommand press() {
        if(!isOpen() || selected_>=entries_.size()) return {};
        if(level_==Level::top) {
            switch(items_[selected_]) {
            case Item::back: close(); return {MenuCommand::close,""};
            case Item::modes: show_list(false); return {};
            case Item::presets: show_list(true); return {};
            case Item::eco:
                if(preset_!="eco") return {MenuCommand::preset,"eco"};
                if(has(presets_,"none")) return {MenuCommand::preset,"none"};
                show_list(true); return {};
            }
        }
        if(selected_==0) { show_top(topIndex_); return {}; }
        return {listOfPresets_ ? MenuCommand::preset : MenuCommand::mode,entries_[selected_].value};
    }
private:
    enum class Item { back, modes, presets, eco };
    static bool has(const std::vector<std::string> &values,const std::string &value) {
        return std::find(values.begin(),values.end(),value)!=values.end();
    }
    void show_top(size_t select) {
        level_=Level::top; heading_.clear(); entries_.clear(); items_.clear();
        entries_.push_back({MenuIcon::back,"Wróć","",""}); items_.push_back(Item::back);
        if(!modes_.empty()) {
            entries_.push_back({mode_icon(mode_),"Tryb",hvac_mode_label(mode_),""});
            items_.push_back(Item::modes);
        }
        if(!presets_.empty()) {
            bool none=preset_.empty() || preset_=="none";
            entries_.push_back({MenuIcon::preset,"Profil",none ? "Brak" : preset_label(preset_),""});
            items_.push_back(Item::presets);
        }
        if(has(presets_,"eco")) {
            entries_.push_back({MenuIcon::eco,"Eco",preset_=="eco" ? "Włączone" : "Wyłączone",""});
            items_.push_back(Item::eco);
        }
        selected_=std::min(select,entries_.size()-1);
    }
    void show_list(bool presets) {
        topIndex_=selected_; listOfPresets_=presets; level_=Level::list;
        heading_=presets ? "PROFIL" : "TRYB";
        entries_.clear(); entries_.push_back({MenuIcon::back,"Wróć","",""});
        selected_=1;
        for(const auto &value:presets ? presets_ : modes_) {
            bool active=value==(presets ? preset_ : mode_);
            if(active) selected_=entries_.size();
            if(presets) entries_.push_back({value=="eco" ? MenuIcon::eco : MenuIcon::preset,preset_label(value),"",value,active});
            else entries_.push_back({mode_icon(value),hvac_mode_label(value),"",value,active});
        }
        selected_=std::min(selected_,entries_.size()-1);
    }
    Level level_=Level::closed;
    std::vector<MenuEntry> entries_;
    std::vector<Item> items_;
    std::vector<std::string> modes_, presets_;
    std::string mode_, preset_, heading_;
    size_t selected_=0, topIndex_=1;
    bool listOfPresets_=false;
};
