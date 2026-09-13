/*
  Example: ESP32_SPI_power_profile
  Bus: SPI
  Default wiring: ESP32-S3 SCK=12/MISO=13/MOSI=11/SS=10/IRQ=4

  Goal:
    Keep the ESP32 awake and switch ST25R3916/ST25R3916B power scenes by
    Serial commands. This is intended for current measurement with an external
    ammeter while you manually place/remove cards.

  Important:
    The "ESP baseline" modes only measure ESP32 consumption if the ST25R3916
    module VCC is physically disconnected or the ammeter is not measuring the
    module supply. If the module remains powered, total current still includes
    the module's power-on/default current.
    In ESP baseline modes, CS/SCK/MOSI are driven low to avoid back-powering an
    unpowered ST module through the IO protection path.
*/

#include <Arduino.h>
#include <SPI.h>

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <st25r3916_config.h>
#include <st_errno.h>

namespace {

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t kSpiBus = HSPI;
constexpr int kPinSck = 12;
constexpr int kPinMiso = 13;
constexpr int kPinMosi = 11;
constexpr int kPinSs = 10;
constexpr int kPinIrq = 4;
constexpr int kPinLed = 2;
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ESP32C3_DEV)
constexpr uint8_t kSpiBus = FSPI;
constexpr int kPinSck = 2;
constexpr int kPinMiso = 10;
constexpr int kPinMosi = 3;
constexpr int kPinSs = 7;
constexpr int kPinIrq = 6;
constexpr int kPinLed = 12;
#else
constexpr uint8_t kSpiBus = VSPI;
constexpr int kPinSck = 18;
constexpr int kPinMiso = 19;
constexpr int kPinMosi = 23;
constexpr int kPinSs = 5;
constexpr int kPinIrq = 4;
constexpr int kPinLed = 2;
#endif

enum class TestMode : uint8_t {
  EspIdleBaseline,
  EspBusyBaseline,
  StPowerDown,
  StReady,
  StWakeUp,
  StFieldOn,
  StPolling,
};

SPIClass gSpi(kSpiBus);
RfalRfST25R3916Class gReader(&gSpi, kPinSs, kPinIrq);
RfalNfcClass gNfc(&gReader);

TestMode gMode = TestMode::EspIdleBaseline;
bool gStInitialized = false;
bool gPollingStarted = false;
uint32_t gLastWakeReportMs = 0;
uint32_t gBusySink = 0;

constexpr uint8_t kStWriteMode = 0x00U;
constexpr uint8_t kStReadMode = 0x40U;
constexpr uint8_t kStDirectCommandMode = 0xC0U;

void waitForSerial()
{
  const unsigned long start = millis();
  while (!Serial && ((millis() - start) < 2000UL)) {
    delay(10);
  }
}

void setQuietSpiPins()
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

void setNoBackfeedSpiPins()
{
  pinMode(kPinSs, OUTPUT);
  digitalWrite(kPinSs, LOW);
  pinMode(kPinSck, OUTPUT);
  digitalWrite(kPinSck, LOW);
  pinMode(kPinMosi, OUTPUT);
  digitalWrite(kPinMosi, LOW);
  pinMode(kPinMiso, INPUT);
  pinMode(kPinIrq, INPUT);
}

void beginRawStSpi()
{
  pinMode(kPinSs, OUTPUT);
  digitalWrite(kPinSs, HIGH);
  pinMode(kPinSck, OUTPUT);
  digitalWrite(kPinSck, LOW);
  pinMode(kPinMosi, OUTPUT);
  digitalWrite(kPinMosi, LOW);
  pinMode(kPinMiso, INPUT);
}

uint8_t rawStTransfer(uint8_t value)
{
  uint8_t rx = 0U;

  for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U) {
    digitalWrite(kPinSck, HIGH);
    digitalWrite(kPinMosi, ((value & mask) != 0U) ? HIGH : LOW);
    delayMicroseconds(1);

    digitalWrite(kPinSck, LOW);
    rx <<= 1U;
    if (digitalRead(kPinMiso) == HIGH) {
      rx |= 0x01U;
    }
    delayMicroseconds(1);
  }

  return rx;
}

void rawStCommand(uint8_t cmd)
{
  beginRawStSpi();
  digitalWrite(kPinSs, LOW);
  rawStTransfer((uint8_t)(cmd | kStDirectCommandMode));
  digitalWrite(kPinSs, HIGH);
}

