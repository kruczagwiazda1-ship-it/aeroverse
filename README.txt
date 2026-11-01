AEROVERSE NA WII U - TEST SIECI (krok 1)
=========================================
Ten maly program sprawdza, czy Twoja Wii U polaczy sie z Aeroverse przez HTTPS.
To najwazniejsza niewiadoma. Dopiero gdy to dziala, pisze sie prawdziwa aplikacje.

NAJLATWIEJ: BUDOWANIE NA GITHUBIE (tak, jak przy Twojej grze)
   - Wrzuc caly ten folder do repozytorium na GitHubie (razem z ukrytym folderem .github).
   - Po wrzuceniu wejdz w zakladke Actions. Budowanie ruszy samo.
   - Gdy bedzie zielone, otworz uruchomienie i pobierz plik wiiu_nettest z sekcji Artifacts.
   - W srodku jest wiiu_nettest.wuhb. Przejdz do punktu 3 (URUCHOMIENIE).
   - Jesli budowanie sie wysypie, wklej mi log z kroku Build albo Install.
   Pominiesz wtedy punkty 1 i 2 ponizej (instalacja lokalna jest tylko alternatywa).

1) INSTALACJA NARZEDZI LOKALNIE (alternatywa, raz, na komputerze z Windows)
   - Pobierz instalator devkitPro: https://devkitpro.org/wiki/Getting_Started
   - Po instalacji otworz "MSYS2" z menu Start (z folderu devkitPro) i wpisz:
        dkp-pacman -S --needed wiiu-dev wiiu-curl
     (jesli wypisze, ze nie zna wiiu-curl, wpisz:  dkp-pacman -Ss curl  i wyslij mi wynik)

2) BUDOWANIE
   - Rozpakuj ten folder, np. do C:\wiiu_nettest
   - W oknie MSYS2:
        cd /c/wiiu_nettest
        make
   - Jesli wyskoczy blad, skopiuj mi go w calosci. Pisalem kod bez kompilatora,
     wiec mozliwe sa male poprawki.
   - Powstanie plik wiiu_nettest.wuhb (oraz .rpx).

3) URUCHOMIENIE (Aroma)
   - Skopiuj wiiu_nettest.wuhb na karte SD do folderu  wiiu/apps/
   - Uruchom z menu Aroma. Zobaczysz tekst na TV i GamePadzie.
   - Program robi dwa zapytania do https://aeroverse.lukwszystkorobicz.pl/api/communities:
        a) z prawdziwa weryfikacja certyfikatu  -> to jest to, co bedzie w aplikacji
        b) bez weryfikacji (tylko diagnostyka)  -> pokazuje, czy samo polaczenie dziala
   - Zrob zdjecie ekranu i wyslij mi je.

Jak czytac wynik:
   a) OK, HTTP 200      -> wszystko dziala, mozna pisac prawdziwa aplikacje.
   a) FAILED, b) OK     -> siec dziala, problem tylko z certyfikatami (naprawialny).
   a) i b) FAILED       -> siec/DNS/TLS nie dziala na tej konsoli (sprawdz Wi-Fi w ustawieniach).
