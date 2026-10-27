#include "app.h"
#include "BoardConfig.h"
#include "ClimateOptions.h"
#include "WifiQr.h"
#include "ui_assets.h"
#include <cmath>
#include <cstring>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

namespace {
// Nest-style dial: ticks over 270° with the gap at the bottom, one tick per
// setpoint step (one knob detent); ticks between the current and target
// temperature are lit, the target has a long marker.
constexpr int centre=120, maxTicks=90, idleTicks=61, tickOuter=113, tickInner=101, litInner=96;
constexpr int markerInner=86, currentRadius=79;
constexpr float startAngle=135, sweep=270, currentOffset=9;
constexpr uint32_t white=0xffffff, grey=0x7d848d;
constexpr uint32_t heatingColour=0xe8620c, coolingColour=0x1f7fe0, idleColour=0x000000, leafColour=0x5fd13b;
lv_obj_t *dial,*target,*fraction,*title,*current,*status,*leaf;
lv_obj_t *setupQrBackground=nullptr;
lv_obj_t *menuHeading,*menuIcons[3],*menuLabel,*menuDetail,*menuPill,*menuRows[3];
bool menuShown=false;
struct DialState {
    float minimum=NAN, maximum=NAN, step=NAN, target=NAN, current=NAN;
    bool online=false;
    bool operator==(const DialState &o) const {
        auto same=[](float a,float b) { return a==b || (std::isnan(a) && std::isnan(b)); };
        return same(minimum,o.minimum) && same(maximum,o.maximum) && same(step,o.step) &&
               same(target,o.target) && same(current,o.current) && online==o.online;
    }
} dialState;
lv_obj_t *label(lv_obj_t *parent,const lv_font_t *font,int y) {
    auto obj=lv_label_create(parent);
    lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(white),0);
    lv_obj_set_width(obj,180); lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);
    lv_obj_align(obj,LV_ALIGN_CENTER,0,y); return obj;
}
// display_update() runs every 100 ms; only real changes may invalidate the LCD.
void set_text(lv_obj_t *obj,const char *text) {
    if(strcmp(lv_label_get_text(obj),text)) lv_label_set_text(obj,text);
}
void set_text_color(lv_obj_t *obj,uint32_t color) {
    if(lv_obj_get_style_text_color(obj,0).full!=lv_color_hex(color).full)
        lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
}
bool has_range() {
    return std::isfinite(dialState.minimum) && std::isfinite(dialState.maximum) &&
           dialState.maximum>dialState.minimum;
}
float angle_of(float value) {
    float part=(value-dialState.minimum)/(dialState.maximum-dialState.minimum);
    return startAngle+sweep*std::max(0.0f,std::min(1.0f,part));
}
lv_point_t polar(float degrees,int radius) {
    float radians=degrees*float(M_PI)/180;
    return {lv_coord_t(centre+std::lround(radius*std::cos(radians))),
            lv_coord_t(centre+std::lround(radius*std::sin(radians)))};
}
void tick(lv_draw_ctx_t *ctx,lv_draw_line_dsc_t &line,float degrees,int inner) {
    auto a=polar(degrees,inner), b=polar(degrees,tickOuter);
    lv_draw_line(ctx,&line,&a,&b);
}
void draw_dial(lv_event_t *e) {
    auto ctx=lv_event_get_draw_ctx(e);
    const auto &d=dialState;
    bool range=has_range();
    float low=NAN, high=NAN;
    if(range && d.online && std::isfinite(d.target) && std::isfinite(d.current)) {
        low=angle_of(std::min(d.target,d.current)); high=angle_of(std::max(d.target,d.current));
    }
    lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
    line.color=lv_color_hex(white);
    line.round_start=line.round_end=1;
    bool stepped=range && std::isfinite(d.step) && d.step>0;
    int steps=stepped ? int(std::lround((d.maximum-d.minimum)/d.step)) : idleTicks-1;
    // Very fine steps would crowd the ring; then every n-th step gets a tick.
    int stride=std::max(1,(steps+maxTicks-1)/maxTicks);
    for(int i=0;i<=steps;i+=stride) {
        float degrees=stepped ? angle_of(d.minimum+i*d.step) : startAngle+sweep*i/steps;
        bool lit=degrees>=low-0.01f && degrees<=high+0.01f;
        line.width=lit ? 3 : 2; line.opa=lit ? LV_OPA_COVER : d.online ? LV_OPA_50 : LV_OPA_30;
        tick(ctx,line,degrees,lit ? litInner : tickInner);
    }
    if(!range || !d.online) return;
    line.opa=LV_OPA_COVER;
    if(std::isfinite(d.current)) { line.width=4; tick(ctx,line,angle_of(d.current),litInner-2); }
    if(std::isfinite(d.target)) { line.width=5; tick(ctx,line,angle_of(d.target),markerInner); }
}
// Current temperature sits beside its tick, on the side away from the target marker.
void place_current() {
    const auto &d=dialState;
    if(!has_range() || !d.online || !std::isfinite(d.current)) {
        lv_obj_add_flag(current,LV_OBJ_FLAG_HIDDEN); return;
    }
    float degrees=angle_of(d.current);
    if(std::isfinite(d.target)) degrees+=d.current<d.target ? -currentOffset : currentOffset;
    auto point=polar(degrees,currentRadius);
    lv_obj_align(current,LV_ALIGN_CENTER,point.x-centre,point.y-centre);
    lv_obj_clear_flag(current,LV_OBJ_FLAG_HIDDEN);
}
const char *activity(const PanelState &s,bool online) {
    if(!online) return "";
    if(s.mode=="off") return "WYŁĄCZONY";
    if(s.action=="heating" || s.action=="preheating") return "GRZANIE";
    if(s.action=="cooling") return "CHŁODZENIE";
    if(s.action=="drying") return "OSUSZANIE";
    if(s.action=="fan") return "WENTYLACJA";
    return "NASTAWA";
}
void show(lv_obj_t *obj,bool visible) {
    if(visible==lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))
        visible ? lv_obj_clear_flag(obj,LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
}
const lv_img_dsc_t *icon(MenuIcon kind,bool large) {
    static const lv_img_dsc_t *const icons[][2]={
        {&ui_icon_back_28,&ui_icon_back_48},{&ui_icon_heat_28,&ui_icon_heat_48},
        {&ui_icon_cool_28,&ui_icon_cool_48},{&ui_icon_off_28,&ui_icon_off_48},
        {&ui_icon_auto_28,&ui_icon_auto_48},{&ui_icon_preset_28,&ui_icon_preset_48},
        {&ui_icon_eco_28,&ui_icon_eco_48}};
    return icons[int(kind)][large];
}
void set_icon(lv_obj_t *obj,const lv_img_dsc_t *src) {
    if(lv_img_get_src(obj)!=src) lv_img_set_src(obj,src);
}
lv_obj_t *menu_icon(lv_obj_t *parent,int x,lv_opa_t opa) {
    auto obj=lv_img_create(parent);
    lv_obj_set_style_img_recolor(obj,lv_color_hex(white),0);
    lv_obj_set_style_img_recolor_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_img_opa(obj,opa,0);
    lv_obj_align(obj,LV_ALIGN_CENTER,x,-18); lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
    return obj;
}
// Top level: the selected icon large in the middle, neighbours small at the sides.
// List level: three rows, the selected one on a white pill; ✓ marks the HA state.
void render_menu(const QuickMenu &menu) {
    bool top=menu.level()==QuickMenu::Level::top;
    const auto &entries=menu.entries();
    size_t selected=menu.selected();
    set_text(menuHeading,menu.heading().c_str());
    for(int i=0;i<3;++i) {
        long index=long(selected)+i-1;
        bool exists=index>=0 && index<long(entries.size());
        show(menuIcons[i],top && exists);
        if(top && exists) set_icon(menuIcons[i],icon(entries[index].icon,i==1));
        show(menuRows[i],!top && exists);
        if(!top && exists) {
            std::string text=entries[index].label;
            if(entries[index].active) text+="  " LV_SYMBOL_OK;
            set_text(menuRows[i],text.c_str());
        }
    }
    show(menuLabel,top); show(menuDetail,top); show(menuPill,!top);
    if(top) {
        set_text(menuLabel,entries[selected].label.c_str());
        set_text(menuDetail,entries[selected].detail.c_str());
    }
}
void show_home(bool visible) {
    for(auto obj:{dial,title,target,fraction,status}) show(obj,visible);
    if(!visible) for(auto obj:{current,leaf}) show(obj,false);
    show(menuHeading,!visible);
    if(!visible) return;
    for(auto obj:{menuIcons[0],menuIcons[1],menuIcons[2],menuLabel,menuDetail,menuPill,
                  menuRows[0],menuRows[1],menuRows[2]}) show(obj,false);
    place_current();
}
void hide_dial() {
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(idleColour),0);
    for(auto obj:{dial,fraction,current,leaf}) lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
}
}
void display_init() {
    spi_bus_config_t bus{}; bus.sclk_io_num=board::lcdClock; bus.mosi_io_num=board::lcdMosi; bus.miso_io_num=-1;
    bus.quadwp_io_num=-1; bus.quadhd_io_num=-1; bus.max_transfer_sz=240*40*2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io{}; io.cs_gpio_num=board::lcdCs; io.dc_gpio_num=board::lcdDc;
    io.spi_mode=0; io.pclk_hz=board::spiClockHz; io.trans_queue_depth=10;
    io.lcd_cmd_bits=8; io.lcd_param_bits=8;
    esp_lcd_panel_io_handle_t ioHandle;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&ioHandle));
    esp_lcd_panel_dev_config_t panelConfig{}; panelConfig.reset_gpio_num=board::lcdReset;
    panelConfig.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR; panelConfig.bits_per_pixel=16;
    esp_lcd_panel_handle_t panel;
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(ioHandle,&panelConfig,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel)); ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
    backlight_init(100);
    lvgl_port_cfg_t port=ESP_LVGL_PORT_INIT_CONFIG(); ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t cfg{}; cfg.io_handle=ioHandle; cfg.panel_handle=panel;
    cfg.buffer_size=240*40; cfg.double_buffer=true; cfg.hres=board::width; cfg.vres=board::height; cfg.flags.buff_dma=true;
    cfg.rotation.mirror_x=board::mirrorX; cfg.rotation.mirror_y=board::mirrorY;
    cfg.rotation.swap_xy=board::swapXY;
    configASSERT(lvgl_port_add_disp(&cfg));
    lvgl_port_lock(0);
    auto screen=lv_scr_act();
    lv_obj_set_style_bg_color(screen,lv_color_hex(idleColour),0);
    lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    dial=lv_obj_create(screen); lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial,board::width,board::height); lv_obj_center(dial);
    lv_obj_clear_flag(dial,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dial,draw_dial,LV_EVENT_DRAW_MAIN,nullptr);
    title=label(screen,&lv_font_pl_14,-46);
    lv_obj_set_style_text_letter_space(title,1,0);
    target=label(screen,&lv_font_temp_80,6);
    lv_obj_set_width(target,LV_SIZE_CONTENT);
    fraction=label(screen,&lv_font_pl_20,0);
    lv_obj_set_width(fraction,LV_SIZE_CONTENT);
    current=label(screen,&lv_font_pl_14,0);
    lv_obj_set_width(current,LV_SIZE_CONTENT);
    leaf=lv_img_create(screen); lv_img_set_src(leaf,&ui_leaf);
    lv_obj_set_style_img_recolor(leaf,lv_color_hex(leafColour),0);
    lv_obj_set_style_img_recolor_opa(leaf,LV_OPA_COVER,0);
    lv_obj_align(leaf,LV_ALIGN_CENTER,0,54); lv_obj_add_flag(leaf,LV_OBJ_FLAG_HIDDEN);
    status=label(screen,&lv_font_pl_14,80);
    lv_obj_set_width(status,160); // Chord of the round LCD at this height.
    menuHeading=label(screen,&lv_font_pl_14,-86);
    lv_obj_set_style_text_letter_space(menuHeading,1,0);
    menuIcons[0]=menu_icon(screen,-76,LV_OPA_50);
    menuIcons[1]=menu_icon(screen,0,LV_OPA_COVER);
    menuIcons[2]=menu_icon(screen,76,LV_OPA_50);
    menuLabel=label(screen,&lv_font_pl_20,30);
    menuDetail=label(screen,&lv_font_pl_14,54);
    lv_obj_set_style_text_opa(menuDetail,LV_OPA_80,0);
    menuPill=lv_obj_create(screen); lv_obj_remove_style_all(menuPill);
    lv_obj_set_size(menuPill,176,38); lv_obj_center(menuPill);
    lv_obj_set_style_radius(menuPill,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(menuPill,lv_color_hex(white),0);
    lv_obj_set_style_bg_opa(menuPill,LV_OPA_COVER,0);
    for(int i=0;i<3;++i) {
        menuRows[i]=label(screen,&lv_font_pl_20,(i-1)*44);
        lv_obj_set_width(menuRows[i],160);
        if(i!=1) lv_obj_set_style_text_opa(menuRows[i],LV_OPA_70,0);
    }
    lv_obj_set_style_text_color(menuRows[1],lv_color_hex(idleColour),0);
    show_home(true);
    lvgl_port_unlock();
}
void display_update(const PanelState &s,float preview,const char *message,const QuickMenu *menu) {
    if(!lvgl_port_lock(50)) return;
    bool online=s.authenticated && s.climate.available, off=online && s.mode=="off";
    bool heating=online && !off && (s.action=="heating" || s.action=="preheating");
    bool cooling=online && !off && s.action=="cooling";
    auto background=lv_color_hex(heating ? heatingColour : cooling ? coolingColour : idleColour);
    if(lv_obj_get_style_bg_color(lv_scr_act(),0).full!=background.full)
        lv_obj_set_style_bg_color(lv_scr_act(),background,0);
    bool menuOpen=menu && menu->isOpen();
    if(menuOpen!=menuShown) { menuShown=menuOpen; show_home(!menuOpen); }
    if(menuOpen) { render_menu(*menu); lvgl_port_unlock(); return; }
    float value=std::isfinite(preview) ? preview : s.climate.target;
    DialState next{s.climate.minimum,s.climate.maximum,s.climate.step,value,s.climate.current,online};
    if(!(next==dialState)) { dialState=next; lv_obj_invalidate(dial); place_current(); }
    set_text(title,activity(s,online));
    // Nest style: whole degrees large, the tenth as a small raised digit.
    char text[48]="--", tenth[8]="";
    if(std::isfinite(value)) {
        snprintf(text,sizeof(text),"%.*f",s.climate.decimals(value),double(value));
        if(auto dot=strchr(text,'.')) {
            if(strcmp(dot,".0")) snprintf(tenth,sizeof(tenth),"%s",dot);
            *dot=0;
        }
    }
    set_text(target,text); set_text(fraction,tenth);
    set_text_color(target,online && !off ? white : grey);
    set_text_color(fraction,online && !off ? white : grey);
    lv_obj_align_to(fraction,target,LV_ALIGN_OUT_RIGHT_TOP,1,18);
    if(std::isfinite(s.climate.current)) {
        snprintf(text,sizeof(text),"%.*f",s.climate.decimals(s.climate.current),double(s.climate.current));
        set_text(current,text);
    }
    bool eco=online && s.preset=="eco";
    if(eco==lv_obj_has_flag(leaf,LV_OBJ_FLAG_HIDDEN))
        eco ? lv_obj_clear_flag(leaf,LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(leaf,LV_OBJ_FLAG_HIDDEN);
    std::string detail=s.status;
    if(online && s.status==statusConnected)
        detail=!s.preset.empty() && s.preset!="none" && !eco ? preset_label(s.preset) : s.name;
    set_text(status,message && *message ? message : detail.c_str());
    lvgl_port_unlock();
}
void display_setup(const char *ssid,const char *password) {
    lvgl_port_lock(0);
    hide_dial();
    lv_obj_add_flag(target,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_letter_space(title,0,0);
    lv_obj_align(title,LV_ALIGN_CENTER,0,-102);
    lv_label_set_text(title,"Połącz z Wi-Fi");
    lv_obj_align(status,LV_ALIGN_CENTER,0,102);
    lv_label_set_text(status,"192.168.4.1");
    // Four white modules around a 132 px QR; the 164 px square fits the round LCD.
    if(setupQrBackground) lv_obj_del(setupQrBackground);
    auto background=lv_obj_create(lv_scr_act());
    setupQrBackground=background;
    lv_obj_remove_style_all(background);
    lv_obj_set_size(background,164,164); lv_obj_center(background);
    lv_obj_set_style_bg_color(background,lv_color_white(),0);
    lv_obj_set_style_bg_opa(background,LV_OPA_COVER,0);
    lv_obj_clear_flag(background,LV_OBJ_FLAG_SCROLLABLE);
    auto qr=lv_qrcode_create(background,132,lv_color_black(),lv_color_white());
    auto payload=wifi_qr_payload(ssid,password);
    if(qr && lv_qrcode_update(qr,payload.data(),payload.size())==LV_RES_OK) {
        lv_obj_center(qr);
    } else {
        lv_obj_del(background); setupQrBackground=nullptr;
        for(auto obj:{target,fraction}) {
            lv_obj_clear_flag(obj,LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_font(obj,&lv_font_pl_20,0); lv_obj_set_width(obj,180);
        }
        lv_obj_align(target,LV_ALIGN_CENTER,0,-8); lv_obj_align(fraction,LV_ALIGN_CENTER,0,32);
        lv_label_set_text(target,ssid); lv_label_set_text(fraction,password);
    }
    lvgl_port_unlock();
}

void display_network(const std::string &ip) {
    lvgl_port_lock(0);
    if(setupQrBackground) { lv_obj_del(setupQrBackground); setupQrBackground=nullptr; }
    hide_dial();
    for(auto obj:{target,fraction,current}) {
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_HIDDEN); lv_obj_set_width(obj,180);
        lv_obj_set_style_text_color(obj,lv_color_white(),0);
    }
    lv_obj_set_style_text_letter_space(title,0,0);
    lv_obj_set_style_text_font(title,&lv_font_pl_20,0);
    lv_obj_align(title,LV_ALIGN_CENTER,0,-62);
    lv_obj_align(status,LV_ALIGN_CENTER,0,87);
    lv_obj_set_style_text_font(target,&lv_font_pl_20,0);
    lv_obj_set_style_text_font(fraction,&lv_font_pl_14,0);
    lv_obj_align(target,LV_ALIGN_CENTER,0,-8);
    lv_obj_align(fraction,LV_ALIGN_CENTER,0,32);
    lv_obj_align(current,LV_ALIGN_CENTER,0,60);
    lv_label_set_text(title,ip.empty() ? "Łączenie z Wi-Fi" : "Home Assistant");
    lv_label_set_text(target,ip.empty() ? "Proszę czekać" : ip.c_str());
    lv_label_set_text(fraction,ip.empty() ? "" : "Otwórz przez http://");
    lv_label_set_text(current,ip.empty() ? "Sprawdzanie połączenia" : "na komputerze");
    lv_label_set_text(status,ip.empty() ? "Ponowienie za 30 s" : "w tej samej sieci");
    lvgl_port_unlock();
}
