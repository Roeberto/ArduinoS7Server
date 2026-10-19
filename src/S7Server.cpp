/*
 * S7Server - implementacja
 *
 * Obsługiwane ramki (wystarczają dla bloków GET/PUT S7-1200/1500 i klientów Snap7):
 *   TPKT (RFC1006) -> COTP: CR/CC, DT, DR -> S7 Job: Setup Communication (0xF0),
 *   Read Var (0x04, GET z PLC), Write Var (0x05, PUT z PLC). Inne funkcje są ignorowane.
 * Kodowanie odpowiedzi jest zgodne z serwerem Snap7.
 */
#include "S7Server.h"

namespace {

// Offsety w ramce liczone od początku TPKT
const uint8_t REQ_PARAMS = 17;   // TPKT 4 + COTP 3 + nagłówek S7 Job 10
const uint8_t RES_PARAMS = 19;   // nagłówek S7 Ack_Data ma 12 bajtów (dochodzi klasa i kod błędu)
const uint8_t ITEM_SIZE  = 12;   // opis jednej zmiennej (S7ANY) w Read/Write Var
const byte    AREA_DB    = 0x84;

// Kody wyniku dla pojedynczej zmiennej
const byte RC_OK            = 0xFF;
const byte RC_ACCESS_DENIED = 0x03;
const byte RC_OUT_OF_RANGE  = 0x05;
const byte RC_BAD_TYPE      = 0x06;
const byte RC_SIZE_MISMATCH = 0x07;
const byte RC_NOT_FOUND     = 0x0A;

// Typ zmiennej w zapytaniu (z wskaźnika ANY, np. P#DB1.DBX0.0 REAL 4)
const byte TS_BIT = 0x01, TS_BYTE = 0x02, TS_CHAR = 0x03, TS_WORD = 0x04,
           TS_INT = 0x05, TS_DWORD = 0x06, TS_DINT = 0x07, TS_REAL = 0x08;
// Typ w sekcji danych (długość w bitach dla BYTE/INT, w bajtach dla REAL/OCTET)
const byte TSD_BIT = 0x03, TSD_BYTE = 0x04, TSD_INT = 0x05, TSD_REAL = 0x07, TSD_OCTET = 0x09;

uint16_t getU16(const byte* buf, uint16_t pos)
{
  return ((uint16_t)buf[pos] << 8) | buf[pos + 1];
}

void putU16(byte* buf, uint16_t pos, uint16_t value)
{
  buf[pos]     = (value >> 8) & 0xFF;
  buf[pos + 1] =  value       & 0xFF;
}

uint32_t getU32(const byte* p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void putU32(byte* p, uint32_t value)
{
  p[0] = (value >> 24) & 0xFF;
  p[1] = (value >> 16) & 0xFF;
  p[2] = (value >> 8)  & 0xFF;
  p[3] =  value        & 0xFF;
}

uint32_t itemAddress(const byte* item)   // adres w bitach
{
  return ((uint32_t)item[9] << 16) | ((uint32_t)item[10] << 8) | item[11];
}

uint32_t itemBytes(byte ts, uint16_t count)
{
  switch (ts) {
    case TS_BIT:   return count == 1 ? 1 : 0;
    case TS_BYTE:
    case TS_CHAR:  return count;
    case TS_WORD:
    case TS_INT:   return (uint32_t)count * 2;
    case TS_DWORD:
    case TS_DINT:
    case TS_REAL:  return (uint32_t)count * 4;
    default:       return 0;
  }
}

} // namespace

// ---------------- Konfiguracja ----------------
S7Server::S7Server(uint16_t port) : server(port), port(port) {}

void S7Server::begin()
{
  server.begin();
  if (debug) {
    debug->print(F("S7Server: "));
    debug->print(Ethernet.localIP());
    debug->print(F(":"));
    debug->println(port);
  }
}

bool S7Server::addDB(uint16_t number, byte* buffer, uint16_t size)
{
  DataBlock* block = findDB(number);
  if (!block) {
    if (dbCount >= MAX_DBS) return false;
    block = &dbs[dbCount++];
  }
  block->number     = number;
  block->data       = buffer;
  block->size       = size;
  block->writeStart = 0;
  block->writeEnd   = size;
  return true;
}

bool S7Server::setWriteRange(uint16_t number, uint16_t start, uint16_t length)
{
  DataBlock* block = findDB(number);
  if (!block || (uint32_t)start + length > block->size) return false;
  block->writeStart = start;
  block->writeEnd   = start + length;
  return true;
}

bool S7Server::setReadOnly(uint16_t number)
{
  return setWriteRange(number, 0, 0);
}

void S7Server::onWrite(S7WriteCallback callback)
{
  writeCallback = callback;
}

void S7Server::setDebug(Print& out)
{
  debug = &out;
}

uint8_t S7Server::clientCount()
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
    if (clients[i].connected()) n++;
  }
  return n;
}

