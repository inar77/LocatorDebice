// SeatLocatorDevice.hpp

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

        // 1. Co określony interwał – wykonaj skan BLE
        if (now - lastScanMs_ >= cfg_.scanIntervalMs) {
            lastScanMs_ = now;
            handleScan();
        }

        // 2. Co jakiś czas wyczyść stare próbki z bufora RSSI
        cleanupRssiBuffer(now);

        // 3. Na podstawie uśrednionego RSSI aktualizuj LED/buzzer
        const auto avgRssi = computeAverageRssi(now);
        if (avgRssi.has_value()) {
            updateFeedback(*avgRssi);
        } else {
            // Jeśli nic nie widzimy – np. brak beacona / jeszcze nie zeskanowano
            led_.setColor(LedColor::Red);
            led_.setBlinkPeriodMs(800);  // wolne miganie
            buzzer_.setBeepPeriodMs(0);  // wyłączony
        }
    }

private:
    struct RssiSample {
        float    rssi_dbm;
        uint64_t timestamp_ms;
    };

    BleScanner&   scanner_;
    LedController& led_;
    Buzzer&       buzzer_;
    TimeProvider& time_;
    Config        cfg_;

    uint64_t lastScanMs_ {0};

    // Prosty bufor pierścieniowy na próbki RSSI
    static constexpr std::size_t kMaxSamples = 32;
    RssiSample rssiBuffer_[kMaxSamples];
    std::size_t rssiCount_ {0};
    std::size_t rssiHeadIndex_ {0};

    void handleScan() {
        auto resultOpt = scanner_.scanOnce();
        if (!resultOpt.has_value()) {
            return;
        }

        const auto& result = *resultOpt;
        if (result.id == cfg_.targetBeacon) {
            addRssiSample(result.rssi_dbm, result.timestamp_ms);
        }
    }

    void addRssiSample(float rssi_dbm, uint64_t ts_ms) {
        if (rssiCount_ < kMaxSamples) {
            const std::size_t idx = (rssiHeadIndex_ + rssiCount_) % kMaxSamples;
            rssiBuffer_[idx] = RssiSample{rssi_dbm, ts_ms};
            ++rssiCount_;
        } else {
            // nadpisujemy najstarszą próbkę
            rssiBuffer_[rssiHeadIndex_] = RssiSample{rssi_dbm, ts_ms};
            rssiHeadIndex_ = (rssiHeadIndex_ + 1) % kMaxSamples;
        }
    }

    void cleanupRssiBuffer(uint64_t now_ms) {
        while (rssiCount_ > 0) {
            const auto& oldest = rssiBuffer_[rssiHeadIndex_];
            if (now_ms - oldest.timestamp_ms > cfg_.rssiFilterWindowMs) {
                rssiHeadIndex_ = (rssiHeadIndex_ + 1) % kMaxSamples;
                --rssiCount_;
            } else {
                break;
            }
        }
    }

    std::optional<float> computeAverageRssi(uint64_t now_ms) const {
        if (rssiCount_ == 0) {
            return std::nullopt;
        }

        float sum = 0.0f;
        std::size_t used = 0;

        for (std::size_t i = 0; i < rssiCount_; ++i) {
            std::size_t idx = (rssiHeadIndex_ + i) % kMaxSamples;
            const auto& sample = rssiBuffer_[idx];
            // teoretycznie cleanup już usunął stare, ale dodatkowy warunek nic nie kosztuje
            if (now_ms - sample.timestamp_ms <= cfg_.rssiFilterWindowMs) {
                sum += sample.rssi_dbm;
                ++used;
            }
        }

        if (used == 0) {
            return std::nullopt;
        }
        return sum / static_cast<float>(used);
    }

    void updateFeedback(float rssi_dbm) {
        // 1. Przytnij RSSI do zdefiniowanego zakresu
        float rssi = rssi_dbm;
        if (rssi < cfg_.rssiMinDbm) rssi = cfg_.rssiMinDbm;
        if (rssi > cfg_.rssiMaxDbm) rssi = cfg_.rssiMaxDbm;

        // 2. Normalizacja do 0–1
        const float range = cfg_.rssiMaxDbm - cfg_.rssiMinDbm;
        float x = (range > 0.0f) ? (rssi - cfg_.rssiMinDbm) / range : 0.0f;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;

        // 3. Częstotliwość w Hz
        const float freqRange = cfg_.freqMaxHz - cfg_.freqMinHz;
        const float freqHz = cfg_.freqMinHz + x * freqRange;
        // Ochrona przed dzieleniem przez zero
        const float safeFreqHz = (freqHz <= 0.01f) ? cfg_.freqMinHz : freqHz;
        const auto periodMs = static_cast<uint32_t>(1000.0f / safeFreqHz);

        // 4. Kolor wg „stref”
        LedColor color = LedColor::Red;
        if (x < 0.33f) {
            color = LedColor::Red;
        } else if (x < 0.66f) {
            color = LedColor::Yellow;
        } else {
            color = LedColor::Green;
        }

        // 5. Zastosowanie na sprzęcie
        led_.setColor(color);
        led_.setBlinkPeriodMs(periodMs);

        // Buzzer opcjonalnie może mieć inną charakterystykę, np. tylko w dalszej strefie
        if (x < 0.5f) {
            // daleko – okresowo pikaj
            buzzer_.setBeepPeriodMs(periodMs * 2);
        } else {
            // blisko – cisza, żeby nie wkurzać ludzi
            buzzer_.setBeepPeriodMs(0);
        }
    }
};
