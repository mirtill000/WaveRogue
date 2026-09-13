/*
  Example: ESP32_SPI_low_power_irq_uid
  Bus: SPI
  Default ESP32-S3 wiring: SCK=12/MISO=13/MOSI=11/SS=10/IRQ=4

  Goal:
    Demonstrate a product-style low-power card-detect flow:
      1. ST25R3916/ST25R3916B enters autonomous hardware Wake-up mode.
      2. A nearby card changes antenna amplitude and the reader asserts IRQ.
      3. The ESP32 notices the IRQ, exits Wake-up mode, reads one ISO14443A UID,
         turns the RF field off, waits for card removal, and re-arms Wake-up mode.

  Notes:
    - Keep cards away when arming Wake-up mode. If a card is already on the
      antenna, the measured reference can be learned with the card present.
    - The sketch performs an active one-shot UID read at boot, so a card already
      on the reader can still validate SPI, IRQ, and RF polling.
    - For the lowest system current, put the host MCU into light/deep sleep after
      "Wake-up armed" and wake it with the same IRQ pin. See
      ESP32_SPI_low_power_wakeup for an ESP32 deep-sleep skeleton.
*/

#include <Arduino.h>
#include <SPI.h>

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <st25r3916_config.h>
#include <st_errno.h>

namespace {

constexpr uint8_t kSpiBus = ST25R3916_DEFAULT_SPI_BUS;
constexpr int kPinSck = ST25R3916_DEFAULT_SPI_SCK_PIN;
constexpr int kPinMiso = ST25R3916_DEFAULT_SPI_MISO_PIN;
constexpr int kPinMosi = ST25R3916_DEFAULT_SPI_MOSI_PIN;
constexpr int kPinSs = ST25R3916_DEFAULT_SPI_SS_PIN;
constexpr int kPinIrq = ST25R3916_DEFAULT_IRQ_PIN;
constexpr int kPinLed = ST25R3916_DEFAULT_LED_PIN;

constexpr rfalWumPeriod kWakeupPeriod = RFAL_WUM_PERIOD_800MS;
constexpr uint8_t kAmplitudeDelta = 2U;
constexpr bool kEnablePhaseWakeup = false;
constexpr uint8_t kPhaseDelta = 2U;
constexpr uint32_t kRemovalPollMs = 700UL;
constexpr uint32_t kStatusPrintMs = 3000UL;

SPIClass gSpi(kSpiBus);
RfalRfST25R3916Class gReader(&gSpi, kPinSs, kPinIrq);
RfalNfcClass gNfc(&gReader);

volatile bool gReaderIrqSeen = false;
bool gNfcInitialized = false;
bool gWakeupArmed = false;
bool gWaitingForRemoval = false;
uint32_t gLastRemovalPollMs = 0;
uint32_t gLastStatusPrintMs = 0;

void readerIrqCallback()
{
  gReaderIrqSeen = true;
}

void waitForSerial()
{
  const unsigned long start = millis();
  while (!Serial && ((millis() - start) < 2000UL)) {
    delay(10);
  }
}

void setQuietPins()
{
  pinMode(kPinSs, OUTPUT);
  digitalWrite(kPinSs, HIGH);
  pinMode(kPinSck, OUTPUT);
  digitalWrite(kPinSck, LOW);
  pinMode(kPinMosi, OUTPUT);
  digitalWrite(kPinMosi, LOW);
  pinMode(kPinMiso, INPUT);
  pinMode(kPinIrq, INPUT);
}

const char *returnCodeToString(ReturnCode err)
{
  switch (err) {
    case ERR_NONE:
      return "ERR_NONE";
    case ERR_TIMEOUT:
      return "ERR_TIMEOUT";
    case ERR_WRONG_STATE:
      return "ERR_WRONG_STATE";
    case ERR_PARAM:
      return "ERR_PARAM";
    case ERR_IO:
      return "ERR_IO";
    case ERR_NOTFOUND:
      return "ERR_NOTFOUND";
    default:
      return "ERR_OTHER";
  }
}

void printReturnCode(const char *label, ReturnCode err)
{
  Serial.print(label);
  Serial.print(": ");
  Serial.print((int)err);
  Serial.print(" ");
  Serial.println(returnCodeToString(err));
}

void printHexBytes(const uint8_t *data, uint8_t len)
{
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    if ((i + 1U) < len) {
      Serial.print(' ');
    }
  }
}

bool initializeNfc(bool force)
{
  if (gNfcInitialized && !force) {
    return true;
  }

  setQuietPins();
  gSpi.begin(kPinSck, kPinMiso, kPinMosi, kPinSs);

  if (gNfcInitialized && force) {
    (void)gReader.rfalDeinitialize();
    delay(2);
  }

  const ReturnCode err = gNfc.rfalNfcInitialize();
  if (err != ERR_NONE) {
    printReturnCode("rfalNfcInitialize", err);
    Serial.println("Check module power, SPI wiring, and IRQ wiring.");
    gNfcInitialized = false;
    return false;
  }

  gReader.st25r3916IRQCallbackSet(readerIrqCallback);
  gNfcInitialized = true;
  return true;
}