void rawStWriteReg(uint8_t reg, uint8_t value)
{
  beginRawStSpi();
  digitalWrite(kPinSs, LOW);
  rawStTransfer((uint8_t)((reg & 0x3FU) | kStWriteMode));
  rawStTransfer(value);
  digitalWrite(kPinSs, HIGH);
}

uint8_t rawStReadReg(uint8_t reg)
{
  beginRawStSpi();
  digitalWrite(kPinSs, LOW);
  rawStTransfer((uint8_t)((reg & 0x3FU) | kStReadMode));
  const uint8_t value = rawStTransfer(0x00U);
  digitalWrite(kPinSs, HIGH);
  return value;
}

void rawStChangeRegBits(uint8_t reg, uint8_t mask, uint8_t value)
{
  const uint8_t oldValue = rawStReadReg(reg);
  rawStWriteReg(reg, (uint8_t)((oldValue & ~mask) | (value & mask)));
}

void rawStClearIrqs()
{
  (void)rawStReadReg(ST25R3916_REG_IRQ_MAIN);
  (void)rawStReadReg(ST25R3916_REG_IRQ_TIMER_NFC);
  (void)rawStReadReg(ST25R3916_REG_IRQ_ERROR_WUP);
  (void)rawStReadReg(ST25R3916_REG_IRQ_TARGET);
}

uint8_t rawStMeasure(uint8_t cmd)
{
  rawStCommand(cmd);
  delay(10);
  return rawStReadReg(ST25R3916_REG_AD_RESULT);
}

void rawStPowerDown()
{
  rawStCommand(ST25R3916_CMD_STOP);
  rawStWriteReg(ST25R3916_REG_WUP_TIMER_CONTROL, 0x00U);
  rawStWriteReg(ST25R3916_REG_IO_CONF1, 0x07U);
  rawStWriteReg(ST25R3916_REG_OP_CONTROL, 0x00U);
}

void rawStReady()
{
  rawStCommand(ST25R3916_CMD_STOP);
  rawStWriteReg(ST25R3916_REG_IRQ_MASK_MAIN, 0xFFU);
  rawStWriteReg(ST25R3916_REG_IRQ_MASK_TIMER_NFC, 0xFFU);
  rawStWriteReg(ST25R3916_REG_IRQ_MASK_ERROR_WUP, 0xFFU);
  rawStWriteReg(ST25R3916_REG_IRQ_MASK_TARGET, 0xFFU);
  rawStClearIrqs();
  rawStWriteReg(ST25R3916_REG_WUP_TIMER_CONTROL, 0x00U);
  rawStWriteReg(ST25R3916_REG_IO_CONF1, 0x07U);
  rawStWriteReg(ST25R3916_REG_MODE, ST25R3916_REG_MODE_om_iso14443a);
  rawStWriteReg(ST25R3916_REG_OP_CONTROL, ST25R3916_REG_OP_CONTROL_en);
  delay(5);
}

void rawStConfigureWakeUp(rfalWumPeriod period, bool phase)
{
  rawStReady();

  const uint8_t ampRef = rawStMeasure(ST25R3916_CMD_MEASURE_AMPLITUDE);
  const uint8_t phaRef = phase ? rawStMeasure(ST25R3916_CMD_MEASURE_PHASE) : 0U;

  const uint8_t measureConf =
    (uint8_t)((2U << ST25R3916_REG_AMPLITUDE_MEASURE_CONF_am_d_shift) |
              (RFAL_WUM_AA_WEIGHT_16 << ST25R3916_REG_AMPLITUDE_MEASURE_CONF_am_aew_shift));

  rawStWriteReg(ST25R3916_REG_AMPLITUDE_MEASURE_CONF, measureConf);
  rawStWriteReg(ST25R3916_REG_AMPLITUDE_MEASURE_REF, ampRef);

  if (phase) {
    rawStWriteReg(ST25R3916_REG_PHASE_MEASURE_CONF, measureConf);
    rawStWriteReg(ST25R3916_REG_PHASE_MEASURE_REF, phaRef);
  }

  uint8_t wup =
    (uint8_t)((((uint8_t)period & 0x0FU) << ST25R3916_REG_WUP_TIMER_CONTROL_wut_shift) |
              ST25R3916_REG_WUP_TIMER_CONTROL_wam);
  if ((uint8_t)period < (uint8_t)RFAL_WUM_PERIOD_100MS) {
    wup |= ST25R3916_REG_WUP_TIMER_CONTROL_wur;
  }
  if (phase) {
    wup |= ST25R3916_REG_WUP_TIMER_CONTROL_wph;
  }

  rawStWriteReg(ST25R3916_REG_IRQ_MASK_MAIN, 0xFFU);
  rawStWriteReg(ST25R3916_REG_IRQ_MASK_TIMER_NFC, 0xFFU);
  rawStWriteReg(ST25R3916_REG_IRQ_MASK_ERROR_WUP, phase ? 0xF9U : 0xFBU);
  rawStWriteReg(ST25R3916_REG_IRQ_MASK_TARGET, 0xFFU);
  rawStClearIrqs();

  rawStWriteReg(ST25R3916_REG_WUP_TIMER_CONTROL, wup);
  rawStWriteReg(ST25R3916_REG_OP_CONTROL, ST25R3916_REG_OP_CONTROL_wu);
}

