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
#include "Config.hpp"
#include "DfService.hpp"
#include <spdlog/spdlog.h>
#include <csignal>
#include <atomic>
#include <string>
#include <cstdlib>

static std::atomic<bool> g_stop{false};

static void on_signal(int) { g_stop.store(true); }

int main(int argc, char* argv[])
{
    // Log level from env (matches AcquisitionApp / AnalysisApp convention)
    const char* log_level_env = std::getenv("SDR_LOG_LEVEL");
    if (log_level_env) {
        std::string lvl = log_level_env;
        if (lvl == "debug")   spdlog::set_level(spdlog::level::debug);
        else if (lvl == "warn")    spdlog::set_level(spdlog::level::warn);
        else if (lvl == "error")   spdlog::set_level(spdlog::level::err);
        else                       spdlog::set_level(spdlog::level::info);
    }

    std::string cfg_path = "/etc/sdr-df/df.xml";
    if (argc > 1) cfg_path = argv[1];
    const char* cfg_env = std::getenv("SDR_DF_CONFIG_PATH");
    if (cfg_env) cfg_path = cfg_env;

    spdlog::info("DfApp v1.0.0 — config={}", cfg_path);

    df::AppConfig cfg;
    try {
        cfg = df::parseConfig(cfg_path);
    } catch (const std::exception& ex) {
        spdlog::critical("Failed to load config: {}", ex.what());
        return 1;
    }

    if (cfg.antennas.size() < 2) {
        spdlog::critical("At least 2 <antenna> elements required in <array>");
        return 1;
    }

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    df::DfService service(cfg);
    service.start();

    spdlog::info("DfApp running — Ctrl+C to stop");
    while (!g_stop.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    service.stop();
    return 0;
}