bool reinitializeNfc()
{
  return initializeNfc(true);
}

void stopWakeupAndField()
{
  if (gWakeupArmed) {
    (void)gReader.rfalWakeUpModeStop();
    gWakeupArmed = false;
  }
  if (gNfcInitialized) {
    (void)gReader.rfalFieldOff();
  }
  digitalWrite(kPinLed, LOW);
}

ReturnCode pollNfcaOnce(rfalNfcaListenDevice *device)
{
  if (device == NULL) {
    return ERR_PARAM;
  }

  if (!reinitializeNfc()) {
    return ERR_IO;
  }

  *device = {};

  ReturnCode err = gNfc.rfalNfcaPollerInitialize();
  if (err != ERR_NONE) {
    (void)gReader.rfalFieldOff();
    return err;
  }

  err = gReader.rfalFieldOnAndStartGT();
  if (err != ERR_NONE) {
    (void)gReader.rfalFieldOff();
    return err;
  }

  rfalNfcaSensRes sensRes = {};
  err = gNfc.rfalNfcaPollerTechnologyDetection(RFAL_COMPLIANCE_MODE_ISO, &sensRes);
  if (err != ERR_NONE) {
    (void)gReader.rfalFieldOff();
    return err;
  }

  device->sensRes = sensRes;

  uint8_t devCnt = 0U;
  err = gNfc.rfalNfcaPollerFullCollisionResolution(RFAL_COMPLIANCE_MODE_ISO, 1U, device, &devCnt);
  if ((err == ERR_NONE) && (devCnt == 0U)) {
    err = ERR_TIMEOUT;
  }

  if (err == ERR_NONE) {
    (void)gNfc.rfalNfcaPollerSleep();
  }

  (void)gReader.rfalFieldOff();
  return err;
}

bool readAndPrintOneUid(const char *prefix, bool printNoCard)
{
  stopWakeupAndField();

  rfalNfcaListenDevice device = {};
  const uint32_t startedMs = millis();
  const ReturnCode err = pollNfcaOnce(&device);
  const uint32_t elapsedMs = millis() - startedMs;

  if (err == ERR_NONE) {
    Serial.print(prefix);
    Serial.print(" CARD type=ISO14443A uid=");
    printHexBytes(device.nfcId1, device.nfcId1Len);
    Serial.print(" atqa=");
    {
      const uint8_t atqa[] = {device.sensRes.anticollisionInfo, device.sensRes.platformInfo};
      printHexBytes(atqa, sizeof(atqa));
    }
    Serial.print(" sak=0x");
    if (device.selRes.sak < 0x10U) {
      Serial.print('0');
    }
    Serial.print(device.selRes.sak, HEX);
    Serial.print(" read_ms=");
    Serial.println(elapsedMs);
    digitalWrite(kPinLed, HIGH);
    return true;
  }

  digitalWrite(kPinLed, LOW);
  if (err == ERR_TIMEOUT) {
    if (printNoCard) {
      Serial.print(prefix);
      Serial.print(" NO_CARD read_ms=");
      Serial.println(elapsedMs);
    }
    return false;
  }

  Serial.print(prefix);
  Serial.print(" ");
  printReturnCode("POLL", err);
  return false;
}

rfalWakeUpConfig makeWakeupConfig()
{
  rfalWakeUpConfig cfg = {};
  cfg.period = kWakeupPeriod;
  cfg.irqTout = false;
  cfg.swTagDetect = false;

  cfg.indAmp.enabled = true;
  cfg.indAmp.delta = kAmplitudeDelta;
  cfg.indAmp.fracDelta = 0U;
  cfg.indAmp.reference = RFAL_WUM_REFERENCE_AUTO;
  cfg.indAmp.autoAvg = false;
  cfg.indAmp.aaInclMeas = false;
  cfg.indAmp.aaWeight = RFAL_WUM_AA_WEIGHT_16;

  cfg.indPha.enabled = kEnablePhaseWakeup;
  cfg.indPha.delta = kPhaseDelta;
  cfg.indPha.fracDelta = 0U;
  cfg.indPha.reference = RFAL_WUM_REFERENCE_AUTO;
  cfg.indPha.autoAvg = false;
  cfg.indPha.aaInclMeas = false;
  cfg.indPha.aaWeight = RFAL_WUM_AA_WEIGHT_16;

  cfg.cap.enabled = false;
  return cfg;
}

bool armWakeup()
{
  stopWakeupAndField();

  if (!reinitializeNfc()) {
    return false;
  }

  gReaderIrqSeen = false;
  rfalWakeUpConfig cfg = makeWakeupConfig();
  const ReturnCode err = gReader.rfalWakeUpModeStart(&cfg);
  if (err != ERR_NONE) {
    printReturnCode("rfalWakeUpModeStart", err);
    return false;
  }

  gReader.st25r3916IRQCallbackSet(readerIrqCallback);
  gWakeupArmed = true;
  gWaitingForRemoval = false;
  gLastStatusPrintMs = millis();

  Serial.print("Wake-up armed: period_code=0x");
  Serial.print((uint8_t)kWakeupPeriod, HEX);
  Serial.print(" amp_delta=");
  Serial.print(kAmplitudeDelta);
  Serial.print(" phase=");
  Serial.println(kEnablePhaseWakeup ? "on" : "off");
  Serial.println("Approach one ISO14443A card. ST25R3916 IRQ will notify the host.");
  return true;
}

