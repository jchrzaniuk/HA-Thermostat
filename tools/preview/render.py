#!/usr/bin/env python3
"""Render the current firmware UI with native LVGL 8.4, no hand-drawn replacement."""
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build' / 'preview'
OUT = ROOT / 'docs' / 'screenshots'
BUILD.mkdir(parents=True, exist_ok=True)
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'main/display.cpp').read_text()
# Retain widget creation and both rendering functions exactly as in firmware.
helpers = source[source.index('namespace {'):source.index('void display_init()')]
ui = source[source.index('    lvgl_port_lock(0);', source.index('void display_init()')):]
(BUILD / 'esp_err.h').write_text('#pragma once\nusing esp_err_t = int;\n')
program = r'''
#include <cstdio>
#include <cstdint>
#include "app.h"
#include "ClimateOptions.h"
#include "BoardConfig.h"
#include "WifiQr.h"
#include "ui_assets.h"
#include <cmath>
#include <cstring>
#include "lvgl.h"
static bool lvgl_port_lock(int) { return true; }
static void lvgl_port_unlock() {}
'''+helpers+'void display_init() {\n'+ui+r'''
static lv_color_t pixels[240*240], buffer[240*40];
static void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *data) {
    for(int y=area->y1;y<=area->y2;y++)
        for(int x=area->x1;x<=area->x2;x++) pixels[y*240+x]=*data++;
    lv_disp_flush_ready(drv);
}
static void save(const char *name) {
    lv_obj_invalidate(lv_scr_act()); lv_refr_now(nullptr);
    FILE *f=fopen(name,"wb"); fprintf(f,"P6\n240 240\n255\n");
    for(auto p:pixels) {
        lv_color32_t color; color.full=lv_color_to32(p);
        uint8_t rgb[]={color.ch.red,color.ch.green,color.ch.blue};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main() {
    lv_init();
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw,buffer,nullptr,240*40);
    static lv_disp_drv_t drv; lv_disp_drv_init(&drv);
    drv.hor_res=240; drv.ver_res=240; drv.draw_buf=&draw; drv.flush_cb=flush;
    lv_disp_drv_register(&drv); display_init();
    PanelState s; s.name="Łazienka"; s.mode="heat"; s.authenticated=true; s.climate.available=true;
    s.climate.minimum=5; s.climate.maximum=35; s.climate.target=22;
    s.climate.current=20.5; s.action="heating"; s.status=statusConnected;
    display_update(s,NAN,""); save("01-heating.ppm");
    s.action="idle"; s.climate.target=21; s.climate.current=21.2;
    display_update(s,NAN,""); save("02-idle.ppm");
    display_update(s,23.5,""); save("03-adjusting.ppm");
    s.preset="eco"; display_update(s,NAN,""); save("04-eco.ppm");
    s.preset="comfort"; s.action="heating";
    QuickMenu menu; menu.open({"off","heat","cool"},{"none","eco","comfort","boost"},"heat","comfort");
    display_update(s,NAN,"",&menu); save("05-menu.ppm");
    menu.rotate(1); display_update(s,NAN,"",&menu); save("06-menu-profile.ppm");
    menu.rotate(-1); menu.press(); display_update(s,NAN,"",&menu); save("07-menu-modes.ppm");
    menu.rotate(-5); menu.press(); menu.rotate(1); menu.press();
    display_update(s,NAN,"",&menu); save("08-menu-profiles.ppm");
    menu.close(); s.preset.clear(); s.mode="off"; s.action="off";
    display_update(s,NAN,"",&menu); save("09-off.ppm");
    s.mode="heat"; s.authenticated=false; s.climate.available=false; s.status="Brak połączenia z HA";
    display_update(s,NAN,""); save("10-offline.ppm");
    lv_obj_clean(lv_scr_act()); display_init();
    display_setup("ONX-A1B2","7FA2C84E901D"); save("11-setup.ppm");
    display_network("192.168.1.42"); save("12-lan-setup.ppm");
}
'''
(BUILD / 'preview.cpp').write_text(program)
subprocess.run(['cmake','-S',str(ROOT/'tools/preview'),'-B',str(BUILD),'-G','Ninja'],check=True)
subprocess.run(['cmake','--build',str(BUILD),'-j','8'],check=True)
subprocess.run([str(BUILD/'preview')],cwd=OUT,check=True)
# Preserve exact framebuffer PNGs, plus circular crops matching the physical LCD.
labels=['Grzanie','Temperatura osiągnięta','Zmiana nastawy','Profil eco','Menu: tryb','Menu: profil','Lista trybów','Lista profili','Wyłączony','Brak połączenia','Konfiguracja Wi-Fi','Konfiguracja HA w LAN']
font_path='/System/Library/Fonts/Supplemental/Arial.ttf'
font=ImageFont.truetype(font_path,24)
small=ImageFont.truetype(font_path,18)
sheet=Image.new('RGB',(1100,1570),'#e9ecf0')
draw=ImageDraw.Draw(sheet)
draw.text((40,26),'ONX · aktualny interfejs firmware',font=ImageFont.truetype(font_path,30),fill='#18212d')
draw.text((40,70),'Render LVGL 8.4 • dane przykładowe • ekran 240 × 240 px',font=small,fill='#536171')
for i,path in enumerate(sorted(OUT.glob('*.ppm'))):
    im=Image.open(path).convert('RGB'); im.save(path.with_suffix('.png')); path.unlink()
    mask=Image.new('L',(240,240)); ImageDraw.Draw(mask).ellipse((0,0,239,239),fill=255)
    circle=im.convert('RGBA'); circle.putalpha(mask)
    circle.save(path.with_name(path.stem+'-round.png'))
    x=40+(i%3)*355; y=126+(i//3)*355
    draw.rounded_rectangle((x,y,x+325,y+330),radius=18,fill='white')
    draw.ellipse((x+31,y+13,x+295,y+277),fill='#252a31')
    sheet.paste(circle,(x+44,y+26),circle)
    draw.text((x+18,y+294),labels[i],font=font,fill='#18212d')
sheet.save(OUT/'overview.png')
print(OUT/'overview.png')
