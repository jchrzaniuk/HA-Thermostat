#include "app.h"
#include "BoardConfig.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
// Timings in microseconds (esp_timer_get_time).
constexpr int64_t debounceUs=30000, holdHintUs=1000000, setupHoldUs=3000000;
constexpr int64_t sendDelayUs=650000, menuTimeoutUs=15000000, wifiRetryUs=30000000, renderUs=100000;
constexpr int64_t idleDimUs=30000000, firmwareConfirmUs=15000000;
constexpr int activeBrightness=100, idleBrightness=20, wakeFadeMs=150, dimFadeMs=1000;
portMUX_TYPE encoderLock=portMUX_INITIALIZER_UNLOCKED;
volatile int transitions=0;
volatile uint8_t previous=0;
void IRAM_ATTR encoder_isr(void *) {
    static const int8_t table[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    uint8_t now=(gpio_get_level(static_cast<gpio_num_t>(board::encoderA))<<1)|gpio_get_level(static_cast<gpio_num_t>(board::encoderB));
    portENTER_CRITICAL_ISR(&encoderLock);
    transitions+=(board::encoderInverted ? -1 : 1)*table[(previous<<2)|now]; previous=now;
    portEXIT_CRITICAL_ISR(&encoderLock);
}
void input_init() {
    gpio_config_t cfg{}; cfg.pin_bit_mask=(1ULL<<board::encoderA)|(1ULL<<board::encoderB)|(1ULL<<board::button);
    cfg.mode=GPIO_MODE_INPUT; cfg.pull_up_en=GPIO_PULLUP_ENABLE; ESP_ERROR_CHECK(gpio_config(&cfg));
    previous=(gpio_get_level(static_cast<gpio_num_t>(board::encoderA))<<1)|gpio_get_level(static_cast<gpio_num_t>(board::encoderB));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_set_intr_type(static_cast<gpio_num_t>(board::encoderA),GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_set_intr_type(static_cast<gpio_num_t>(board::encoderB),GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_isr_handler_add(static_cast<gpio_num_t>(board::encoderA),encoder_isr,nullptr));
    ESP_ERROR_CHECK(gpio_isr_handler_add(static_cast<gpio_num_t>(board::encoderB),encoder_isr,nullptr));
}
// After an OTA update the bootloader rolls back unless the new image confirms itself.
void confirm_firmware() {
    esp_ota_img_states_t state;
    if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK &&
       state==ESP_OTA_IMG_PENDING_VERIFY) esp_ota_mark_app_valid_cancel_rollback();
}
void enter_setup() {
    confirm_firmware(); // The user is operating it, so the image works.
    nvs_handle_t h; ESP_ERROR_CHECK(nvs_open("panel",NVS_READWRITE,&h));
    ESP_ERROR_CHECK(nvs_set_u8(h,"setup",1)); ESP_ERROR_CHECK(nvs_commit(h)); nvs_close(h); panel_restart();
}
bool requested_setup() {
    nvs_handle_t h; uint8_t flag=0;
    if(nvs_open("panel",NVS_READWRITE,&h)==ESP_OK) {
        nvs_get_u8(h,"setup",&flag); nvs_erase_key(h,"setup"); nvs_commit(h); nvs_close(h);
    }
    return flag;
}
}
extern "C" void app_main() {
    // Never erase credentials automatically on a storage error.
    ESP_ERROR_CHECK(nvs_flash_init()); input_init(); display_init();
    Config config=config_load();
    bool setup=requested_setup() || !config.ready();
    bool accessPoint=!config.wifiConfigured();
    network_start(config,accessPoint); ha_start(config,setup);
    if(setup) portal_start(config);
    std::string setupScreen;
    int64_t disconnectedAt=esp_timer_get_time();
    float preview=NAN; int64_t lastTurn=0,lastRender=0,pressedAt=0,edgeAt=0,menuAt=0,lastActivity=0;
    bool rawPressed=false,stablePressed=false,dimmed=false,swallowPress=false,confirmed=false;
    QuickMenu menu;
    while(true) {
        ha_poll(); auto state=ha_state(); int64_t now=esp_timer_get_time();
        if(!state.authenticated || !state.climate.available) { preview=NAN; menu.close(); }
        portENTER_CRITICAL(&encoderLock);
        int ticks=transitions/board::encoderEdgesPerStep;
        transitions-=ticks*board::encoderEdgesPerStep;
        portEXIT_CRITICAL(&encoderLock);
        bool pressed=gpio_get_level(static_cast<gpio_num_t>(board::button))==0;
        if(ticks || pressed) {
            lastActivity=now;
            if(dimmed) { // The first touch only wakes the screen.
                dimmed=false; backlight_set(activeBrightness,wakeFadeMs);
                ticks=0; swallowPress=pressed;
            }
        }
        if(ticks && !setup) {
            if(menu.isOpen()) { menu.rotate(ticks); menuAt=now; }
            else if(state.climate.adjustable() && !state.commandPending) {
                preview=state.climate.adjust(std::isfinite(preview) ? preview : state.climate.target,ticks); lastTurn=now;
            }
        }
        if(pressed!=rawPressed) { rawPressed=pressed; edgeAt=now; }
        if(now-edgeAt>debounceUs && stablePressed!=rawPressed) {
            stablePressed=rawPressed;
            if(stablePressed) pressedAt=swallowPress ? 0 : now;
            else if(swallowPress) swallowPress=false; // Release of the wake-up press.
            else if(pressedAt && now-pressedAt<setupHoldUs && !setup && state.authenticated) {
                if(menu.isOpen()) {
                    auto command=menu.press(); menuAt=now;
                    if(command.kind==MenuCommand::mode || command.kind==MenuCommand::preset) {
                        bool accepted=command.kind==MenuCommand::mode ? ha_set_mode(command.value)
                                                                      : ha_set_preset(command.value);
                        // A rejected option may be stale; reopen from the current HA state.
                        if(accepted) menu.close();
                        else menu.open(state.modes,state.presets,state.mode,state.preset);
                    }
                } else if(!state.modes.empty() || !state.presets.empty()) {
                    if(std::isfinite(preview)) { ha_set_temperature(preview); preview=NAN; }
                    menu.open(state.modes,state.presets,state.mode,state.preset); menuAt=now;
                }
                pressedAt=0;
            }
        }
        if(stablePressed && pressedAt && now-pressedAt>setupHoldUs) enter_setup();
        bool holding=stablePressed && pressedAt && now-pressedAt>holdHintUs;
        if(menu.isOpen() && now-menuAt>menuTimeoutUs) menu.close();
        if(!menu.isOpen() && std::isfinite(preview) && now-lastTurn>sendDelayUs) {
            ha_set_temperature(preview); preview=NAN; // Never queue/replay controls offline.
        }
        if(!setup && !dimmed && !menu.isOpen() && !stablePressed && !std::isfinite(preview) &&
           now-lastActivity>idleDimUs) {
            dimmed=true; backlight_set(idleBrightness,dimFadeMs);
        }
        if(!confirmed && now>firmwareConfirmUs) { confirm_firmware(); confirmed=true; }
        if(setup && now-lastRender>renderUs) {
            auto ip=network_ip();
            if(!ip.empty()) {
                disconnectedAt=now;
                if(accessPoint) { network_setup_ap(false); accessPoint=false; }
            } else if(!accessPoint && now-disconnectedAt>wifiRetryUs) {
                network_setup_ap(true); accessPoint=true;
            }
            std::string screen=accessPoint ? "AP" : "LAN:"+ip;
            if(screen!=setupScreen) {
                if(accessPoint) display_setup(setup_ssid(),setup_password());
                else display_network(ip);
                setupScreen=screen;
            }
            lastRender=now;
        }
        if(!setup && now-lastRender>renderUs) {
            display_update(state,preview,holding ? "Trzymaj: konfiguracja" : "",&menu); lastRender=now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
