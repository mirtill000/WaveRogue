#include "gps_logger.h"
#include "config.h"
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

namespace {
    HardwareSerial gpsSerial(GPS_UART_NUM);
    TinyGPSPlus gps;
}

void GpsLogger::begin() {
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

void GpsLogger::update() {
    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }
}

bool GpsLogger::hasFix() {
    return gps.location.isValid();
}

double GpsLogger::latitude() {
    return gps.location.isValid() ? gps.location.lat() : 0.0;
}

double GpsLogger::longitude() {
    return gps.location.isValid() ? gps.location.lng() : 0.0;
}

String GpsLogger::timeString() {
    if (!gps.time.isValid()) return "00:00:00";
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    return String(buf);
}
