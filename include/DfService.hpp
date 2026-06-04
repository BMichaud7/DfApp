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
#pragma once
#include "Config.hpp"
#include "DfEngine.hpp"
#include <au/units/hertz.hh>
#include <au/units/seconds.hh>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef DF_WITH_DB
#include <pqxx/pqxx>
#endif

namespace proton { class container; }

namespace df {

class ServiceAmqpHandler;

struct SnapshotEntry {
    std::string              scanner_id;
    std::vector<float>       iq_snapshot;      // interleaved I,Q float32
    au::QuantityD<au::Hertz> center_freq_hz{au::hertz(0.0)};
    double                   snr_db{0};
    au::QuantityD<au::Seconds> timestamp_ms{au::seconds(0.0)};
};

// Aggregates RF_DETECTION snapshots from multiple AcquisitionApp instances
// (one per PlutoSDR). When ≥ min_elements antennas report the same signal
// within the aggregation window, runs MUSIC and publishes the bearing.
class DfService {
public:
    explicit DfService(const AppConfig& cfg);
    ~DfService();

    void start();
    void stop();

private:
    AppConfig cfg_;
    DfEngine  engine_;

    std::atomic<bool> running_{false};

    std::shared_ptr<ServiceAmqpHandler> amqp_handler_;
    std::shared_ptr<proton::container>  amqp_container_;
    std::thread sub_thread_;
    std::thread sweep_thread_;
    std::condition_variable sweep_cv_;

    // O(1) lookup from scanner_id to its AntennaElement — built once in constructor.
    std::unordered_map<std::string, AntennaElement> antenna_map_;

    // Pending snapshots keyed by 100 kHz frequency bucket.
    // Inner map: scanner_id → latest snapshot (dedup per scanner).
    std::mutex agg_mu_;
    std::unordered_map<int64_t,
        std::unordered_map<std::string, SnapshotEntry>> pending_;

    void subscriptionLoop();
    void sweepLoop();

    void onDetection(SnapshotEntry e);
    void tryCompute(int64_t bucket);

    void publishResult(const DfResult& r,
                       const std::vector<AntennaElement>& used_antennas,
                       const std::vector<SnapshotEntry>&  used_snapshots);

#ifdef DF_WITH_DB
    std::unique_ptr<pqxx::connection> db_conn_;
    std::string db_conn_str_;
    void persistResult(const DfResult& r,
                       const std::vector<SnapshotEntry>& used_snapshots);
#endif
};

} // namespace df

/*
========================================================================
End of file — OpenRFStack
Subject to Personal Use License
https://github.com/OpenRFStack
========================================================================
*/
