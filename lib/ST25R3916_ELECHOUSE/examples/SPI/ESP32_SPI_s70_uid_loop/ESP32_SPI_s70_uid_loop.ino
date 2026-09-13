/*
  Example: ESP32_SPI_s70_uid_loop
  Bus: SPI
  Default wiring: ESP32 SCK=18/MISO=19/MOSI=23/SS=5; ESP32-S3 SCK=12/MISO=13/MOSI=11/SS=10; ESP32-C3 SCK=2/MISO=10/MOSI=3/SS=7; IRQ=4 (ESP32/S3) or GPIO6 (C3); LED=2 (ESP32/S3) or GPIO12 (C3, optional)
  Target card: MIFARE One S70 / MIFARE Classic 4K

  Behavior:
    - No card: keep polling for an ISO14443A card.
    - S70 card present: continuously print UID, ATQA, and SAK.
    - AWS defaults to enabled. Edit kEnableAws below to disable it.
    - No authentication, no block reads, and no card writes are performed.
*/

#include <Arduino.h>
#include <SPI.h>

#include <rfal_mf1.h>
#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <st25r3916_config.h>
#include <st_errno.h>

namespace {

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
// ESP32-S3-N16R8 default SPI wiring. Edit these values to match your board.
constexpr uint8_t kSpiBus = FSPI;
constexpr int kPinSck = 12;
constexpr int kPinMiso = 13;
constexpr int kPinMosi = 11;
constexpr int kPinSs = 10;
constexpr int kPinIrq = 4;
constexpr int kPinLed = 2;
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ESP32C3_DEV)
// ESP32-C3 default SPI wiring. Edit these values to match your board.
constexpr uint8_t kSpiBus = FSPI;
constexpr int kPinSck = 2;
constexpr int kPinMiso = 10;
constexpr int kPinMosi = 3;
constexpr int kPinSs = 7;
constexpr int kPinIrq = 6;
constexpr int kPinLed = 12;
#else
// Classic ESP32 default SPI wiring. Edit these values to match your board.
constexpr uint8_t kSpiBus = VSPI;
constexpr int kPinSck = 18;
constexpr int kPinMiso = 19;
constexpr int kPinMosi = 23;
constexpr int kPinSs = 5;
constexpr int kPinIrq = 4;
constexpr int kPinLed = 2;
#endif

// AWS = active wave shaping. On ST25R3916/ST25R3916B this example maps it to
// the default overshoot/undershoot protection register values used for NFC-A.
constexpr bool kEnableAws = true;

constexpr uint8_t kAwsOnOvershootConf1 = 0x40U;
constexpr uint8_t kAwsOnOvershootConf2 = 0x03U;
constexpr uint8_t kAwsOnUndershootConf1 = 0x40U;
constexpr uint8_t kAwsOnUndershootConf2 = 0x03U;
constexpr uint8_t kAwsOffRegisterValue = 0x00U;

constexpr unsigned long kNoCardMessageIntervalMs = 2000UL;
constexpr unsigned long kPollDelayMs = 150UL;

SPIClass gSpi(kSpiBus);
RfalRfST25R3916Class gReader(&gSpi, kPinSs, kPinIrq);
RfalNfcClass gNfc(&gReader);

unsigned long gLastCardPrintMs = 0UL;
unsigned long gLastNoCardMessageMs = 0UL;

void waitForSerial()
{
  const unsigned long start = millis();
  while (!Serial && ((millis() - start) < 2000UL)) {
    delay(10);
  }
}

