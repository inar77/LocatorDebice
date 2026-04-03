
#pragma once

#include <cstdint>
#include <optional>
#include <chrono>

// Prosty typ na identyfikację beacona przypisanego do miejsca
struct BeaconId {
    uint16_t major;
    uint16_t minor;

    bool operator==(const BeaconId& other) const noexcept {
        return major == other.major && minor == other.minor;
    }
};

// Wynik skanowania pojedynczego beacona
struct BeaconScanResult {
    BeaconId id;
    float    rssi_dbm;     // np. -85.0f
    uint64_t timestamp_ms; // czas od startu urządzenia
};

// Interfejsy sprzętowe (abstrakcja – w realnym projekcie będzie implementacja pod konkretny MCU)
class BleScanner {
public:
    virtual ~BleScanner() = default;
    virtual std::optional<BeaconScanResult> scanOnce() = 0;
};

enum class LedColor {
    Red,
    Yellow,
    Green,
    Off
};

class LedController {
public:
    virtual ~LedController() = default;
    virtual void setColor(LedColor color) = 0;
    virtual void setBlinkPeriodMs(uint32_t period_ms) = 0; // pełen okres (on + off)
};

class Buzzer {
public:
    virtual ~Buzzer() = default;
    virtual void setBeepPeriodMs(uint32_t period_ms) = 0;  // opcjonalnie: beep w rytm LED
};

class TimeProvider {
public:
    virtual ~TimeProvider() = default;
    virtual uint64_t nowMs() const = 0;
};

// Główna klasa logiki urządzenia
class SeatLocatorDevice {
public:
    struct Config {
        BeaconId targetBeacon;     // beacon przypisany do naszego miejsca
        float    rssiMinDbm = -90.0f;
        float    rssiMaxDbm = -40.0f;
        float    freqMinHz  = 1.0f;   // minimalna częstotliwość migania
        float    freqMaxHz  = 5.0f;   // maksymalna częstotliwość migania
        uint32_t scanIntervalMs = 300; // jak często skanujemy BLE
        uint32_t rssiFilterWindowMs = 1500; // okno do uśredniania
    };

    SeatLocatorDevice(BleScanner& scanner,
                      LedController& led,
                      Buzzer& buzzer,
                      TimeProvider& time,
                      Config cfg)
        : scanner_{scanner}
        , led_{led}
        , buzzer_{buzzer}
        , time_{time}
        , cfg_{cfg}
    {}

    // Wywoływane cyklicznie z głównej pętli (np. co 50–100 ms)
    void tick() {
        const auto now = time_.nowMs();