void rawStFieldOn(bool highResistanceDriver)
{
  rawStReady();
  if (highResistanceDriver) {
    rawStChangeRegBits(ST25R3916_REG_TX_DRIVER, ST25R3916_REG_TX_DRIVER_d_res_mask, 0x0FU);
  }
  rawStCommand(ST25R3916_CMD_ADJUST_REGULATORS);
  delay(5);
  rawStWriteReg(ST25R3916_REG_OP_CONTROL,
                (ST25R3916_REG_OP_CONTROL_en |
                 ST25R3916_REG_OP_CONTROL_rx_en |
                 ST25R3916_REG_OP_CONTROL_tx_en));
}

void writeReg(uint8_t reg, uint8_t value)
{
  const uint8_t tmp = value;
  gReader.rfalChipWriteReg(reg, &tmp, 1U);
}

uint8_t readReg(uint8_t reg)
{
  uint8_t value = 0U;
  gReader.rfalChipReadReg(reg, &value, 1U);
  return value;
}

bool ensureStInitialized()
{
  if (gStInitialized) {
    return true;
  }

  gSpi.begin(kPinSck, kPinMiso, kPinMosi, kPinSs);

  const ReturnCode err = gNfc.rfalNfcInitialize();
  if (err != ERR_NONE) {
    Serial.print("rfalNfcInitialize failed: ");
    Serial.println((int)err);
    Serial.println("Check ST25R3916B power, SPI wiring, and IRQ wiring.");
    return false;
  }

  gStInitialized = true;
  return true;
}

void stopPollingIfNeeded()
{
  if (!gStInitialized) {
    return;
  }

  if (gPollingStarted) {
    gNfc.rfalNfcDeactivate(false);
    gPollingStarted = false;
  }
}

void forceFieldOff()
{
  if (!gStInitialized) {
    return;
  }

  gReader.rfalFieldOff();
  gReader.rfalWakeUpModeStop();
  writeReg(ST25R3916_REG_WUP_TIMER_CONTROL, 0x00U);
}

void putStPowerDownIfInitialized()
{
  rawStPowerDown();
  gPollingStarted = false;
  gStInitialized = false;
}

void printModeHold(const char *name)
{
  Serial.print("MODE: ");
  Serial.println(name);
  Serial.println("Hold this mode and record current.");
}

rfalWakeUpConfig makeWakeupConfig(rfalWumPeriod period, bool phase)
{
  rfalWakeUpConfig cfg = {};
  cfg.period = period;
  cfg.irqTout = false;
  cfg.swTagDetect = false;

  cfg.indAmp.enabled = true;
  cfg.indAmp.delta = 2U;
  cfg.indAmp.fracDelta = 0U;
  cfg.indAmp.reference = RFAL_WUM_REFERENCE_AUTO;
  cfg.indAmp.autoAvg = false;
  cfg.indAmp.aaInclMeas = false;
  cfg.indAmp.aaWeight = RFAL_WUM_AA_WEIGHT_16;

  cfg.indPha.enabled = phase;
  cfg.indPha.delta = 2U;
  cfg.indPha.fracDelta = 0U;
  cfg.indPha.reference = RFAL_WUM_REFERENCE_AUTO;
  cfg.indPha.autoAvg = false;
  cfg.indPha.aaInclMeas = false;
  cfg.indPha.aaWeight = RFAL_WUM_AA_WEIGHT_16;

  cfg.cap.enabled = false;
  return cfg;
}

