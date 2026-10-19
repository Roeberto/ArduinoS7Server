/*
 * S7Server - prosty serwer S7 (ISO-on-TCP, port 102) dla Arduino
 *
 * Pozwala sterownikowi Siemens S7-1200/1500 robić GET/PUT z Arduino
 * (a klientom Snap7 / Settimino czytać i zapisywać dane Arduino).
 * Arduino udostępnia zwykłe tablice bajtów jako bloki DB.
 *
 * Działa z biblioteką Ethernet (W5100 / W5200 / W5500) na AVR (Uno, Mega) i STM32.
 *
 * Użycie:
 *   #include <Ethernet.h>
 *   #include <S7Server.h>
 *
 *   S7Server s7;
 *   byte db1[32];
 *
 *   void setup() {
 *     Ethernet.begin(mac, ip);
 *     s7.addDB(1, db1, sizeof(db1));
 *     s7.begin();
 *   }
 *   void loop() {
 *     s7.poll();
 *     s7.setReal(1, 0, 21.5);          // DB1.DBD0
 *     bool on = s7.getBit(1, 31, 0);   // DB1.DBX31.0
 *   }
 */
#pragma once

#include <Arduino.h>
#include <Ethernet.h>

// Wywoływane po każdym udanym zapisie (PUT) z PLC
typedef void (*S7WriteCallback)(uint16_t db, uint16_t start, uint16_t size);

class S7Server
{
public:
  static const uint8_t  MAX_DBS     = 4;     // ile bloków DB można zarejestrować
  static const uint8_t  MAX_CLIENTS = 2;     // W5100 ma 4 gniazda: nasłuch + 2 klientów + zapas
  static const uint16_t PDU_SIZE    = 240;   // minimum akceptowane przez każdy PLC; mieści się w RAM Uno

  explicit S7Server(uint16_t port = 102);

  // Uruchamia nasłuch. Ethernet.begin() musi być wywołane wcześniej.
  void begin();
  // Obsługa sieci - wywołuj w każdym obiegu loop(), bez długich delay()
  void poll();

  // Rejestruje tablicę jako blok DB o podanym numerze (domyślnie PLC może ją całą zapisywać)
  bool addDB(uint16_t number, byte* buffer, uint16_t size);
  // Ogranicza zapis z PLC do zakresu [start, start + length); reszta DB tylko do odczytu
  bool setWriteRange(uint16_t number, uint16_t start, uint16_t length);
  // Blok tylko do odczytu dla PLC
  bool setReadOnly(uint16_t number);

  void    onWrite(S7WriteCallback callback);
  void    setDebug(Print& out);      // np. s7.setDebug(Serial) - wypisuje połączenia i błędy
  uint8_t clientCount();             // liczba podłączonych klientów
  uint32_t readCount()  { return reads; }    // liczba obsłużonych zapytań odczytu (GET)
  uint32_t writeCount() { return writes; }   // liczba obsłużonych zapytań zapisu (PUT)

  // Odczyt i zapis danych w DB (kolejność bajtów jak w S7 - big-endian).
  // Przy złym numerze DB lub offsecie poza blokiem: get zwraca 0, set nic nie robi.
  bool     getBit  (uint16_t db, uint16_t offset, uint8_t bit);
  uint8_t  getByte (uint16_t db, uint16_t offset);
  uint16_t getWord (uint16_t db, uint16_t offset);
  int16_t  getInt  (uint16_t db, uint16_t offset);
  uint32_t getDWord(uint16_t db, uint16_t offset);
  int32_t  getDInt (uint16_t db, uint16_t offset);
  float    getReal (uint16_t db, uint16_t offset);

  void setBit  (uint16_t db, uint16_t offset, uint8_t bit, bool value);
  void setByte (uint16_t db, uint16_t offset, uint8_t value);
  void setWord (uint16_t db, uint16_t offset, uint16_t value);
  void setInt  (uint16_t db, uint16_t offset, int16_t value);
  void setDWord(uint16_t db, uint16_t offset, uint32_t value);
  void setDInt (uint16_t db, uint16_t offset, int32_t value);
  void setReal (uint16_t db, uint16_t offset, float value);

private:
  struct DataBlock {
    uint16_t number;
    byte*    data;
    uint16_t size;
    uint16_t writeStart;
    uint16_t writeEnd;     // pierwszy bajt za zakresem zapisu
  };

  static const uint16_t FRAME_MAX = PDU_SIZE + 7;   // TPKT (4) + COTP DT (3) + PDU S7

  EthernetServer  server;
  EthernetClient  clients[MAX_CLIENTS];
  DataBlock       dbs[MAX_DBS];
  uint8_t         dbCount = 0;
  S7WriteCallback writeCallback = nullptr;
  Print*          debug = nullptr;
  uint16_t        port;
  uint32_t        reads = 0;
  uint32_t        writes = 0;
  byte            rx[FRAME_MAX];
  byte            tx[FRAME_MAX];

  DataBlock* findDB(uint16_t number);
  byte*      area(uint16_t db, uint16_t offset, uint16_t size);

  bool     readExact(EthernetClient& c, byte* dst, uint16_t n);
  uint16_t readFrame(EthernetClient& c);
  void     handleFrame(EthernetClient& c, uint16_t len);
  void     cotpConfirm(EthernetClient& c, uint16_t len);
  void     beginAck();
  void     sendAck(EthernetClient& c, uint16_t parLen, uint16_t dataLen);
  void     setupCommunication(EthernetClient& c, uint16_t len);
  void     readVar(EthernetClient& c, uint16_t len);
  void     writeVar(EthernetClient& c, uint16_t len);
  byte     checkItem(const byte* item, bool write, DataBlock*& block, uint16_t& start, uint8_t& bit, uint16_t& size);
  void     logItemError(char op, const byte* item, byte rc);
};
