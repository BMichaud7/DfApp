#pragma once
#include "Config.hpp"
#include <au/units/hertz.hh>
#include <complex>
#include <string>
#include <vector>

namespace df {

struct DfResult {
    au::QuantityD<au::Hertz> center_freq_hz{au::hertz(0.0)};
    double      azimuth_deg{0.0};    // bearing clockwise from North, 0–360
    float       confidence{0.0f};    // 0–1: MUSIC peak-to-mean prominence
    int         num_elements{0};     // antennas that contributed
    std::string algorithm;
    bool        valid{false};
};

// Implements MUSIC (Multiple Signal Classification) for direction-of-arrival
// estimation using a 2-D horizontal antenna array.
//
// All PlutoSDRs share a common LO reference so IQ snapshots from different
// units are phase-coherent: the carrier phase difference between antenna i and
// antenna j is solely due to the signal's propagation delay, making sequential
// snapshots of a continuous signal directly usable for interferometric DF.
class DfEngine {
public:
    explicit DfEngine(double angle_step_deg = 0.5);

    // iq_snapshots[i] — interleaved float32 I,Q from antenna i
    // antennas[i]     — position of antenna i (metres, ENU frame)
    // freq            — carrier frequency of the signal being localised
    DfResult compute(
        const std::vector<std::vector<float>>& iq_snapshots,
        const std::vector<AntennaElement>&     antennas,
        au::QuantityD<au::Hertz> freq) const;

private:
    double angle_step_deg_;
};

} // namespace df