// ---------------- Dostęp do danych ----------------
S7Server::DataBlock* S7Server::findDB(uint16_t number)
{
  for (uint8_t i = 0; i < dbCount; i++) {
    if (dbs[i].number == number) return &dbs[i];
  }
  return nullptr;
}

byte* S7Server::area(uint16_t db, uint16_t offset, uint16_t size)
{
  DataBlock* block = findDB(db);
  if (!block || (uint32_t)offset + size > block->size) return nullptr;
  return block->data + offset;
}

bool S7Server::getBit(uint16_t db, uint16_t offset, uint8_t bit)
{
  byte* p = area(db, offset, 1);
  return p && bit < 8 && ((*p >> bit) & 0x01);
}

uint8_t S7Server::getByte(uint16_t db, uint16_t offset)
{
  byte* p = area(db, offset, 1);
  return p ? *p : 0;
}

uint16_t S7Server::getWord(uint16_t db, uint16_t offset)
{
  byte* p = area(db, offset, 2);
  return p ? getU16(p, 0) : 0;
}

int16_t S7Server::getInt(uint16_t db, uint16_t offset)
{
  return (int16_t)getWord(db, offset);
}

uint32_t S7Server::getDWord(uint16_t db, uint16_t offset)
{
  byte* p = area(db, offset, 4);
  return p ? getU32(p) : 0;
}

int32_t S7Server::getDInt(uint16_t db, uint16_t offset)
{
  return (int32_t)getDWord(db, offset);
}

float S7Server::getReal(uint16_t db, uint16_t offset)
{
  uint32_t raw = getDWord(db, offset);
  float value;
  memcpy(&value, &raw, 4);             // AVR, ARM i S7 używają IEEE754, różni się tylko kolejność bajtów
  return value;
}

void S7Server::setBit(uint16_t db, uint16_t offset, uint8_t bit, bool value)
{
  byte* p = area(db, offset, 1);
  if (!p || bit > 7) return;
  if (value) *p |=  (1 << bit);
  else       *p &= ~(1 << bit);
}

void S7Server::setByte(uint16_t db, uint16_t offset, uint8_t value)
{
  byte* p = area(db, offset, 1);
  if (p) *p = value;
}

void S7Server::setWord(uint16_t db, uint16_t offset, uint16_t value)
{
  byte* p = area(db, offset, 2);
  if (p) putU16(p, 0, value);
}

void S7Server::setInt(uint16_t db, uint16_t offset, int16_t value)
{
  setWord(db, offset, (uint16_t)value);
}

void S7Server::setDWord(uint16_t db, uint16_t offset, uint32_t value)
{
  byte* p = area(db, offset, 4);
  if (p) putU32(p, value);
}

void S7Server::setDInt(uint16_t db, uint16_t offset, int32_t value)
{
  setDWord(db, offset, (uint32_t)value);
}

void S7Server::setReal(uint16_t db, uint16_t offset, float value)
{
  uint32_t raw;
  memcpy(&raw, &value, 4);
  setDWord(db, offset, raw);
}

// ---------------- Warstwa TCP / TPKT ----------------
void S7Server::poll()
{
  EthernetClient incoming = server.accept();
  if (incoming) {
    // Wolne miejsce, a jeśli brak - nowe połączenie zastępuje pierwsze (np. po restarcie PLC)
    uint8_t slot = 0;
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i].connected()) { slot = i; break; }
    }
    clients[slot].stop();
    clients[slot] = incoming;
    if (debug) {
      debug->print(F("S7Server: polaczenie od "));
      debug->println(incoming.remoteIP());
    }
  }

  for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
    EthernetClient& c = clients[i];
    if (!c) continue;
    if (!c.connected()) {
      c.stop();
      if (debug) debug->println(F("S7Server: klient rozlaczony"));
      continue;
    }
    if (c.available() < 4) continue;
    uint16_t len = readFrame(c);
    if (len == 0) {
      if (debug) debug->println(F("S7Server: bledna ramka - rozlaczam"));
      c.stop();
      continue;
    }
    handleFrame(c, len);
  }
}

bool S7Server::readExact(EthernetClient& c, byte* dst, uint16_t n)
{
  unsigned long t0 = millis();
  uint16_t got = 0;
  while (got < n) {
    int r = c.read(dst + got, n - got);
    if (r > 0) {
      got += r;
      t0 = millis();
    } else if (!c.connected() || millis() - t0 > 1000) {
      return false;
    }
  }
  return true;
}

