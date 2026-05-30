#pragma once
#include <string>
#include <vector>
#include "au/units/hertz.hh"
#include "au/units/seconds.hh"

namespace df {

struct AmqpConfig {
    std::string url              = "amqp://localhost:5672";
    std::string username         = "sdr_ctrl";
    std::string password         = "password";
    std::string detections_topic = "rf.detections";
    std::string df_results_topic = "rf.df_results";
};

struct DbConfig {
    std::string host     = "localhost";
    int         port     = 5432;
    std::string dbname   = "sdr_scanner";
    std::string user     = "sdr";
    std::string password = "";
    bool        enabled  = false;
};

// One entry per PlutoSDR. Positions are in metres, ENU frame:
//   x = East, y = North, z = Up, relative to any fixed reference point.
struct AntennaElement {
    std::string scanner_id;
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct DfParams {
    std::string               algorithm          = "MUSIC";
    au::QuantityD<au::Seconds> aggregation_window = au::seconds(5.0); // max age of a snapshot for aggregation
    int                        min_elements       = 3;                // minimum antennas needed for a solution
    double                     angle_step_deg     = 0.5;             // MUSIC sweep resolution
    double                     snr_threshold_db   = 3.0;             // minimum per-antenna SNR to include
};

struct AppConfig {
    AmqpConfig              amqp;
    DbConfig                db;
    std::vector<AntennaElement> antennas;   // one per scanner_id, in any order
    DfParams                df;
    std::string             scanner_id = "df-0";
};

AppConfig parseConfig(const std::string& xml_path);

} // namespace df