void enterEspIdleBaseline()
{
  putStPowerDownIfInitialized();
  setNoBackfeedSpiPins();
  digitalWrite(kPinLed, LOW);
  gMode = TestMode::EspIdleBaseline;
  printModeHold("ESP idle baseline. For true ESP-only current, reset with ST module VCC disconnected.");
}

void enterEspBusyBaseline()
{
  putStPowerDownIfInitialized();
  setNoBackfeedSpiPins();
  digitalWrite(kPinLed, HIGH);
  gMode = TestMode::EspBusyBaseline;
  printModeHold("ESP busy baseline. For true ESP-only current, reset with ST module VCC disconnected.");
}

void enterStPowerDown()
{
  rawStPowerDown();
  gPollingStarted = false;
  gStInitialized = false;
  setQuietSpiPins();

  gMode = TestMode::StPowerDown;
  printModeHold("ST power-down, ESP awake idle");
}

void enterStReady()
{
  rawStReady();
  gPollingStarted = false;
  gStInitialized = false;

  gMode = TestMode::StReady;
  printModeHold("ST ready mode, oscillator/regulators on, RF field off");
}

void enterStWakeUp(rfalWumPeriod period, bool phase)
{
  rawStConfigureWakeUp(period, phase);
  gPollingStarted = false;
  gStInitialized = false;

  gLastWakeReportMs = 0;
  gMode = TestMode::StWakeUp;

  Serial.print("MODE: ST hardware wake-up, period code 0x");
  Serial.print((uint8_t)period, HEX);
  Serial.print(", amplitude=on, phase=");
  Serial.println(phase ? "on" : "off");
  Serial.println("No card: record idle wake-up current. Then approach card/phone and record triggered behavior.");
}

void enterStFieldOn(bool highResistanceDriver)
{
  rawStFieldOn(highResistanceDriver);
  gPollingStarted = false;
  gStInitialized = false;

  gMode = TestMode::StFieldOn;
  printModeHold(highResistanceDriver ? "ST RF field on, high driver resistance" : "ST RF field on, default driver");
}

const char *deviceTypeToString(rfalNfcDevType type)
{
  switch (type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
    case RFAL_NFC_POLL_TYPE_NFCA:
      return "ISO14443A";
    case RFAL_NFC_LISTEN_TYPE_NFCB:
    case RFAL_NFC_POLL_TYPE_NFCB:
      return "ISO14443B";
    case RFAL_NFC_LISTEN_TYPE_NFCV:
    case RFAL_NFC_POLL_TYPE_NFCV:
      return "ISO15693";
    default:
      return "UNKNOWN";
  }
}

void printId(const uint8_t *id, uint8_t idLen)
{
  for (uint8_t i = 0; i < idLen; i++) {
    if (id[i] < 0x10U) {
      Serial.print('0');
    }
    Serial.print(id[i], HEX);
    if (i + 1U < idLen) {
      Serial.print(' ');
    }
  }
}

void onNfcStateChange(rfalNfcState state)
{
  if (state != RFAL_NFC_STATE_ACTIVATED) {
    return;
  }

  rfalNfcDevice *device = NULL;
  if (gNfc.rfalNfcGetActiveDevice(&device) != ERR_NONE || (device == NULL)) {
    return;
  }

  Serial.print("Card: ");
  Serial.print(deviceTypeToString(device->type));
  Serial.print(" ");
  printId(device->nfcid, device->nfcidLen);
  Serial.println();

  gNfc.rfalNfcDeactivate(true);
}

void enterStPolling()
{
  Serial.println("N is disabled in this build because Arduino hardware SPI transfer hangs on this ESP32-S3 setup.");
  Serial.println("Use F/H for RF-field current tests, and use 8/4/2/1/A for autonomous wake-up current tests.");
}