// Zwraca długość całej ramki (z nagłówkiem TPKT) albo 0 przy błędzie
uint16_t S7Server::readFrame(EthernetClient& c)
{
  if (!readExact(c, rx, 4) || rx[0] != 0x03) return 0;
  uint16_t len = getU16(rx, 2);
  if (len < 7 || len > sizeof(rx)) return 0;
  if (!readExact(c, rx + 4, len - 4)) return 0;
  return len;
}

void S7Server::handleFrame(EthernetClient& c, uint16_t len)
{
  switch (rx[5]) {                     // typ COTP
    case 0xE0:                         // Connection Request
      cotpConfirm(c, len);
      break;
    case 0x80:                         // Disconnect Request
      c.stop();
      break;
    case 0xF0:                         // Data
      if (len <= REQ_PARAMS + 1 || !(rx[6] & 0x80)) break;   // pomijamy fragmenty (przy PDU 240 nie występują)
      if (rx[7] != 0x32 || rx[8] != 0x01) break;            // tylko S7 Job
      switch (rx[REQ_PARAMS]) {
        case 0xF0: setupCommunication(c, len); break;
        case 0x04: readVar(c, len);            break;
        case 0x05: writeVar(c, len);           break;
        default:
          if (debug) {
            debug->print(F("S7Server: nieobslugiwana funkcja 0x"));
            debug->println(rx[REQ_PARAMS], HEX);
          }
      }
      break;
  }
}

// ---------------- Warstwa COTP ----------------
// Connection Confirm = kopia Connection Request z typem 0xD0 i zamienionymi referencjami
// (tak jak w Snap7 - PLC jako klient wymaga zamiany referencji)
void S7Server::cotpConfirm(EthernetClient& c, uint16_t len)
{
  if (len < 11) return;
  memcpy(tx, rx, len);
  tx[5] = 0xD0;
  tx[6] = rx[8];                       // referencja docelowa = referencja klienta
  tx[7] = rx[9];
  tx[8] = 0x00;                        // nasza referencja
  tx[9] = 0x01;
  c.write(tx, len);
}

// ---------------- Warstwa S7 ----------------
// Nagłówek odpowiedzi Ack_Data; długości uzupełnia sendAck
void S7Server::beginAck()
{
  tx[0]  = 0x03; tx[1] = 0x00;         // TPKT
  tx[4]  = 0x02; tx[5] = 0xF0; tx[6] = 0x80;   // COTP DT, ostatni fragment
  tx[7]  = 0x32;                       // identyfikator protokołu S7
  tx[8]  = 0x03;                       // ROSCTR = Ack_Data
  tx[9]  = 0x00; tx[10] = 0x00;
  tx[11] = rx[11]; tx[12] = rx[12];    // numer PDU (PDU reference) z zapytania
  tx[17] = 0x00; tx[18] = 0x00;        // klasa i kod błędu: brak
}

void S7Server::sendAck(EthernetClient& c, uint16_t parLen, uint16_t dataLen)
{
  uint16_t total = RES_PARAMS + parLen + dataLen;
  putU16(tx, 2, total);
  putU16(tx, 13, parLen);
  putU16(tx, 15, dataLen);
  c.write(tx, total);
}

void S7Server::setupCommunication(EthernetClient& c, uint16_t len)
{
  if (len < REQ_PARAMS + 8) return;
  beginAck();
  memcpy(tx + RES_PARAMS, rx + REQ_PARAMS, 6);   // F0 00 + liczba równoległych zadań (oferujemy to samo)
  putU16(tx, RES_PARAMS + 6, PDU_SIZE);
  sendAck(c, 8, 0);
}

// Sprawdza opis zmiennej (12 bajtów S7ANY) i wylicza jej położenie w DB
byte S7Server::checkItem(const byte* item, bool write, DataBlock*& block,
                         uint16_t& start, uint8_t& bit, uint16_t& size)
{
  if (item[0] != 0x12 || item[1] != 0x0A || item[2] != 0x10) return RC_BAD_TYPE;
  byte ts = item[3];
  uint32_t bytes = itemBytes(ts, getU16(item, 4));
  if (bytes == 0) return RC_BAD_TYPE;
  block = (item[8] == AREA_DB) ? findDB(getU16(item, 6)) : nullptr;
  if (!block) return RC_NOT_FOUND;

  uint32_t addr = itemAddress(item);
  bit = addr & 0x07;
  uint32_t first = addr >> 3;
  if (ts != TS_BIT && bit != 0) return RC_OUT_OF_RANGE;
  if (first + bytes > block->size) return RC_OUT_OF_RANGE;
  if (write && (first < block->writeStart || first + bytes > block->writeEnd)) return RC_ACCESS_DENIED;

  start = first;
  size  = bytes;
  return RC_OK;
}

