#include "DfService.hpp"

#include <sdr/Base64.hpp>
#include <proton/container.hpp>
#include <proton/message.hpp>
#include <proton/messaging_handler.hpp>
#include <proton/connection.hpp>
#include <proton/connection_options.hpp>
#include <proton/reconnect_options.hpp>
#include <proton/sender.hpp>
#include <proton/receiver.hpp>
#include <proton/delivery.hpp>
#include <proton/work_queue.hpp>
#include <proton/transport.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <algorithm>

namespace df {

using json = nlohmann::json;
using clock = std::chrono::steady_clock;

// ── AMQP handler ─────────────────────────────────────────────────────────────

class ServiceAmqpHandler : public proton::messaging_handler {
public:
    ServiceAmqpHandler(const AmqpConfig& cfg,
                       std::function<void(SnapshotEntry)> on_det)
        : cfg_(cfg), on_detection_(std::move(on_det)) {}

    void on_container_start(proton::container& c) override {
        proton::connection_options opts;
        if (!cfg_.username.empty()) {
            opts.sasl_allowed_mechs("PLAIN");
            opts.sasl_allow_insecure_mechs(true);
            opts.user(cfg_.username).password(cfg_.password);
        } else {
            opts.sasl_allowed_mechs("ANONYMOUS");
        }
        proton::reconnect_options ropts;
        ropts.delay(proton::duration(2000));
        ropts.max_delay(proton::duration(30000));
        ropts.max_attempts(0);
        opts.reconnect(ropts);
        c.connect(cfg_.url, opts);
    }

    void on_connection_open(proton::connection& c) override {
        c.open_receiver(cfg_.detections_topic);
        pub_sender_ = c.open_sender(cfg_.df_results_topic);
        spdlog::info("DfService: connected (sub={} pub={})",
                     cfg_.detections_topic, cfg_.df_results_topic);
    }

    void on_sender_open(proton::sender& s) override {
        pub_sender_ = s;
        work_queue_ = &s.work_queue();
        std::lock_guard<std::mutex> lk(ready_mu_);
        pub_ready_ = true;
        ready_cv_.notify_all();
    }

    void on_message(proton::delivery& d, proton::message& m) override {
        d.accept();
        try {
            auto j = json::parse(proton::get<std::string>(m.body()));
            if (j.value("msg_type", "") != "RF_DETECTION") return;

            SnapshotEntry e;
            e.scanner_id     = j.value("scanner_id",     "unknown");
            e.center_freq_hz = j.value("center_freq_hz", 0.0);
            // Default to a high value when absent so old senders pass the SNR filter.
            e.snr_db         = j.value("snr_db",         99.0);
            e.timestamp_ms   = j.value("timestamp_ms",   (int64_t)0);

            // Decode IQ snapshot: base64 (schema >= 1.2) or JSON float array (< 1.2).
            if (j.contains("iq_snapshot_b64") && j["iq_snapshot_b64"].is_string()) {
                e.iq_snapshot = sdr::base64::decodeFloats(
                    j["iq_snapshot_b64"].get<std::string>());
            } else if (j.contains("iq_snapshot") && j["iq_snapshot"].is_array()) {
                e.iq_snapshot = j["iq_snapshot"].get<std::vector<float>>();
            } else {
                return;  // no IQ data — nothing to DF
            }

            on_detection_(std::move(e));
        } catch (const std::exception& ex) {
            spdlog::warn("DfService: parse error: {}", ex.what());
        }
    }

    void publish(const std::string& body) {
        std::unique_lock<std::mutex> lk(ready_mu_);
        if (!ready_cv_.wait_for(lk, std::chrono::seconds(5),
                                [this]{ return pub_ready_; })) return;
        if (!work_queue_) return;
        work_queue_->add([this, body]{
            if (pub_sender_ && pub_sender_.credit() > 0) {
                proton::message msg;
                msg.body(body);
                msg.content_type("application/json");
                msg.durable(false);
                pub_sender_.send(msg);
            }
        });
    }

    void close() {
        if (work_queue_)
            work_queue_->add([this]{ pub_sender_.connection().close(); });
    }

