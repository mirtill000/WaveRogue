/*
  Example: ESP32_SPI_card_current_profile
  Bus: SPI transport through ST25R3916 RFAL, with ESP32-S3 software SPI fallback
  Default wiring: ESP32-S3 SCK=12/MISO=13/MOSI=11/SS=10/IRQ=4

  Goal:
    Measure current for reader operating modes that involve actual card polling
    and UID activation. Commands intentionally do not overlap with
    ESP32_SPI_power_profile.

  AccessPort note:
    Disable DTR and RTS when opening the native USB serial port, otherwise an
    ESP32-S3 can be reset into ROM download mode.
*/

#include <Arduino.h>
#include <SPI.h>

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <st25r3916_config.h>
#include <st_errno.h>

namespace {

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t kSpiBus = ST25R3916_DEFAULT_SPI_BUS;
constexpr int kPinSck = ST25R3916_DEFAULT_SPI_SCK_PIN;
constexpr int kPinMiso = ST25R3916_DEFAULT_SPI_MISO_PIN;
constexpr int kPinMosi = ST25R3916_DEFAULT_SPI_MOSI_PIN;
constexpr int kPinSs = ST25R3916_DEFAULT_SPI_SS_PIN;
constexpr int kPinIrq = ST25R3916_DEFAULT_IRQ_PIN;
constexpr int kPinLed = ST25R3916_DEFAULT_LED_PIN;
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ESP32C3_DEV)
constexpr uint8_t kSpiBus = ST25R3916_DEFAULT_SPI_BUS;
constexpr int kPinSck = ST25R3916_DEFAULT_SPI_SCK_PIN;
constexpr int kPinMiso = ST25R3916_DEFAULT_SPI_MISO_PIN;
constexpr int kPinMosi = ST25R3916_DEFAULT_SPI_MOSI_PIN;
constexpr int kPinSs = ST25R3916_DEFAULT_SPI_SS_PIN;
constexpr int kPinIrq = ST25R3916_DEFAULT_IRQ_PIN;
constexpr int kPinLed = ST25R3916_DEFAULT_LED_PIN;
#else
constexpr uint8_t kSpiBus = ST25R3916_DEFAULT_SPI_BUS;
constexpr int kPinSck = ST25R3916_DEFAULT_SPI_SCK_PIN;
constexpr int kPinMiso = ST25R3916_DEFAULT_SPI_MISO_PIN;
constexpr int kPinMosi = ST25R3916_DEFAULT_SPI_MOSI_PIN;
constexpr int kPinSs = ST25R3916_DEFAULT_SPI_SS_PIN;
constexpr int kPinIrq = ST25R3916_DEFAULT_IRQ_PIN;
constexpr int kPinLed = ST25R3916_DEFAULT_LED_PIN;
#endif

constexpr uint32_t kOneShotTimeoutMs = 5000UL;
constexpr uint32_t kBurstDurationMs = 30000UL;
constexpr uint32_t kRestartGapMs = 80UL;
constexpr uint32_t kPeakTrainGapMs = 20UL;
constexpr uint32_t kReportPeriodMs = 5000UL;

enum class TestMode : uint8_t {
  Off,
  EspBaseline,
  NoCardPolling,
  OneShotUid,
  ContinuousUid,
  PeakUidTrain,
  BurstTransactions,
};

struct TestStats {
  uint32_t startedMs = 0;
  uint32_t cycles = 0;
  uint32_t uidReads = 0;
  uint32_t errors = 0;
  uint32_t totalUidMs = 0;
  uint32_t maxUidMs = 0;
  char lastId[48] = {0};
  char lastType[16] = {0};
};

SPIClass gSpi(kSpiBus);
RfalRfST25R3916Class gReader(&gSpi, kPinSs, kPinIrq);
RfalNfcClass gNfc(&gReader);

TestMode gMode = TestMode::Off;
TestStats gStats;
bool gNfcInitialized = false;
uint32_t gNextPollMs = 0;
uint32_t gBurstEndsMs = 0;
uint32_t gLastReportMs = 0;

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

