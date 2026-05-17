#include "TrainMap.hpp"
#include <cmath>
#include <sstream>
#include <algorithm>

float TrainMap::wagonLength(WagonType t) const {
    switch (t) {
        case WagonType::PENDOLINO_1CL:
        case WagonType::PENDOLINO_2CL: return Config::WAGON_LENGTH_PENDOLINO;
        case WagonType::OPEN_1CL:
        case WagonType::OPEN_2CL:      return Config::WAGON_LENGTH_OPEN;
        default:                        return Config::WAGON_LENGTH_CORRIDOR;
    }
}

void TrainMap::loadICTrain(const std::string& trainId,
                            std::vector<WagonDescriptor> wagons) {
    m_trainId     = trainId;
    m_wagons      = std::move(wagons);
    m_totalLength = 0.0f;
    for (auto& w : m_wagons) {
        w.offsetFromHead = m_totalLength;
        m_totalLength   += wagonLength(w.type);
    }
}

// Numeracja PKP: seatNr dziesiątki = nr przedziału, jedności = pozycja
// jedności 5,6 → okno; 1,2 → korytarz; 3,4,7,8 → środek (8-miejsc.)
SeatPosition TrainMap::resolveCorridorSeat(const WagonDescriptor& w,
                                            int seatNr) const {
    SeatPosition pos;
    pos.valid = true;

    const int seatsPerComp  = (w.type == WagonType::CORRIDOR_1CL) ? 6 : 8;
    const int compCount     = w.seatCount / seatsPerComp;

    int compartment  = seatNr / 10;
    int seatInComp   = seatNr % 10;

    if (compartment < 1 || compartment > compCount) {
        compartment = std::max(1, std::min((seatNr-1)/seatsPerComp + 1, compCount));
        seatInComp  = ((seatNr-1) % seatsPerComp) + 1;
    }

    const float segLen  = wagonLength(w.type) / static_cast<float>(compCount);
    const float relPos  = (compartment - 0.5f) * segLen / wagonLength(w.type);

    pos.relativePos  = relPos;
    pos.absolutePos  = w.offsetFromHead + relPos * wagonLength(w.type);
    pos.compartment  = compartment;
    pos.rowInWagon   = compartment;
    pos.windowSide   = (seatsPerComp == 8)
                       ? (seatInComp >= 5)
                       : (seatInComp >= 4);

    std::ostringstream ss;
    ss << "Przedział " << compartment
       << (pos.windowSide ? " | okno" : " | korytarz");
    pos.description = ss.str();
    return pos;
}

// Wagon bezprzedziałowy 2+2: parzyste → okno
SeatPosition TrainMap::resolveOpenSeat(const WagonDescriptor& w,
                                        int seatNr) const {
    SeatPosition pos;
    pos.valid = true;

    const int cols     = (w.type == WagonType::OPEN_1CL) ? 3 : 4;
    const int rowCount = w.seatCount / cols;
    const int row      = (seatNr - 1) / cols;
    const int col      = (seatNr - 1) % cols;

    const float usable  = wagonLength(w.type) * 0.85f;
    const float spacing = usable / std::max(1, rowCount);
    const float startY  = wagonLength(w.type) * 0.075f;

    pos.relativePos = (startY + row * spacing) / wagonLength(w.type);
    pos.absolutePos = w.offsetFromHead + pos.relativePos * wagonLength(w.type);
    pos.rowInWagon  = row + 1;
    pos.windowSide  = (col == 0 || col == cols - 1);

    std::ostringstream ss;
    ss << "Rząd " << (row+1) << "/" << rowCount
       << (pos.windowSide ? " | okno" : " | środek");
    pos.description = ss.str();
    return pos;
}

SeatPosition TrainMap::resolveSeat(const Ticket& ticket) const {
    for (const auto& w : m_wagons) {
        if (w.wagonNr != ticket.wagonNr) continue;
        switch (w.type) {
            case WagonType::CORRIDOR_1CL:
            case WagonType::CORRIDOR_2CL:
                return resolveCorridorSeat(w, ticket.seatNr);
            default:
                return resolveOpenSeat(w, ticket.seatNr);
        }
    }
    SeatPosition p; p.valid = false; return p;
}