void printStatus()
{
  Serial.print("OP_CONTROL=0x");
  Serial.print(gStInitialized ? readReg(ST25R3916_REG_OP_CONTROL) : rawStReadReg(ST25R3916_REG_OP_CONTROL), HEX);
  Serial.print(" IO_CONF1=0x");
  Serial.print(gStInitialized ? readReg(ST25R3916_REG_IO_CONF1) : rawStReadReg(ST25R3916_REG_IO_CONF1), HEX);
  Serial.print(" IO_CONF2=0x");
  Serial.print(gStInitialized ? readReg(ST25R3916_REG_IO_CONF2) : rawStReadReg(ST25R3916_REG_IO_CONF2), HEX);
  Serial.print(" WUP_TIMER=0x");
  Serial.print(gStInitialized ? readReg(ST25R3916_REG_WUP_TIMER_CONTROL) : rawStReadReg(ST25R3916_REG_WUP_TIMER_CONTROL), HEX);
  Serial.print(" IRQ=");
  Serial.println(digitalRead(kPinIrq));
}

void printMenu()
{
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  0  ESP idle baseline. For ESP-only current, reset with ST module VCC disconnected.");
  Serial.println("  9  ESP busy baseline. For ESP-only current, reset with ST module VCC disconnected.");
  Serial.println("  P  ST power-down, ESP awake idle.");
  Serial.println("  R  ST ready, oscillator/regulators on, RF field off.");
  Serial.println("  8  ST hardware wake-up amplitude, 800 ms.");
  Serial.println("  4  ST hardware wake-up amplitude, 400 ms.");
  Serial.println("  2  ST hardware wake-up amplitude, 200 ms.");
  Serial.println("  1  ST hardware wake-up amplitude, 100 ms.");
  Serial.println("  A  ST hardware wake-up amplitude + phase, 800 ms.");
  Serial.println("  F  ST RF field on, default driver.");
  Serial.println("  H  ST RF field on, high driver resistance.");
  Serial.println("  N  Disabled in this build; hardware SPI transfer hangs on this ESP32-S3 setup.");
  Serial.println("  S  Print key register/status values.");
  Serial.println("  ?  Print this menu.");
  Serial.println();
}

void handleCommand(char cmd)
{
  switch (cmd) {
    case '0':
      enterEspIdleBaseline();
      break;
    case '9':
      enterEspBusyBaseline();
      break;
    case 'P':
    case 'p':
      enterStPowerDown();
      break;
    case 'R':
    case 'r':
      enterStReady();
      break;
    case '8':
      enterStWakeUp(RFAL_WUM_PERIOD_800MS, false);
      break;
    case '4':
      enterStWakeUp(RFAL_WUM_PERIOD_400MS, false);
      break;
    case '2':
      enterStWakeUp(RFAL_WUM_PERIOD_200MS, false);
      break;
    case '1':
      enterStWakeUp(RFAL_WUM_PERIOD_100MS, false);
      break;
    case 'A':
    case 'a':
      enterStWakeUp(RFAL_WUM_PERIOD_800MS, true);
      break;
    case 'F':
    case 'f':
      enterStFieldOn(false);
      break;
    case 'H':
    case 'h':
      enterStFieldOn(true);
      break;
    case 'N':
    case 'n':
      enterStPolling();
      break;
    case 'S':
    case 's':
      printStatus();
      break;
    case '?':
      printMenu();
      break;
    case '\r':
    case '\n':
      break;
    default:
      Serial.println("Unknown command. Send ? for menu.");
      break;
  }
}

void runCurrentMode()
{
  switch (gMode) {
    case TestMode::EspBusyBaseline:
      for (uint32_t i = 0; i < 10000UL; i++) {
        gBusySink = (gBusySink * 1664525UL) + 1013904223UL + i;
      }
      break;

    case TestMode::StWakeUp:
      if ((digitalRead(kPinIrq) == HIGH) && ((millis() - gLastWakeReportMs) > 500UL)) {
        gLastWakeReportMs = millis();
        Serial.println("Wake-up IRQ pin asserted by ST25R3916B.");
      }
      delay(20);
      break;

    case TestMode::StPolling:
      gNfc.rfalNfcWorker();
      break;

    default:
      delay(50);
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
  setQuietSpiPins();

  Serial.println();
  Serial.println("ESP32 + ST25R3916B power profile test");
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
  printMenu();
  enterEspIdleBaseline();
}

void loop()
{
  while (Serial.available() > 0) {
    handleCommand((char)Serial.read());
  }

  runCurrentMode();
}
