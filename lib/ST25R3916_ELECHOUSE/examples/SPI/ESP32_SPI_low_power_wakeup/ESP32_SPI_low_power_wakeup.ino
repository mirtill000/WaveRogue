/*
  Example: ESP32_SPI_low_power_wakeup
  Bus: SPI
  Default wiring: ESP32-S3 SCK=12/MISO=13/MOSI=11/SS=10/IRQ=4

  Goal:
    Configure ST25R3916/ST25R3916B hardware Wake-up mode, then put ESP32-S3
    into deep sleep. A card/phone approaching the antenna changes the antenna
    amplitude enough for the reader IC to assert IRQ and wake the ESP32-S3.

  Test notes:
    - Keep the ST25R3916 module powered while the ESP32-S3 sleeps.
    - Connect IRQ to an ESP32-S3 RTC-capable GPIO. GPIO4 is RTC-capable.
    - Place the card/phone near the antenna only after "Entering deep sleep".
    - Remove the card before the sketch re-arms Wake-up mode, otherwise the new
      reference can be learned with the card already present.
*/

#include <Arduino.h>
#include <SPI.h>

#include <esp_sleep.h>
#include <driver/gpio.h>

#include <rfal_rfst25r3916.h>
#include <st25r3916_config.h>
#include <st_errno.h>

namespace {

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
constexpr uint8_t kSpiBus = FSPI;
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

// Longer periods reduce average current. Use 400 ms or 800 ms for battery tests.
constexpr rfalWumPeriod kWakeupPeriod = RFAL_WUM_PERIOD_800MS;

// Start with amplitude only. Increase if false wakeups occur; decrease if range is too short.
constexpr uint8_t kAmplitudeDelta = 2U;

// Enable phase only if amplitude alone is not reliable enough on the target enclosure.
constexpr bool kEnablePhaseWakeup = false;
constexpr uint8_t kPhaseDelta = 2U;

// Active time after reset/wake so the serial log is visible before re-entering sleep.
constexpr uint32_t kPostWakeLogWindowMs = 3000UL;

SPIClass gSpi(kSpiBus);
RfalRfST25R3916Class gReader(&gSpi, kPinSs, kPinIrq);

void waitForSerial()
{
  const unsigned long start = millis();
  while (!Serial && ((millis() - start) < 2000UL)) {
    delay(10);
  }
}

const char *wakeupCauseToString(esp_sleep_wakeup_cause_t cause)
{
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0 IRQ";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    default:
      return "power-on/reset";
  }
}

void releaseSleepHolds()
{
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)kPinSs);
  gpio_hold_dis((gpio_num_t)kPinSck);
  gpio_hold_dis((gpio_num_t)kPinMosi);
  gpio_hold_dis((gpio_num_t)kPinLed);
}

void setSpiPinsForSleep()
{
  pinMode(kPinSs, OUTPUT);
  digitalWrite(kPinSs, HIGH);

  pinMode(kPinSck, OUTPUT);
  digitalWrite(kPinSck, LOW);

  pinMode(kPinMosi, OUTPUT);
  digitalWrite(kPinMosi, LOW);

  pinMode(kPinMiso, INPUT);
  pinMode(kPinIrq, INPUT);

  gpio_hold_en((gpio_num_t)kPinSs);
  gpio_hold_en((gpio_num_t)kPinSck);
  gpio_hold_en((gpio_num_t)kPinMosi);
  gpio_hold_en((gpio_num_t)kPinLed);
  gpio_deep_sleep_hold_en();
}

rfalWakeUpConfig makeWakeupConfig()
{
  rfalWakeUpConfig cfg = {};
  cfg.period = kWakeupPeriod;
  cfg.irqTout = false;
  cfg.swTagDetect = false; // Hardware autonomous wake-up; do not wake MCU on every timeout.

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

  cfg.cap.enabled = false; // ST25R3916B does not support capacitive wake-up.
  return cfg;
}

void printWakeupConfig(const rfalWakeUpConfig &cfg)
{
  Serial.print("Wake-up period code: 0x");
  Serial.println((uint8_t)cfg.period, HEX);
  Serial.print("Amplitude wake-up: ");
  Serial.print(cfg.indAmp.enabled ? "on" : "off");
  Serial.print(", delta=");
  Serial.println(cfg.indAmp.delta);
  Serial.print("Phase wake-up: ");
  Serial.print(cfg.indPha.enabled ? "on" : "off");
  Serial.print(", delta=");
  Serial.println(cfg.indPha.delta);
}

void enterDeepSleep()
{
  Serial.println("Entering deep sleep. Approach a card/phone to wake ESP32-S3.");
  Serial.flush();
  delay(20);

  digitalWrite(kPinLed, LOW);
  setSpiPinsForSleep();

  Serial.end();
  esp_deep_sleep_start();
}

} // namespace

void setup()
{
  releaseSleepHolds();

  pinMode(kPinLed, OUTPUT);
  digitalWrite(kPinLed, HIGH);
  pinMode(kPinSs, OUTPUT);
  digitalWrite(kPinSs, HIGH);
  pinMode(kPinIrq, INPUT);

  Serial.begin(115200);
  waitForSerial();

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  Serial.println();
  Serial.println("ESP32-S3 + ST25R3916B low-power wake-up test");
  Serial.print("Wake cause: ");
  Serial.println(wakeupCauseToString(cause));
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

  gSpi.begin(kPinSck, kPinMiso, kPinMosi, kPinSs);

  ReturnCode err = gReader.rfalInitialize();
  if (err != ERR_NONE) {
    Serial.print("rfalInitialize failed: ");
    Serial.println((int)err);
    while (true) {
      digitalWrite(kPinLed, !digitalRead(kPinLed));
      delay(250);
    }
  }

  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("ESP32-S3 was woken by ST25R3916B IRQ.");
  }

  delay(kPostWakeLogWindowMs);

  rfalWakeUpConfig cfg = makeWakeupConfig();
  printWakeupConfig(cfg);

  err = gReader.rfalWakeUpModeStart(&cfg);
  if (err != ERR_NONE) {
    Serial.print("rfalWakeUpModeStart failed: ");
    Serial.println((int)err);
    while (true) {
      digitalWrite(kPinLed, !digitalRead(kPinLed));
      delay(250);
    }
  }

  if (digitalRead(kPinIrq) == HIGH) {
    Serial.println("IRQ is already high. Remove nearby card/phone before measuring sleep current.");
    const unsigned long start = millis();
    while ((digitalRead(kPinIrq) == HIGH) && ((millis() - start) < 3000UL)) {
      gReader.rfalWorker();
      delay(10);
    }
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const esp_err_t sleepErr = esp_sleep_enable_ext0_wakeup((gpio_num_t)kPinIrq, 1);
  if (sleepErr != ESP_OK) {
    Serial.print("esp_sleep_enable_ext0_wakeup failed: ");
    Serial.println((int)sleepErr);
    Serial.println("Use an ESP32-S3 RTC-capable GPIO for IRQ.");
    while (true) {
      digitalWrite(kPinLed, !digitalRead(kPinLed));
      delay(250);
    }
  }

  enterDeepSleep();
}

void loop()
{
  delay(1000);
}
