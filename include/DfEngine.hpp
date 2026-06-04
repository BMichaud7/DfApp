/*
========================================================================
Project: OpenRFStack
Author:  Brendan Michaud
Year:    2026
Part of OpenRFStack (https://github.com/OpenRFStack)

Licensed under the Personal Use License.
Do not use for commercial, organizational, or military purposes.
Contact author for permission: https://github.com/OpenRFStack
========================================================================
*/
/**
 * @file DfEngine.hpp
 * @brief MUSIC direction-finding engine for multi-element antenna arrays.
 *
 * Implements the MUSIC (MUltiple SIgnal Classification) algorithm for
 * direction-of-arrival estimation. Requires ≥2 time-coherent receivers
 * sharing a common LO reference — phase differences between antennas are
 * then purely geometric, making sequential snapshots directly usable.
 */
#pragma once
#include "Config.hpp"
#include <au/units/hertz.hh>
#include <complex>
#include <string>
#include <vector>

namespace df {

/// @brief Result produced by DfEngine::compute().
struct DfResult {
    au::QuantityD<au::Hertz> center_freq_hz{au::hertz(0.0)}; ///< Signal carrier frequency.
    double      azimuth_deg{0.0};    ///< Bearing clockwise from North, 0–360°.
    float       confidence{0.0f};    ///< MUSIC peak-to-mean prominence [0, 1].
    int         num_elements{0};     ///< Number of antenna elements that contributed.
    std::string algorithm;           ///< Algorithm identifier (e.g. "MUSIC").
    bool        valid{false};        ///< False if insufficient elements or SNR.
};

/**
 * @class DfEngine
 * @brief MUSIC direction-of-arrival estimator for a planar antenna array.
 *
 * Algorithm:
 *  1. Build the M×N sample matrix X from IQ snapshots across M antennas.
 *  2. Form the spatial covariance R = X Xᴴ / N.
 *  3. Eigendecompose R → signal subspace Eₛ and noise subspace Eₙ.
 *  4. Sweep azimuth θ at angle_step_deg resolution, evaluating
 *     P(θ) = 1 / ‖ Eₙᴴ a(θ) ‖² where a(θ) is the steering vector.
 *  5. Return the azimuth of the global peak.
 *
 * Phase coherence requirement: all receivers must share a common oscillator
 * reference (GPS-disciplined, hardwired, or using a reference signal).
 */
class DfEngine {
public:
    /**
     * @brief Construct the engine with a given angular sweep resolution.
     * @param angle_step_deg  Azimuth step size in degrees (default 0.5°).
     *                        Smaller values increase accuracy at the cost of CPU.
     */
    explicit DfEngine(double angle_step_deg = 0.5);

    /**
     * @brief Run MUSIC bearing estimation.
     *
     * @param iq_snapshots  Per-antenna IQ data. iq_snapshots[i] contains
     *                      interleaved float32 I, Q pairs from antenna i.
     *                      All snapshots must have the same length.
     * @param antennas      Antenna element positions in the ENU frame (metres).
     *                      Must match the length of iq_snapshots.
     * @param freq          Carrier frequency of the signal being localised.
     * @return DfResult     Bearing estimate. result.valid is false if the
     *                      computation could not be performed (e.g. too few
     *                      elements, degenerate covariance matrix).
     */
    DfResult compute(
        const std::vector<std::vector<float>>& iq_snapshots,
        const std::vector<AntennaElement>&     antennas,
        au::QuantityD<au::Hertz> freq) const;

private:
    double angle_step_deg_; ///< Angular sweep resolution in degrees.
};

} // namespace df

/*
========================================================================
End of file — OpenRFStack
Subject to Personal Use License
https://github.com/OpenRFStack
========================================================================
*/