    void on_transport_error(proton::transport& t) override {
        spdlog::warn("DfService: transport error: {}", t.error().what());
    }
    void on_connection_error(proton::connection& c) override {
        spdlog::warn("DfService: connection error: {}", c.error().what());
    }

private:
    AmqpConfig cfg_;
    std::function<void(SnapshotEntry)> on_detection_;
    proton::sender      pub_sender_;
    proton::work_queue* work_queue_{nullptr};
    std::mutex          ready_mu_;
    std::condition_variable ready_cv_;
    bool                pub_ready_{false};
};

// ── DfService ─────────────────────────────────────────────────────────────────

DfService::DfService(const AppConfig& cfg)
    : cfg_(cfg)
    , engine_(cfg.df.angle_step_deg)
{
    for (const auto& a : cfg_.antennas)
        antenna_map_.emplace(a.scanner_id, a);
#ifdef DF_WITH_DB
    if (cfg.db.enabled) {
        db_conn_str_ = "host="     + cfg.db.host
                     + " port="    + std::to_string(cfg.db.port)
                     + " dbname="  + cfg.db.dbname
                     + " user="    + cfg.db.user
                     + " password=" + cfg.db.password;
        try {
            db_conn_ = std::make_unique<pqxx::connection>(db_conn_str_);
            spdlog::info("DfService: DB connected {}:{}/{}", cfg.db.host,
                         cfg.db.port, cfg.db.dbname);
        } catch (const std::exception& ex) {
            spdlog::warn("DfService: DB connection failed: {}", ex.what());
        }
    }
#endif
}

DfService::~DfService() { stop(); }

void DfService::start()
{
    if (running_.exchange(true)) return;
    sweep_thread_  = std::thread(&DfService::sweepLoop,        this);
    sub_thread_    = std::thread(&DfService::subscriptionLoop, this);
    spdlog::info("DfService: started ({} antennas configured)", cfg_.antennas.size());
}

void DfService::stop()
{
    if (!running_.exchange(false)) return;
    sweep_cv_.notify_all();
    if (amqp_container_) amqp_container_->stop();
    if (sub_thread_.joinable())   sub_thread_.join();
    if (sweep_thread_.joinable()) sweep_thread_.join();
    spdlog::info("DfService: stopped");
}

void DfService::subscriptionLoop()
{
    while (running_.load()) {
        auto on_det = [this](SnapshotEntry e){ onDetection(std::move(e)); };
        amqp_handler_    = std::make_shared<ServiceAmqpHandler>(cfg_.amqp, on_det);
        amqp_container_  = std::make_shared<proton::container>(*amqp_handler_);
        try { amqp_container_->run(); }
        catch (const std::exception& ex) {
            spdlog::error("DfService: AMQP error: {}", ex.what());
        }
        if (!running_.load()) break;
        spdlog::info("DfService: reconnecting in 3 s…");
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

// Background thread: every 250 ms evict stale entries and re-check ready buckets.
void DfService::sweepLoop()
{
    while (running_.load()) {
        std::unique_lock<std::mutex> lk(agg_mu_);
        sweep_cv_.wait_for(lk, std::chrono::milliseconds(250));
        if (!running_.load()) break;

        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::vector<int64_t> ready_buckets;
        for (auto it = pending_.begin(); it != pending_.end(); ) {
            auto& slot = it->second;
            // Evict snapshots older than the aggregation window.
            for (auto si = slot.begin(); si != slot.end(); ) {
                if ((now_ms - si->second.timestamp_ms) > cfg_.df.aggregation_window_ms)
                    si = slot.erase(si);
                else
                    ++si;
            }
            if (slot.empty()) {
                it = pending_.erase(it);
            } else {
                if ((int)slot.size() >= cfg_.df.min_elements)
                    ready_buckets.push_back(it->first);
                ++it;
            }
        }
        lk.unlock();

        for (int64_t bucket : ready_buckets)
            tryCompute(bucket);
    }
}

void DfService::onDetection(SnapshotEntry e)
{
    if (e.iq_snapshot.empty()) return;
    if (e.snr_db < cfg_.df.snr_threshold_db) return;
    if (antenna_map_.find(e.scanner_id) == antenna_map_.end()) return;

    int64_t bucket = static_cast<int64_t>(e.center_freq_hz / 100e3);  // 100 kHz buckets

    bool should_compute = false;
    {
        std::lock_guard<std::mutex> lk(agg_mu_);
        auto& slot = pending_[bucket];
        slot[e.scanner_id] = std::move(e);
        if ((int)slot.size() >= cfg_.df.min_elements)
            should_compute = true;
    }

    if (should_compute)
        tryCompute(bucket);
}

void DfService::tryCompute(int64_t bucket)
{
    std::vector<SnapshotEntry> entries;
    {
        std::lock_guard<std::mutex> lk(agg_mu_);
        auto it = pending_.find(bucket);
        if (it == pending_.end()) return;
        if ((int)it->second.size() < cfg_.df.min_elements) return;
        for (auto& [sid, e] : it->second)
            entries.push_back(e);
        pending_.erase(it);
    }

    // Build ordered lists (same index = same antenna).
    std::vector<std::vector<float>> iq_list;
    std::vector<AntennaElement>     ant_list;
    double freq_hz = 0.0;

    for (const auto& e : entries) {
        auto it = antenna_map_.find(e.scanner_id);
        if (it == antenna_map_.end()) continue;
        iq_list.push_back(e.iq_snapshot);
        ant_list.push_back(it->second);
        freq_hz = e.center_freq_hz;
    }
    if (iq_list.size() < 2) return;

    DfResult r = engine_.compute(iq_list, ant_list, freq_hz);
    if (!r.valid) {
        spdlog::debug("DfService: MUSIC inconclusive at {:.3f} MHz", freq_hz / 1e6);
        return;
    }

    spdlog::info("DfService: {:.3f} MHz → {:.1f}° (confidence={:.2f}, M={})",
                 freq_hz / 1e6, r.azimuth_deg, r.confidence, r.num_elements);

    publishResult(r, ant_list, entries);

#ifdef DF_WITH_DB
    persistResult(r, entries);
#endif
}

void DfService::publishResult(const DfResult& r,
                               const std::vector<AntennaElement>& /*antennas*/,
                               const std::vector<SnapshotEntry>&  snapshots)
{
    json j;
    j["msg_type"]       = "DF_RESULT";
    j["scanner_id"]     = cfg_.scanner_id;
    j["center_freq_hz"] = r.center_freq_hz;
    j["azimuth_deg"]    = r.azimuth_deg;
    j["confidence"]     = r.confidence;
    j["num_elements"]   = r.num_elements;
    j["algorithm"]      = r.algorithm;

    json ids = json::array();
    for (const auto& e : snapshots) ids.push_back(e.scanner_id);
    j["contributing_scanners"] = ids;

    if (amqp_handler_)
        amqp_handler_->publish(j.dump());
}

#ifdef DF_WITH_DB
void DfService::persistResult(const DfResult& r,
                               const std::vector<SnapshotEntry>& snapshots)
{
    if (db_conn_str_.empty()) return;
    if (!db_conn_ || !db_conn_->is_open()) {
        try { db_conn_ = std::make_unique<pqxx::connection>(db_conn_str_); }
        catch (const std::exception& ex) {
            spdlog::warn("DfService: DB reconnect failed: {}", ex.what());
            db_conn_.reset();
            return;
        }
    }

    std::string scanner_ids;
    for (size_t i = 0; i < snapshots.size(); ++i) {
        if (i) scanner_ids += ',';
        scanner_ids += snapshots[i].scanner_id;
    }

    try {
        pqxx::work tx{*db_conn_};
        tx.exec_params(
            "INSERT INTO df_results"
            " (center_freq_hz, azimuth_deg, confidence, num_elements,"
            "  algorithm, scanner_ids)"
            " VALUES ($1,$2,$3,$4,$5,$6)",
            static_cast<int64_t>(r.center_freq_hz),
            r.azimuth_deg,
            r.confidence,
            r.num_elements,
            r.algorithm,
            scanner_ids);
        tx.commit();
    } catch (const std::exception& ex) {
        spdlog::warn("DfService: DB write failed: {}", ex.what());
        db_conn_.reset();
    }
}
#endif

} // namespace df