void printBytesInline(const uint8_t *buf, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    if (buf[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(buf[i], HEX);
    if (i + 1U < len) {
      Serial.print(' ');
    }
  }
}

void printHexByte(uint8_t value)
{
  if (value < 0x10U) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

ReturnCode writeAwsRegister(uint8_t reg, uint8_t value, const char *name, bool verbose)
{
  ReturnCode err = gReader.st25r3916WriteRegister(reg, value);
  if (err != ERR_NONE) {
    if (verbose) {
      Serial.print(name);
      Serial.print(" write failed: ");
      Serial.println((int)err);
    }
    return err;
  }

  uint8_t readback = 0U;
  err = gReader.st25r3916ReadRegister(reg, &readback);
  if (err != ERR_NONE) {
    if (verbose) {
      Serial.print(name);
      Serial.print(" readback failed: ");
      Serial.println((int)err);
    }
    return err;
  }

  if (verbose) {
    Serial.print("  ");
    Serial.print(name);
    Serial.print(" = 0x");
    printHexByte(value);
    Serial.print(" readback 0x");
    printHexByte(readback);
    Serial.println();
  }

  return (readback == value) ? ERR_NONE : ERR_IO;
}

ReturnCode applyAwsConfig(bool verbose)
{
  struct AwsRegisterSetting {
    uint8_t reg;
    uint8_t value;
    const char *name;
  };

  const AwsRegisterSetting settings[] = {
    { ST25R3916_REG_OVERSHOOT_CONF1,  kEnableAws ? kAwsOnOvershootConf1 : kAwsOffRegisterValue, "OVERSHOOT_CONF1" },
    { ST25R3916_REG_OVERSHOOT_CONF2,  kEnableAws ? kAwsOnOvershootConf2 : kAwsOffRegisterValue, "OVERSHOOT_CONF2" },
    { ST25R3916_REG_UNDERSHOOT_CONF1, kEnableAws ? kAwsOnUndershootConf1 : kAwsOffRegisterValue, "UNDERSHOOT_CONF1" },
    { ST25R3916_REG_UNDERSHOOT_CONF2, kEnableAws ? kAwsOnUndershootConf2 : kAwsOffRegisterValue, "UNDERSHOOT_CONF2" },
  };

  if (verbose) {
    Serial.print("AWS active wave shaping: ");
    Serial.println(kEnableAws ? "ON" : "OFF");
  }

  for (size_t i = 0; i < (sizeof(settings) / sizeof(settings[0])); i++) {
    const ReturnCode err = writeAwsRegister(settings[i].reg, settings[i].value, settings[i].name, verbose);
    if (err != ERR_NONE) {
      return err;
    }
  }

  return ERR_NONE;
}

bool isLikelyS70(const rfalNfcaListenDevice &device)
{
  return (device.sensRes.anticollisionInfo == 0x02U) &&
         (device.sensRes.platformInfo == 0x00U) &&
         (device.selRes.sak == 0x18U);
}

void printS70Card(const rfalNfcaListenDevice &device)
{
  const uint8_t atqa[2] = {
    device.sensRes.anticollisionInfo,
    device.sensRes.platformInfo
  };

  Serial.print("S70 UID: ");
  printBytesInline(device.nfcId1, device.nfcId1Len);
  Serial.print("  ATQA: ");
  printBytesInline(atqa, sizeof(atqa));
  Serial.print("  SAK: ");
  printHexByte(device.selRes.sak);
  Serial.println();
}

void printNonS70Card(const rfalNfcaListenDevice &device)
{
  const uint8_t atqa[2] = {
    device.sensRes.anticollisionInfo,
    device.sensRes.platformInfo
  };

  Serial.print("ISO14443A card is not S70/MIFARE Classic 4K. UID: ");
  printBytesInline(device.nfcId1, device.nfcId1Len);
  Serial.print("  ATQA: ");
  printBytesInline(atqa, sizeof(atqa));
  Serial.print("  SAK: ");
  printHexByte(device.selRes.sak);
  Serial.println();
}

ReturnCode pollNfcaOnce(rfalNfcaListenDevice *device)
{
  if (device == NULL) {
    return ERR_PARAM;
  }

  *device = {};

  ReturnCode err = gNfc.rfalNfcaPollerInitialize();
  if (err != ERR_NONE) {
    gReader.rfalFieldOff();
    return err;
  }

  err = applyAwsConfig(false);
  if (err != ERR_NONE) {
    gReader.rfalFieldOff();
    return err;
  }

  err = gReader.rfalFieldOnAndStartGT();
  if (err != ERR_NONE) {
    gReader.rfalFieldOff();
    return err;
  }

  rfalNfcaSensRes sensRes = {};
  err = gNfc.rfalNfcaPollerTechnologyDetection(RFAL_COMPLIANCE_MODE_ISO, &sensRes);
  if (err != ERR_NONE) {
    gReader.rfalFieldOff();
    return err;
  }

  device->sensRes = sensRes;

  uint8_t devCnt = 0U;
  err = gNfc.rfalNfcaPollerFullCollisionResolution(RFAL_COMPLIANCE_MODE_ISO, 1U, device, &devCnt);
  if (err == ERR_NONE) {
    if (devCnt == 0U) {
      err = ERR_TIMEOUT;
    } else {
      gNfc.rfalNfcaPollerSleep();
    }
  }

  gReader.rfalFieldOff();
  return err;
}

void printStartupPins()
{
  Serial.println("ESP32 SPI S70 UID loop");
  Serial.println("Reader chip: ST25R3916 / ST25R3916B");
  Serial.print("SCK=");
  Serial.print(kPinSck);
  Serial.print(" MISO=");
  Serial.print(kPinMiso);
  Serial.print(" MOSI=");
  Serial.print(kPinMosi);
  Serial.print(" SS=");
  Serial.print(kPinSs);
  Serial.print(" IRQ=");
  Serial.print(kPinIrq);
  Serial.print(" LED=");
  Serial.println(kPinLed);
  Serial.print("AWS=");
  Serial.println(kEnableAws ? "ON" : "OFF");
  Serial.println("No card writes are performed.");
  Serial.println("Searching for a MIFARE One S70 / MIFARE Classic 4K card...");
}

} // namespace

void setup()
{
  Serial.begin(115200);
  waitForSerial();

  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, LOW);
  pinMode(kPinSs, OUTPUT);
  digitalWrite(kPinSs, HIGH);

  printStartupPins();

  gSpi.begin(kPinSck, kPinMiso, kPinMosi, kPinSs);

  ReturnCode err = gNfc.rfalNfcInitialize();
  if (err != ERR_NONE) {
    Serial.print("rfalNfcInitialize failed: ");
    Serial.println((int)err);
    while (true) {
      delay(250);
    }
  }

  err = applyAwsConfig(true);
  if (err != ERR_NONE) {
    Serial.print("AWS register setup failed: ");
    Serial.println((int)err);
    while (true) {
      delay(250);
    }
  }
}

void loop()
{
  rfalNfcaListenDevice device = {};
  const ReturnCode err = pollNfcaOnce(&device);

  const unsigned long now = millis();

  if (err == ERR_NONE) {
    digitalWrite(kPinLed, HIGH);
    gLastCardPrintMs = now;
    if (isLikelyS70(device)) {
      printS70Card(device);
    } else if ((now - gLastNoCardMessageMs) > kNoCardMessageIntervalMs) {
      gLastNoCardMessageMs = now;
      printNonS70Card(device);
    }
  } else {
    digitalWrite(kPinLed, LOW);
    if ((err != ERR_TIMEOUT) && ((now - gLastNoCardMessageMs) > kNoCardMessageIntervalMs)) {
      Serial.print("Poll failed: ");
      Serial.println((int)err);
      gLastNoCardMessageMs = now;
    }

    if (((now - gLastCardPrintMs) > kNoCardMessageIntervalMs) &&
        ((now - gLastNoCardMessageMs) > kNoCardMessageIntervalMs)) {
      gLastNoCardMessageMs = now;
      Serial.println("Searching for S70 card...");
    }
  }

  delay(kPollDelayMs);
}