void S7Server::logItemError(char op, const byte* item, byte rc)
{
  if (!debug) return;
  debug->print(F("S7Server: "));
  debug->print(op);
  debug->print(F(" DB")); debug->print(getU16(item, 6));
  debug->print(F(".DBB")); debug->print(itemAddress(item) >> 3);
  debug->print(F(" len ")); debug->print(getU16(item, 4));
  debug->print(F(" -> blad 0x")); debug->println(rc, HEX);
}

// GET z PLC
void S7Server::readVar(EthernetClient& c, uint16_t len)
{
  uint8_t count = rx[REQ_PARAMS + 1];
  if (count == 0 || REQ_PARAMS + 2 + (uint16_t)count * ITEM_SIZE > len) return;

  beginAck();
  tx[RES_PARAMS]     = 0x04;
  tx[RES_PARAMS + 1] = count;
  uint16_t d = RES_PARAMS + 2;

  for (uint8_t i = 0; i < count; i++) {
    const byte* item = rx + REQ_PARAMS + 2 + i * ITEM_SIZE;
    DataBlock* block = nullptr;
    uint16_t start = 0, size = 0;
    uint8_t  bit = 0;
    byte rc = checkItem(item, false, block, start, bit, size);
    if (rc == RC_OK && d + 4 + size + 1 > FRAME_MAX) rc = RC_OUT_OF_RANGE;   // nie mieści się w PDU

    tx[d] = rc;
    if (rc != RC_OK) {
      logItemError('R', item, rc);
      tx[d + 1] = 0x00;
      putU16(tx, d + 2, 0);
      d += 4;
      continue;
    }

    byte ts = item[3];
    if (ts == TS_BIT) {
      tx[d + 1] = TSD_BIT;
      putU16(tx, d + 2, 1);
      tx[d + 4] = (block->data[start] >> bit) & 0x01;
    } else {
      memcpy(tx + d + 4, block->data + start, size);
      if (ts == TS_REAL) {
        tx[d + 1] = TSD_REAL;
        putU16(tx, d + 2, size);
      } else if (ts == TS_CHAR) {
        tx[d + 1] = TSD_OCTET;
        putU16(tx, d + 2, size);
      } else if (ts == TS_INT || ts == TS_DINT) {
        tx[d + 1] = TSD_INT;
        putU16(tx, d + 2, size * 8);
      } else {
        tx[d + 1] = TSD_BYTE;
        putU16(tx, d + 2, size * 8);
      }
    }
    d += 4 + size;
    if ((size & 1) && i < count - 1) tx[d++] = 0x00;   // S7 wyrównuje zmienne do parzystej długości
  }
  sendAck(c, 2, d - RES_PARAMS - 2);
  reads++;
}

// PUT z PLC
void S7Server::writeVar(EthernetClient& c, uint16_t len)
{
  uint8_t  count = rx[REQ_PARAMS + 1];
  uint16_t p = REQ_PARAMS + getU16(rx, 13);            // początek sekcji danych
  if (count == 0 || REQ_PARAMS + 2 + (uint16_t)count * ITEM_SIZE > p || p > len) return;

  beginAck();
  tx[RES_PARAMS]     = 0x05;
  tx[RES_PARAMS + 1] = count;

  for (uint8_t i = 0; i < count; i++) {
    const byte* item = rx + REQ_PARAMS + 2 + i * ITEM_SIZE;
    DataBlock* block = nullptr;
    uint16_t start = 0, size = 0;
    byte rc = RC_SIZE_MISMATCH;

    if (p + 4 <= len) {
      byte dts = rx[p + 1];
      uint16_t dlen = getU16(rx, p + 2);
      if (dts != TSD_OCTET && dts != TSD_REAL && dts != TSD_BIT) dlen /= 8;   // długość podana w bitach
      const byte* src = rx + p + 4;
      p += 4 + dlen;
      if ((dlen & 1) && i < count - 1) p++;

      uint8_t bit = 0;
      rc = checkItem(item, true, block, start, bit, size);
      if (rc == RC_OK && (dlen != size || src + size > rx + len)) rc = RC_SIZE_MISMATCH;
      if (rc == RC_OK) {
        if (item[3] == TS_BIT) {
          if (src[0] & 0x01) block->data[start] |=  (1 << bit);
          else               block->data[start] &= ~(1 << bit);
        } else {
          memcpy(block->data + start, src, size);
        }
      }
    }
    if (rc != RC_OK) logItemError('W', item, rc);
    tx[RES_PARAMS + 2 + i] = rc;
    if (rc == RC_OK && writeCallback) writeCallback(block->number, start, size);
  }
  sendAck(c, 2, count);
  writes++;
}
