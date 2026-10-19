# S7Server – serwer protokołu S7 dla Arduino

Biblioteka zamienia Arduino z kartą Ethernet (W5100 / W5200 / W5500) w **serwer protokołu S7**,
czyli natywnego protokołu sterowników Siemens SIMATIC. Dzięki niej sterownik **S7-1200 / S7-1500**
może za pomocą standardowych instrukcji **GET** i **PUT** odczytywać i zapisywać dane Arduino
tak samo, jak dane innego sterownika PLC.

Arduino udostępnia zwykłe tablice bajtów jako **bloki danych (DB)**. Program w Arduino zapisuje
do nich wartości (np. pomiary) i odczytuje polecenia przesłane przez PLC.

```
 ┌──────────────────┐   GET (odczyt)    ┌───────────────────────────┐
 │  Sterownik PLC   │ ────────────────▶ │  Arduino + Ethernet       │
 │  S7-1200/1500    │                   │  biblioteka S7Server      │
 │  (klient S7)     │ ◀──────────────── │  (serwer S7, port 102)    │
 └──────────────────┘   PUT (zapis)     │  DB1, DB2… = tablice byte │
                                        └───────────────────────────┘
```

## Spis treści

1. [Cechy](#cechy)
2. [Wymagania](#wymagania)
3. [Instalacja](#instalacja)
4. [Szybki start](#szybki-start)
5. [Opis funkcji (API)](#opis-funkcji-api)
6. [Adresowanie i kolejność bajtów](#adresowanie-i-kolejność-bajtów)
7. [Konfiguracja sterownika w TIA Portal](#konfiguracja-sterownika-w-tia-portal)
8. [Zasada działania protokołu](#zasada-działania-protokołu)
9. [Kody błędów](#kody-błędów)
10. [Wydajność i zużycie zasobów](#wydajność-i-zużycie-zasobów)
11. [Ograniczenia](#ograniczenia)
12. [Testowanie](#testowanie)
13. [Struktura plików](#struktura-plików)

## Cechy

- Serwer S7 na porcie TCP **102** (ISO-on-TCP, RFC 1006), zgodny z instrukcjami GET/PUT S7-1200/1500
  oraz z klientami Snap7 / python-snap7.
- Do **4 bloków DB** o dowolnych numerach i rozmiarach.
- **Ochrona przed zapisem** – dla każdego DB można określić obszar, który PLC może zapisywać;
  pozostałe dane są tylko do odczytu.
- Obsługa typów: Bool (bit), Byte, Char, Word, Int, DWord, DInt, Real.
- Do **4 obszarów w jednym zapytaniu** (rozwinięty blok GET/PUT), z wyrównaniem danych wymaganym przez S7.
- Proste funkcje `get…` / `set…` – konwersja kolejności bajtów (big-endian) wykonywana automatycznie.
- Funkcja zwrotna wywoływana po każdym zapisie z PLC (`onWrite`).
- Liczniki obsłużonych zapytań (`readCount`, `writeCount`) – pomiar częstotliwości komunikacji.
- Do **2 jednoczesnych klientów** (np. PLC + komputer z narzędziem diagnostycznym).
- Działa na **Arduino Uno** (2 KB RAM) – bufory komunikacyjne zajmują ok. 500 B.

## Wymagania

| Element | Wymaganie |
|---|---|
| Płytka | Arduino Uno / Mega (AVR) lub STM32 (STM32duino) |
| Karta sieciowa | Ethernet Shield / moduł z układem WIZnet **W5100**, **W5200** lub **W5500** |
| Biblioteki | **Ethernet** w wersji ≥ 2.0 (wymagana funkcja `EthernetServer::accept()`) |
| Sterownik | Siemens **S7-1200** lub **S7-1500** z instrukcjami GET/PUT |

## Instalacja

**Sposób 1 – katalog bibliotek Arduino.** Skopiuj cały katalog `S7Server` do katalogu bibliotek
Arduino IDE (Linux: `~/Arduino/libraries/`, Windows: `Dokumenty\Arduino\libraries\`)
i uruchom ponownie Arduino IDE.

**Sposób 2 – plik ZIP.** Spakuj katalog `S7Server` do archiwum ZIP i w Arduino IDE wybierz
*Szkic → Dołącz bibliotekę → Dodaj bibliotekę .ZIP*.

Po instalacji przykład jest dostępny w *Plik → Przykłady → S7Server → Podstawowy*.

## Szybki start

```cpp
#include <SPI.h>
#include <Ethernet.h>
#include <S7Server.h>

byte      mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x36 };
IPAddress ip(192, 168, 0, 90);

S7Server s7;          // serwer S7 na porcie 102
byte     db1[16];     // pamięć bloku DB1

void setup()
{
  Ethernet.init(10);                 // pin CS karty Ethernet
  Ethernet.begin(mac, ip);

  s7.addDB(1, db1, sizeof(db1));     // tablica widoczna w PLC jako DB1
  s7.setWriteRange(1, 6, 2);         // PLC może zapisywać tylko bajty 6..7
  s7.begin();
}

void loop()
{
  s7.poll();                                     // obsługa sieci – w każdym obiegu loop()

  s7.setReal(1, 0, analogRead(A0) * 5.0 / 1023); // DB1.DBD0  (Real)  Arduino -> PLC
  s7.setInt (1, 4, millis() / 1000);             // DB1.DBW4  (Int)   Arduino -> PLC
  bool led = s7.getBit(1, 7, 0);                 // DB1.DBX7.0 (Bool) PLC -> Arduino
  digitalWrite(7, led);
}
```

> **Ważne:** `s7.poll()` musi być wywoływane często. Program nie powinien używać długich
> `delay()` – zamiast tego należy planować zadania za pomocą `millis()`.

## Opis funkcji (API)

### Konfiguracja i praca

| Funkcja | Opis |
|---|---|
| `S7Server(uint16_t port = 102)` | Konstruktor. Port 102 jest wymagany przez PLC; inny port przydaje się tylko do testów. |
| `bool addDB(number, buffer, size)` | Rejestruje tablicę `buffer` o rozmiarze `size` bajtów jako blok `DB<number>`. Domyślnie PLC może zapisywać cały blok. Zwraca `false`, gdy przekroczono limit 4 bloków. Ponowne wywołanie dla tego samego numeru podmienia blok. |
| `bool setWriteRange(number, start, length)` | Zezwala PLC na zapis tylko w zakresie bajtów `[start, start + length)`. Zapis poza tym zakresem kończy się błędem `0x03`. |
| `bool setReadOnly(number)` | Blok tylko do odczytu dla PLC. |
| `void begin()` | Uruchamia nasłuch. Wywołać po `Ethernet.begin()` i `addDB()`. |
| `void poll()` | Przyjmuje połączenia i obsługuje zapytania. Wywoływać w każdym obiegu `loop()`. |
| `void onWrite(callback)` | Rejestruje funkcję `void f(uint16_t db, uint16_t start, uint16_t size)` wywoływaną po każdym udanym zapisie obszaru przez PLC. |
| `void setDebug(Print& out)` | Włącza komunikaty diagnostyczne (np. `s7.setDebug(Serial)`): nowe połączenia, rozłączenia i odrzucone zapytania. |
| `uint8_t clientCount()` | Liczba aktualnie podłączonych klientów. |
| `uint32_t readCount()` | Liczba obsłużonych zapytań odczytu (GET) od startu. |
| `uint32_t writeCount()` | Liczba obsłużonych zapytań zapisu (PUT) od startu. |

### Odczyt i zapis danych w DB

Wszystkie funkcje przyjmują numer bloku `db` i przesunięcie `offset` w bajtach (jak `DBB` w TIA).
Przy błędnym numerze DB lub przesunięciu poza blokiem funkcje `get…` zwracają `0`,
a funkcje `set…` nic nie robią.

| Typ S7 | Rozmiar | Odczyt | Zapis | Adres w TIA |
|---|---|---|---|---|
| Bool | 1 bit | `getBit(db, offset, bit)` | `setBit(db, offset, bit, value)` | `DBX offset.bit` |
| Byte | 1 B | `getByte(db, offset)` | `setByte(db, offset, value)` | `DBB offset` |
| Word | 2 B | `getWord(db, offset)` | `setWord(db, offset, value)` | `DBW offset` |
| Int | 2 B | `getInt(db, offset)` | `setInt(db, offset, value)` | `DBW offset` |
| DWord | 4 B | `getDWord(db, offset)` | `setDWord(db, offset, value)` | `DBD offset` |
| DInt | 4 B | `getDInt(db, offset)` | `setDInt(db, offset, value)` | `DBD offset` |
| Real | 4 B | `getReal(db, offset)` | `setReal(db, offset, value)` | `DBD offset` |

Stałe klasy: `S7Server::MAX_DBS = 4`, `S7Server::MAX_CLIENTS = 2`, `S7Server::PDU_SIZE = 240`.

## Adresowanie i kolejność bajtów

Sterowniki S7 zapisują liczby w kolejności **big-endian** (najstarszy bajt pierwszy), a Arduino
(AVR, ARM) w kolejności little-endian. Funkcje `get…` / `set…` wykonują konwersję automatycznie,
dlatego do danych w tablicy DB należy zawsze odwoływać się przez te funkcje, a nie bezpośrednio.

**Bity w słowie (Word).** Ponieważ najstarszy bajt jest pierwszy, bit 0 słowa `DBW14` leży
w bajcie **15**, a nie 14:

| Bit słowa `DBW14` | 0 … 7 | 8 … 15 |
|---|---|---|
| Adres bitowy | `DBX15.0` … `DBX15.7` | `DBX14.0` … `DBX14.7` |

W praktyce najprościej operować na całym słowie i maskach bitowych:

```cpp
uint16_t ster = s7.getWord(1, 14);
bool wentylator = ster & (1 << 0);     // w TIA: "DB".Sterowanie.%X0
```

## Konfiguracja sterownika w TIA Portal

1. **Połączenie.** Wstaw instrukcję **GET** lub **PUT** (*Instructions → Communication →
   S7 communication*). W *Properties → Configuration → Connection parameter* ustaw:
   partner **unspecified**, adres IP Arduino, zaznaczone **Active connection establishment**.
   TSAP partnera może pozostać domyślny – biblioteka akceptuje dowolny.
2. **GET i PUT mogą korzystać z tego samego połączenia** (ten sam parametr `ID`).
3. **Parametr `ADDR_x`** wskazuje obszar **w Arduino** i musi być adresem bezwzględnym,
   np. `P#DB1.DBX0.0 REAL 2`.
4. **Parametry `RD_x` / `SD_x`** wskazują obszar **w PLC**. Mogą leżeć w DB
   z włączonym *Optimized block access*. Typ i liczba elementów muszą odpowiadać `ADDR_x`,
   np. `P#DB1.DBX0.0 REAL 2` ↔ `Array[0..1] of Real`.
5. **Wyzwalanie.** `REQ` reaguje na zbocze narastające – np. bit zegarowy `Clock_10Hz` (%M0.0)
   po włączeniu *System and clock memory* w konfiguracji CPU.
6. Opcja *„Permit access with PUT/GET communication”* **nie jest potrzebna** – to PLC jest klientem.

Przykład dla danych z sekcji [Szybki start](#szybki-start):

```
GET   REQ    = "Clock_10Hz"
      ADDR_1 = P#DB1.DBX0.0 REAL 1     RD_1 = "Arduino".Napiecie   (Real)
      ADDR_2 = P#DB1.DBX4.0 INT 1      RD_2 = "Arduino".Sekundy    (Int)
PUT   REQ    = "Clock_10Hz"
      ADDR_1 = P#DB1.DBX6.0 WORD 1     SD_1 = "Arduino".Command    (Word)
```

> Wyjścia `NDR`, `DONE` i `ERROR` bloków GET/PUT są aktywne tylko przez jeden cykl.
> Do diagnostyki warto zapamiętywać `STATUS` przy `ERROR = 1` w osobnej zmiennej.

## Zasada działania protokołu

Komunikacja S7 przez Ethernet składa się z czterech warstw:

```
┌────────────────────────────────────────────────────────────┐
│ TCP, port 102                                              │
│ ┌────────────────────────────────────────────────────────┐ │
│ │ TPKT (RFC 1006): 03 00 + długość ramki                 │ │
│ │ ┌────────────────────────────────────────────────────┐ │ │
│ │ │ COTP (ISO 8073): połączenie (CR/CC), dane (DT)     │ │ │
│ │ │ ┌────────────────────────────────────────────────┐ │ │ │
│ │ │ │ S7: nagłówek 0x32 + funkcja + parametry + dane │ │ │ │
│ │ │ └────────────────────────────────────────────────┘ │ │ │
│ │ └────────────────────────────────────────────────────┘ │ │
│ └────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────┘
```

Przebieg komunikacji z PLC:

| Krok | Ramka od PLC | Odpowiedź biblioteki |
|---|---|---|
| 1 | TCP connect na port 102 | akceptacja połączenia |
| 2 | COTP **Connection Request** (0xE0) | COTP **Connection Confirm** (0xD0) z zamienionymi referencjami |
| 3 | S7 **Setup Communication** (0xF0) | uzgodniony rozmiar PDU = 240 B |
| 4 | S7 **Read Var** (0x04) – instrukcja GET | dane każdego obszaru + kod wyniku |
| 5 | S7 **Write Var** (0x05) – instrukcja PUT | zapis do DB + kod wyniku dla każdego obszaru |

Kroki 4 i 5 powtarzają się przy każdym zboczu `REQ`. Połączenie pozostaje otwarte.
Po zerwaniu połączenia (np. restart Arduino) PLC nawiązuje je ponownie automatycznie.

Każdy obszar w zapytaniu jest opisany 12-bajtową strukturą S7ANY: typ danych, liczba elementów,
numer DB, obszar pamięci (`0x84` = DB) i adres początkowy **w bitach** (bajt × 8 + bit).

## Kody błędów

Kod wyniku jest zwracany osobno dla każdego obszaru; w PLC pojawia się jako `ERROR = 1`
i odpowiedni `STATUS` bloku GET/PUT. Przy włączonym `setDebug(Serial)` biblioteka wypisuje
np. `S7Server: R DB3.DBB0 len 4 -> blad 0xA` (`R` = odczyt, `W` = zapis).

| Kod | Znaczenie | Typowa przyczyna |
|---|---|---|
| `0xFF` | OK | – |
| `0x03` | Dostęp zabroniony | zapis poza zakresem ustawionym w `setWriteRange` / `setReadOnly` |
| `0x05` | Adres poza zakresem | obszar wykracza poza rozmiar DB w Arduino |
| `0x06` | Nieobsługiwany typ | typ COUNTER / TIMER albo BOOL z liczbą elementów > 1 |
| `0x07` | Niezgodny rozmiar danych | długość danych w zapisie nie odpowiada opisowi obszaru |
| `0x0A` | Obiekt nie istnieje | numer DB w `ADDR_x` nie został zarejestrowany przez `addDB` albo obszar inny niż DB (M, I, Q) |

## Wydajność i zużycie zasobów

Pomiary na Arduino Uno + Ethernet Shield W5100 (klient python-snap7 w sieci lokalnej):

| Operacja | Czas odpowiedzi (mediana) | Maks. liczba zapytań / s |
|---|---|---|
| GET całego DB (20 B) | 1,5 ms | ok. 680 |
| PUT 6 B (Word + Real) | 1,3 ms | ok. 750 |

W praktyce częstotliwość wymiany określa PLC (zbocza `REQ`); przy `Clock_10Hz` jest to
10 zapytań GET i 10 PUT na sekundę.

| Zasób (Arduino Uno) | Zużycie przez bibliotekę |
|---|---|
| RAM | ok. 500 B buforów (2 × 247 B) + opisy DB + obiekty połączeń |
| Gniazda W5100 (4) | 1 nasłuch + do 2 klientów |

## Ograniczenia

- Obsługiwany jest wyłącznie obszar **DB**; obszary M, I, Q, liczniki i timery nie są dostępne.
- Obsługiwane funkcje S7: Setup Communication, Read Var, Write Var. Pozostałe (np. odczyt SZL,
  diagnostyka, ładowanie bloków) są ignorowane.
- Rozmiar PDU jest stały: **240 B** – ok. 220 B danych w jednym zapytaniu. Ramki podzielone
  na fragmenty COTP nie są obsługiwane (przy PDU 240 nie występują).
- Brak uwierzytelniania i szyfrowania – jak w klasycznej komunikacji S7 (PUT/GET). Sieć
  sterowania powinna być odseparowana od innych sieci.
- Biblioteka nie jest przeznaczona do wywoływania z przerwań – `poll()` i funkcje `get…`/`set…`
  należy wywoływać w `loop()`.

## Testowanie

**Bez sterownika – python-snap7 na komputerze PC:**

```python
import snap7
c = snap7.client.Client()
c.connect("192.168.0.90", 0, 1)          # rack 0, slot 1 (biblioteka akceptuje dowolne)
print(c.db_read(1, 0, 16))               # odczyt 16 bajtów DB1
c.db_write(1, 6, bytearray([0, 1]))      # zapis słowa DBW6 = 1
```

**Podgląd ramek – Wireshark** z filtrem `s7comm`: dekoder rozpisuje każde pole ramek
TPKT, COTP i S7, co ułatwia diagnozę błędów konfiguracji GET/PUT.

**Symulator na PC** (osobne narzędzie z projektu dyplomowego): kompiluje szkic Arduino z biblioteką na Linuksie
i udostępnia serwer S7 bez sprzętu – PLC lub PLCSIM łączy się z nim jak z płytką.

Przetestowane konfiguracje:
- Arduino Uno (klon, CH340) + Ethernet Shield W5100 ↔ python-snap7 3.2.1,
- Arduino Uno + W5100 ↔ S7-PLCSIM Advanced V7 (CPU 1515-2 PN), TIA Portal V20, GET/PUT,
- symulator PC ↔ python-snap7 oraz ręcznie zbudowane ramki z wieloma obszarami.

## Struktura plików

```
S7Server/
├── library.properties          opis biblioteki dla Arduino IDE
├── keywords.txt                podświetlanie składni
├── README.md                   ta dokumentacja
├── src/
│   ├── S7Server.h              deklaracja klasy i opis funkcji
│   └── S7Server.cpp            implementacja protokołu
└── examples/
    └── Podstawowy/
        └── Podstawowy.ino      przykład: Real + Int do PLC, Word z PLC
```
