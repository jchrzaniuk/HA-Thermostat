# Testy na urządzeniu

Kompilacja i testy na komputerze nie potwierdzają, że firmware działa na sprzęcie.
Ta lista mówi, co sprawdzono na ONX2424G013 z działającym Home Assistant,
a co czeka na test. Jeśli sprawdzisz któryś punkt, zgłoś wynik w issue.

## Sprawdzone

- Start aplikacji, PSRAM 8 MB, obraz na LCD.
- Połączenie z Wi-Fi i z Home Assistant, odczyt encji.
- Zmiana tablicy partycji na układ OTA przez USB. Ustawienia Wi-Fi i HA
  zostają, bo partycja NVS nie zmienia adresu.
- Enkoder: jedna zapadka to jeden krok (zob. `hardware-reference.md`).

## Do sprawdzenia

**Ekran i obsługa**
1. Orientacja i kolory, czytelność napisów 14 px.
2. Czy obudowa nie zasłania kresek na obwodzie.
3. Szybki obrót: płynność rysowania, brak zgubionych kroków.
4. Menu: przewijanie ikon i list, „Wróć” na obu poziomach, zamknięcie po 15 s.
5. Przyciemnianie po 30 s bez pasów i migotania. Pierwszy ruch tylko budzi ekran.
6. Podpowiedź po 1 s przytrzymania i wejście do konfiguracji po 3 s.

**Konfiguracja**
1. Od pustej pamięci: Wi-Fi z telefonu przez kod QR, potem HA z komputera.
2. Błędne hasło Wi-Fi: po 30 s znów pojawia się kod QR.
3. Błędny token nie kasuje ustawień Wi-Fi.
4. Strona konfiguracji przez sieć panelu (AP) i przez sieć domową.
   Po zakończeniu konfiguracji strona jest niedostępna.
5. `wss://` z poprawnym i z błędnym certyfikatem.

**Home Assistant**
1. Better Thermostat: odczyt temperatur, zmiana nastawy z panelu i z HA,
   przejścia grzanie / spoczynek / wyłączony, zakres i krok encji.
2. Versatile Thermostat: lista profili, przełącznik Eco (eco ↔ none), ręczna
   zmiana nastawy przy aktywnym profilu, blokady i wykrywanie okna. Panel ma
   pokazać stan zwrócony przez HA i nie powtarzać odrzuconych poleceń.
3. Encja bez profili, encja z dwiema nastawami, stan `unknown` i `unavailable`,
   usunięta encja (komunikat „Brak encji w HA”).
4. °C i °F (krok 1 bez części dziesiętnej).
5. Wyłączenie HA i Wi-Fi w trakcie obracania. Po powrocie panel pobiera świeży
   stan i nie wysyła starej nastawy.
6. Odrzucona usługa, opóźnione potwierdzenie, restart HA.
7. Konto HA bez uprawnień administratora.

**Aktualizacja i stabilność**
1. OTA ze strony konfiguracji: nowa wersja widoczna po restarcie.
2. Odrzucenie pliku innego projektu i przerwanego wysyłania.
3. Powrót do poprzedniej wersji, gdy nowa restartuje się przed upływem 15 s.
4. Praca ciągła przez 24 h: wolna pamięć, ponowne połączenia.
