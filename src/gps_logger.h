// =============================================================================
// WaveRogue - gps_logger.h
//
// Thin wrapper around TinyGPS++ + a hardware UART, used by Module 2
// (LoRaWAN wardriving) to timestamp/geotag intercepted packets.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace GpsLogger {
    void begin();
    // Feed any pending NMEA bytes into the parser. Call every loop tick.
    void update();

    bool hasFix();
    double latitude();
    double longitude();
    // Satellites currently used in the fix computation (0 if unknown/no
    // data yet) - handy to show progress while acquiring a fix, not just
    // a flat "no fix" message.
    int satellites();
    // ISO-ish "HH:MM:SS" UTC time string from the last valid GPS fix, or
    // "00:00:00" if no fix yet (falls back to millis()-based logging).
    String timeString();
}
