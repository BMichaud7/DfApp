#include "Config.hpp"
#include <tinyxml2.h>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace df {

namespace {

static std::string xmlText(const tinyxml2::XMLElement* p, const char* name,
                            const std::string& def = "")
{
    if (!p) return def;
    const auto* el = p->FirstChildElement(name);
    if (!el || !el->GetText()) return def;
    return std::string(el->GetText());
}

static int xmlInt(const tinyxml2::XMLElement* p, const char* name, int def = 0)
{
    std::string s = xmlText(p, name, "");
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

static double xmlDouble(const tinyxml2::XMLElement* p, const char* name, double def = 0.0)
{
    std::string s = xmlText(p, name, "");
    if (s.empty()) return def;
    try { return std::stod(s); } catch (...) { return def; }
}

} // namespace

AppConfig parseConfig(const std::string& xml_path)
{
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(xml_path.c_str()) != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("Failed to load config: " + xml_path +
                                 " — " + doc.ErrorStr());

    const auto* root = doc.FirstChildElement("df_config");
    if (!root)
        throw std::runtime_error("Config XML missing root <df_config>");

    AppConfig cfg;
    cfg.scanner_id = xmlText(root, "scanner_id", cfg.scanner_id);

    // <amqp>
    const auto* amqp = root->FirstChildElement("amqp");
    if (amqp) {
        cfg.amqp.url              = xmlText(amqp, "url",              cfg.amqp.url);
        cfg.amqp.username         = xmlText(amqp, "username",         cfg.amqp.username);
        cfg.amqp.password         = xmlText(amqp, "password",         cfg.amqp.password);
        cfg.amqp.detections_topic = xmlText(amqp, "detections_topic", cfg.amqp.detections_topic);
        cfg.amqp.df_results_topic = xmlText(amqp, "df_results_topic", cfg.amqp.df_results_topic);
    }

    // <database>
    const auto* db = root->FirstChildElement("database");
    if (db) {
        cfg.db.host     = xmlText(db, "host",     cfg.db.host);
        cfg.db.port     = xmlInt (db, "port",     cfg.db.port);
        cfg.db.dbname   = xmlText(db, "name",     cfg.db.dbname);
        cfg.db.user     = xmlText(db, "user",     cfg.db.user);
        cfg.db.password = xmlText(db, "password", cfg.db.password);
        cfg.db.enabled  = true;
    }

    // <array> — one <antenna> child per PlutoSDR
    const auto* arr = root->FirstChildElement("array");
    if (arr) {
        for (const auto* el = arr->FirstChildElement("antenna"); el;
             el = el->NextSiblingElement("antenna")) {
            AntennaElement a;
            a.scanner_id = el->Attribute("scanner_id") ? el->Attribute("scanner_id") : "";
            if (const char* v = el->Attribute("x")) a.x = std::stod(v);
            if (const char* v = el->Attribute("y")) a.y = std::stod(v);
            if (const char* v = el->Attribute("z")) a.z = std::stod(v);
            if (!a.scanner_id.empty())
                cfg.antennas.push_back(a);
        }
    }

    // <df>
    const auto* df = root->FirstChildElement("df");
    if (df) {
        cfg.df.algorithm             = xmlText  (df, "algorithm",             cfg.df.algorithm);
        cfg.df.aggregation_window_ms = xmlInt   (df, "aggregation_window_ms", cfg.df.aggregation_window_ms);
        cfg.df.min_elements          = xmlInt   (df, "min_elements",          cfg.df.min_elements);
        cfg.df.angle_step_deg        = xmlDouble(df, "angle_step_deg",        cfg.df.angle_step_deg);
        cfg.df.snr_threshold_db      = xmlDouble(df, "snr_threshold_db",      cfg.df.snr_threshold_db);
    }

    spdlog::info("DfApp config: scanner={} antennas={} algorithm={} min_elements={}",
                 cfg.scanner_id, cfg.antennas.size(),
                 cfg.df.algorithm, cfg.df.min_elements);
    return cfg;
}

} // namespace df