void resetStats()
{
  gStats = TestStats();
  gStats.startedMs = millis();
  gLastReportMs = gStats.startedMs;
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

void formatId(const uint8_t *id, uint8_t idLen, char *out, size_t outLen)
{
  size_t written = 0;
  out[0] = '\0';

  for (uint8_t i = 0; i < idLen; i++) {
    written += (size_t)snprintf(out + written, (written < outLen) ? (outLen - written) : 0U, "%02X", id[i]);
    if ((i + 1U < idLen) && (written + 1U < outLen)) {
      out[written++] = ' ';
      out[written] = '\0';
    }
    if (written >= outLen) {
      out[outLen - 1U] = '\0';
      return;
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
    Serial.print("rfalNfcInitialize failed: ");
    Serial.println((int)err);
    gStats.errors++;
    return false;
  }

  gNfcInitialized = true;
  return true;
}

bool ensureNfcInitialized()
{
  return initializeNfc(false);
}

bool reinitializeNfc()
{
  return initializeNfc(true);
}

void stopDiscoveryAndField()
{
  if (gNfcInitialized) {
    (void)gReader.rfalFieldOff();
  }
  gNextPollMs = 0;
  digitalWrite(kPinLed, LOW);
}

ReturnCode pollNfcaOnceRaw(rfalNfcaListenDevice *device)
{
  if (device == NULL) {
    return ERR_PARAM;
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

ReturnCode pollNfcaOnce(rfalNfcaListenDevice *device)
{
  ReturnCode err = pollNfcaOnceRaw(device);
  if (err != ERR_WRONG_STATE) {
    return err;
  }

  if (!reinitializeNfc()) {
    return err;
  }

  return pollNfcaOnceRaw(device);
}

bool runOneUidTransaction(bool printNoCard, bool printCard)
{
  if (!ensureNfcInitialized()) {
    return false;
  }

  const uint32_t startedMs = millis();
  rfalNfcaListenDevice device = {};
  gStats.cycles++;

  const ReturnCode err = pollNfcaOnce(&device);
  const uint32_t uidMs = millis() - startedMs;

  if (err == ERR_NONE) {
    char id[sizeof(gStats.lastId)];
    formatId(device.nfcId1, device.nfcId1Len, id, sizeof(id));

    strncpy(gStats.lastType, "A", sizeof(gStats.lastType) - 1U);
    gStats.lastType[sizeof(gStats.lastType) - 1U] = '\0';
    strncpy(gStats.lastId, id, sizeof(gStats.lastId) - 1U);
    gStats.lastId[sizeof(gStats.lastId) - 1U] = '\0';
    gStats.uidReads++;
    gStats.totalUidMs += uidMs;
    if (uidMs > gStats.maxUidMs) {
      gStats.maxUidMs = uidMs;
    }

    if (printCard) {
      Serial.print("CARD type=A uid=");
      Serial.print(id);
      Serial.print(" tx_ms=");
      Serial.println(uidMs);
    }
    digitalWrite(kPinLed, HIGH);
    return true;
  }

  digitalWrite(kPinLed, LOW);
  if (err == ERR_TIMEOUT) {
    if (printNoCard) {
      Serial.print("NO_CARD tx_ms=");
      Serial.println(uidMs);
    }
    return false;
  }

  gStats.errors++;
  Serial.print("POLL failed: ");
  Serial.print((int)err);
  Serial.print(" ");
  Serial.println(returnCodeToString(err));
  return false;
}

void printStats()
{
  const uint32_t now = millis();
  const uint32_t elapsedMs = now - gStats.startedMs;
  const uint32_t avgUidMs = (gStats.uidReads == 0U) ? 0U : (gStats.totalUidMs / gStats.uidReads);

  Serial.print("STATS elapsed_ms=");
  Serial.print(elapsedMs);
  Serial.print(" cycles=");
  Serial.print(gStats.cycles);
  Serial.print(" uid_reads=");
  Serial.print(gStats.uidReads);
  Serial.print(" errors=");
  Serial.print(gStats.errors);
  Serial.print(" avg_uid_ms=");
  Serial.print(avgUidMs);
  Serial.print(" max_uid_ms=");
  Serial.print(gStats.maxUidMs);
  Serial.print(" last=");
  Serial.print(gStats.lastType);
  Serial.print(":");
  Serial.println(gStats.lastId);
  Serial.println("Energy per UID estimate: mA*s = measured_mA * avg_uid_ms / 1000.");
  Serial.println("For no-card polling, use measured average current directly for this mode.");
}

void enterOff()
{
  stopDiscoveryAndField();
  setQuietPins();
  gMode = TestMode::Off;
  Serial.println("MODE O: RF off / idle. Record idle current if needed.");
}

void enterEspBaseline()
{
  stopDiscoveryAndField();
  pinMode(kPinSs, OUTPUT);
  digitalWrite(kPinSs, LOW);
  pinMode(kPinSck, OUTPUT);
  digitalWrite(kPinSck, LOW);
  pinMode(kPinMosi, OUTPUT);
  digitalWrite(kPinMosi, LOW);
  pinMode(kPinMiso, INPUT);
  pinMode(kPinIrq, INPUT);
  digitalWrite(kPinLed, LOW);
  gMode = TestMode::EspBaseline;
  Serial.println("MODE E: ESP baseline. For ESP-only current, disconnect ST module VCC.");
}

void enterNoCardPolling()
{
  stopDiscoveryAndField();
  (void)reinitializeNfc();
  resetStats();
  gMode = TestMode::NoCardPolling;
  gNextPollMs = 0;
  Serial.println("MODE U: no-card polling current. Keep cards away and record average current.");
}

void enterOneShotUid()
{
  stopDiscoveryAndField();
  (void)reinitializeNfc();
  resetStats();
  gMode = TestMode::OneShotUid;
  gNextPollMs = 0;
  Serial.println("MODE C: one UID transaction. Place card now; each attempt turns RF off after UID/no-card.");
}

void enterContinuousUid()
{
  stopDiscoveryAndField();
  (void)reinitializeNfc();
  resetStats();
  gMode = TestMode::ContinuousUid;
  gNextPollMs = 0;
  Serial.println("MODE L: continuous UID loop, low serial output. Send O to stop.");
}

void enterPeakTrain()
{
  stopDiscoveryAndField();
  (void)reinitializeNfc();
  resetStats();
  gMode = TestMode::PeakUidTrain;
  gNextPollMs = 0;
  Serial.println("MODE T: repeated UID transaction pulses for peak-current capture. Send O to stop.");
}

void enterBurst()
{
  stopDiscoveryAndField();
  (void)reinitializeNfc();
  resetStats();
  gMode = TestMode::BurstTransactions;
  gBurstEndsMs = millis() + kBurstDurationMs;
  gNextPollMs = 0;
  Serial.println("MODE B: 30 s UID transaction burst. Keep card near antenna and record average current.");
}

void printMenu()
{
  Serial.println();
  Serial.println("Card-current commands, no overlap with power_profile:");
  Serial.println("  M  Print this menu.");
  Serial.println("  O  RF off / idle.");
  Serial.println("  E  ESP baseline; disconnect ST VCC for ESP-only current.");
  Serial.println("  U  No-card polling average current.");
  Serial.println("  C  One UID read transaction for peak/current capture.");
  Serial.println("  L  Continuous UID loop for card-present working current, low output.");
  Serial.println("  T  Repeated UID transaction pulses for peak-current capture, low output.");
  Serial.println("  B  30 s transaction burst; prints avg/max UID transaction time, low output.");
  Serial.println("  D  Print accumulated stats.");
  Serial.println();
}

void handleCommand(char cmd)
{
  switch (cmd) {
    case 'M':
    case 'm':
      printMenu();
      break;
    case 'O':
    case 'o':
      enterOff();
      break;
    case 'E':
    case 'e':
      enterEspBaseline();
      break;
    case 'U':
    case 'u':
      enterNoCardPolling();
      break;
    case 'C':
    case 'c':
      enterOneShotUid();
      break;
    case 'L':
    case 'l':
      enterContinuousUid();
      break;
    case 'T':
    case 't':
      enterPeakTrain();
      break;
    case 'B':
    case 'b':
      enterBurst();
      break;
    case 'D':
    case 'd':
      printStats();
      break;
    case '\r':
    case '\n':
      break;
    default:
      Serial.println("Unknown command. Send M for menu.");
      break;
  }
}

void runMode()
{
  const uint32_t now = millis();

  switch (gMode) {
    case TestMode::NoCardPolling:
    case TestMode::ContinuousUid:
    case TestMode::PeakUidTrain:
    case TestMode::BurstTransactions:
      if (now >= gNextPollMs) {
        const bool found = runOneUidTransaction(false, false);
        (void)found;
        gNextPollMs = millis() + ((gMode == TestMode::PeakUidTrain) ? kPeakTrainGapMs : kRestartGapMs);
      }

      if ((now - gLastReportMs) >= kReportPeriodMs) {
        gLastReportMs = now;
        printStats();
      }

      if ((gMode == TestMode::BurstTransactions) && (now >= gBurstEndsMs)) {
        printStats();
        enterOff();
      }
      break;

    case TestMode::OneShotUid:
      if (now >= gNextPollMs) {
        if (runOneUidTransaction(false, true)) {
          printStats();
          enterOff();
          break;
        }
        gNextPollMs = millis() + kRestartGapMs;
      }

      if ((now - gStats.startedMs) >= kOneShotTimeoutMs) {
        Serial.println("One UID transaction timeout.");
        printStats();
        enterOff();
      }
      break;

    default:
      delay(20);
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
  Serial.println("ESP32 + ST25R3916B card-current profile test");
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
  enterOff();
}

void loop()
{
  while (Serial.available() > 0) {
    handleCommand((char)Serial.read());
  }

  runMode();
}