void waitForCardRemoval()
{
  gWaitingForRemoval = true;
  gLastRemovalPollMs = 0;
  Serial.println("Remove the card, then the sketch will re-arm low-power Wake-up.");
}

void processRemovalWait()
{
  if (!gWaitingForRemoval) {
    return;
  }

  const uint32_t now = millis();
  if ((now - gLastRemovalPollMs) < kRemovalPollMs) {
    delay(20);
    return;
  }
  gLastRemovalPollMs = now;

  if (readAndPrintOneUid("REMOVAL_CHECK", false)) {
    if ((now - gLastStatusPrintMs) >= kStatusPrintMs) {
      gLastStatusPrintMs = now;
      Serial.println("Card is still present. Keep it away before low-power calibration.");
    }
    return;
  }

  Serial.println("Card removed. Re-arming Wake-up mode.");
  gWaitingForRemoval = false;
  (void)armWakeup();
}

void processWakeup()
{
  if (!gWakeupArmed) {
    return;
  }

  gReader.rfalWorker();

  const bool irqLineHigh = (digitalRead(kPinIrq) == HIGH);
  if (!gReaderIrqSeen && !irqLineHigh && !gReader.rfalWakeUpModeHasWoke()) {
    if ((millis() - gLastStatusPrintMs) >= kStatusPrintMs) {
      gLastStatusPrintMs = millis();
      Serial.println("Wake-up armed, waiting for ST25R3916 IRQ...");
    }
    delay(20);
    return;
  }

  Serial.print("IRQ event: callback=");
  Serial.print(gReaderIrqSeen ? "yes" : "no");
  Serial.print(" line=");
  Serial.print(irqLineHigh ? "HIGH" : "LOW");
  Serial.print(" wum=");
  const bool wakeupMatched = gReader.rfalWakeUpModeHasWoke();
  Serial.println(wakeupMatched ? "woke" : "pending");

  gReaderIrqSeen = false;
  if (!wakeupMatched) {
    Serial.println("Ignoring non-wakeup IRQ while Wake-up mode remains armed.");
    return;
  }

  stopWakeupAndField();
  delay(20);

  if (readAndPrintOneUid("WAKE", true)) {
    waitForCardRemoval();
  } else {
    Serial.println("No ISO14443A UID after IRQ. Re-arming after a short guard delay.");
    delay(500);
    (void)armWakeup();
  }
}

void printMenu()
{
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  M  Print this menu.");
  Serial.println("  P  Active one-shot ISO14443A UID read, then RF field off.");
  Serial.println("  A  Arm ST25R3916 hardware Wake-up mode. Keep cards away first.");
  Serial.println("  O  Stop Wake-up and turn RF field off.");
  Serial.println("  R  Reinitialize RFAL/ST25R3916.");
  Serial.println();
}

void handleCommand(char cmd)
{
  switch (cmd) {
    case 'M':
    case 'm':
      printMenu();
      break;
    case 'P':
    case 'p':
      (void)readAndPrintOneUid("MANUAL", true);
      break;
    case 'A':
    case 'a':
      (void)armWakeup();
      break;
    case 'O':
    case 'o':
      stopWakeupAndField();
      gWaitingForRemoval = false;
      Serial.println("Wake-up stopped and RF field off.");
      break;
    case 'R':
    case 'r':
      stopWakeupAndField();
      if (reinitializeNfc()) {
        Serial.println("RFAL/ST25R3916 reinitialized.");
      }
      break;
    case '\r':
    case '\n':
      break;
    default:
      Serial.println("Unknown command. Send M for menu.");
      break;
  }
}

} // namespace

void setup()
{
  Serial.begin(115200);
  waitForSerial();

  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, LOW);
  setQuietPins();

  Serial.println();
  Serial.println("ESP32 + ST25R3916B low-power IRQ UID example");
  Serial.print("SPI SCK=");
  Serial.print(kPinSck);
  Serial.print(" MISO=");
  Serial.print(kPinMiso);
  Serial.print(" MOSI=");
  Serial.print(kPinMosi);
  Serial.print(" SS=");
  Serial.print(kPinSs);
  Serial.print(" IRQ=");
  Serial.println(kPinIrq);
#if ST25R3916_FORCE_SOFT_SPI
  Serial.println("Transport: RFAL software SPI fallback enabled.");
#else
  Serial.println("Transport: Arduino hardware SPI.");
#endif
  printMenu();

  if (!initializeNfc(false)) {
    return;
  }

  if (readAndPrintOneUid("BOOT", false)) {
    waitForCardRemoval();
  } else {
    (void)armWakeup();
  }
}

void loop()
{
  while (Serial.available() > 0) {
    handleCommand((char)Serial.read());
  }

  processRemovalWait();
  processWakeup();
}
