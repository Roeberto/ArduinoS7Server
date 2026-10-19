/*
 * S7Server - przykład podstawowy
 * Arduino Uno / Mega + Ethernet Shield W5100 / W5500
 *
 * DB1 (16 bajtów):
 *   Offset  Typ    Nazwa     Kierunek
 *   0.0     Real   Napiecie  Arduino -> PLC (A0 przeliczone na wolty)
 *   4.0     Int    Sekundy   Arduino -> PLC (czas od startu)
 *   6.0     Word   Command   PLC -> Arduino (bit 0 = DBX7.0 steruje pinem 7)
 *
 * PLC (TIA Portal): GET  ADDR_1 = P#DB1.DBX0.0 REAL 1  -> RD_1 = Real
 *                        ADDR_2 = P#DB1.DBX4.0 INT 1   -> RD_2 = Int
 *                   PUT  ADDR_1 = P#DB1.DBX6.0 WORD 1  -> SD_1 = Word
 */
#include <SPI.h>
#include <Ethernet.h>
#include <S7Server.h>

byte      mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x36 };
IPAddress ip(192, 168, 0, 90);

S7Server s7;
byte     db1[16];

const int OUT_PIN = 7;   // nie używaj 10-13 (SPI Ethernetu) ani 4 (karta SD)

void onPlcWrite(uint16_t db, uint16_t start, uint16_t size)
{
  Serial.print(F("PLC zapisal DB"));
  Serial.print(db);
  Serial.print(F(" od bajtu "));
  Serial.print(start);
  Serial.print(F(", bajtow: "));
  Serial.println(size);
}

void setup()
{
  Serial.begin(115200);
  pinMode(OUT_PIN, OUTPUT);
  pinMode(4, OUTPUT);           // odłącz kartę SD na shieldzie
  digitalWrite(4, HIGH);

  Ethernet.init(10);
  Ethernet.begin(mac, ip);

  s7.addDB(1, db1, sizeof(db1));
  s7.setWriteRange(1, 6, 2);    // PLC może zapisywać tylko Command
  s7.onWrite(onPlcWrite);
  s7.setDebug(Serial);
  s7.begin();
}

void loop()
{
  s7.poll();

  s7.setReal(1, 0, analogRead(A0) * 5.0 / 1023.0);
  s7.setInt(1, 4, millis() / 1000);

  digitalWrite(OUT_PIN, s7.getBit(1, 7, 0) ? HIGH : LOW);   // bit 0 słowa DBW6 to DBX7.0
}
