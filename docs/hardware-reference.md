# Ustawienia sprzętu a dokumentacja producenta

Wartości w `include/BoardConfig.h` pochodzą z przykładu producenta
`Example Programs/ESP-IDF/02_KNOB_KEY_BL_test` (pliki `main/lcd_config.h`,
`main/main.c`, `sdkconfig.defaults`) w
[repozytorium OpenNextion-SKU-ONX2424G013](https://github.com/OpenNextion/OpenNextion-SKU-ONX2424G013),
commit `63d1bb6f18b413de07e9e25a74128c809648dd47`.

| Ustawienie | Wartość |
|---|---|
| LCD | GC9A01, SPI2, 40 MHz, tryb 0 |
| MOSI / SCLK / CS / DC / RST / podświetlenie | GPIO 1 / 5 / 2 / 3 / 8 / 6 |
| Enkoder A / B / przycisk | GPIO 48 / 47 / 9, podciągnięte do zasilania |
| Rozdzielczość | 240×240 |
| Format LCD | BGR, 16 bitów, inwersja kolorów włączona |
| Orientacja | odbicie X włączone, odbicie Y i zamiana osi wyłączone |
| LVGL | zamiana bajtów RGB565 włączona |
| PSRAM | 8 MB OPI, 80 MHz |
| Flash | 16 MB |
| Podświetlenie | LEDC low-speed, timer 0, kanał 0, 11 bitów, 25 kHz |

Orientację ustawia `esp_lvgl_port`, który przekazuje ją do `esp_lcd_panel_mirror`.

## Podświetlenie

Jasność liczymy tak samo jak producent: wypełnienie = 2047·(p/100)^2,8 dla p
od 10 do 100%. Producent zaznacza w komentarzu, że przy niższej częstotliwości
PWM na ekranie widać pasy, dlatego zostaje 25 kHz.

## Enkoder

Tu firmware odchodzi od przykładu. Producent liczy dwa przejścia kwadratury
na krok. Pomiar na urządzeniu pokazał, że jedna zapadka to jedno przejście,
a każdy z czterech stanów linii A/B jest pozycją spoczynku. Przy dwóch
przejściach na krok co druga zapadka nie zmieniała nastawy, a po zmianie
kierunku pierwsza zapadka tylko kasowała resztę. Dlatego
`encoderEdgesPerStep = 1`. Pomiar nie wykazał drgań styków ani pominiętych
przejść.
