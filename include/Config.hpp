/**
 * @file Config.hpp
 * @brief Configuration structures and XML loader for DfApp.
 *
 * All physical quantities use the Au units library (aurora-opensource/au)
 * to prevent unit mismatches at compile time.
 */
#pragma once
#include <string>
#include <vector>
#include "au/units/hertz.hh"
#include "au/units/seconds.hh"

namespace df {

/// @brief AMQP broker connection and topic configuration.
struct AmqpConfig {
    std::string url              = "amqp://localhost:5672"; ///< Broker URL.
    std::string username         = "sdr_ctrl";
    std::string password         = "password";
    std::string detections_topic = "rf.detections";    ///< Source of RF_DETECTION messages.
    std::string df_results_topic = "rf.df_results";    ///< Destination for DF_RESULT messages.
};

/// @brief PostgreSQL connection configuration (optional).
struct DbConfig {
    std::string host     = "localhost";
    int         port     = 5432;
    std::string dbname   = "sdr_scanner";
    std::string user     = "sdr";
    std::string password = "";
    bool        enabled  = false; ///< When false the DB writer is skipped entirely.
};

/**
 * @brief Position and identity of one antenna element.
 *
 * Positions are in metres in the ENU (East-North-Up) frame relative to
 * any fixed reference point shared by all elements.  The scanner_id must
 * match the scanner_id field emitted by the corresponding AcquisitionApp.
 */
struct AntennaElement {
    std::string scanner_id; ///< Matches AcquisitionApp scanner_id.
    double x{0.0};          ///< East offset in metres.
    double y{0.0};          ///< North offset in metres.
    double z{0.0};          ///< Up offset in metres (typically 0 for horizontal arrays).
};

/// @brief Direction-finding algorithm parameters.
struct DfParams {
    std::string                algorithm          = "MUSIC"; ///< Only "MUSIC" supported.
    au::QuantityD<au::Seconds> aggregation_window = au::seconds(5.0); ///< Max snapshot age.
    int                        min_elements       = 3;    ///< Minimum antennas for a solution.
    double                     angle_step_deg     = 0.5;  ///< MUSIC sweep resolution (degrees).
    double                     snr_threshold_db   = 3.0;  ///< Minimum per-antenna SNR to include.
};

/// @brief Top-level application configuration.
struct AppConfig {
    AmqpConfig                  amqp;
    DbConfig                    db;
    std::vector<AntennaElement> antennas;            ///< One per scanner_id, in any order.
    DfParams                    df;
    std::string                 scanner_id = "df-0"; ///< Identity of this DF service instance.
};

/**
 * @brief Parse an XML configuration file into AppConfig.
 * @param xml_path  Path to the df.xml configuration file.
 * @return          Populated AppConfig; throws std::runtime_error on parse failure.
 */
AppConfig parseConfig(const std::string& xml_path);

} // namespace df
