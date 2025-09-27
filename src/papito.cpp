#include "papito.h"

#include <papi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>

// Internal state
static int EventSet = PAPI_NULL;
static std::vector<int> event_codes;
static std::vector<std::string> event_names;
static bool papito_inited = false;
static bool papito_running = false;
static bool used_multiplex = false;

static const char* DEFAULT_COUNTERS_FILE = "counters.in";

static void die_with_msg(const std::string &s) {
    std::cerr << "[papito][FATAL] " << s << std::endl;
    std::exit(1);
}

static void warn_msg(const std::string &s) {
    std::cerr << "[papito][WARN] " << s << std::endl;
}

static void info_msg(const std::string &s) {
    std::cerr << "[papito][INFO] " << s << std::endl;
}

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::vector<std::string> read_counters_file(const std::string &path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in.is_open()) {
        warn_msg("Could not open counters file: " + path + " (errno=" + std::to_string(errno) + "). Trying default location.");
        return lines;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t.size() > 0 && t[0] == '#') continue;
        lines.push_back(t);
    }
    return lines;
}

static void prepare_eventset_from_file(const std::string &path) {
    auto lines = read_counters_file(path);
    if (lines.empty()) {
        warn_msg("No events read from file '" + path + "'. No events will be measured.");
        return;
    }

    int num_hw_counters = PAPI_num_hwctrs();
    if (num_hw_counters <= 0) {
        warn_msg("PAPI_num_counters() reports " + std::to_string(num_hw_counters) + ". Multiplex will be used if needed.");
        num_hw_counters = 0;
    }
    info_msg("Hardware counters available: " + std::to_string(num_hw_counters));

    // Create eventset
    if (PAPI_create_eventset(&EventSet) != PAPI_OK) {
        die_with_msg("PAPI_create_eventset failed");
    }

    // If we will add more events than counters, enable multiplex
    if ((int)lines.size() > num_hw_counters) {
        if (PAPI_multiplex_init() != PAPI_OK) {
            warn_msg("PAPI_multiplex_init failed - trying to continue without multiplex. Results may be incomplete.");
        } else {
            if (PAPI_set_multiplex(EventSet) != PAPI_OK) {
                warn_msg("PAPI_set_multiplex failed - multiplex might not be active");
            } else {
                used_multiplex = true;
                info_msg("Using PAPI multiplexing (event count > hardware counters).");
            }
        }
    }

    // Try to add each event by name
    for (const auto &ename : lines) {
        int code = 0;
        int ret = PAPI_event_name_to_code(const_cast<char*>(ename.c_str()), &code);
        if (ret != PAPI_OK) {
            // try mapping native name by trying uppercase/lowercase variants (some toolchains)
            std::string alt = ename;
            // try uppercase
            std::transform(alt.begin(), alt.end(), alt.begin(), ::toupper);
            ret = PAPI_event_name_to_code(const_cast<char*>(alt.c_str()), &code);
        }
        if (ret != PAPI_OK) {
            warn_msg("Event not available (skipping): " + ename + "   (PAPI_event_name_to_code error: " + std::to_string(ret) + ")");
            continue;
        }
        // Add event
        ret = PAPI_add_event(EventSet, code);
        if (ret != PAPI_OK) {
            // If addition fails, warn but continue
            warn_msg("Failed to add event '" + ename + "' (code=" + std::to_string(code) + ") -> PAPI_add_event returned " + std::to_string(ret));
            continue;
        }
        event_codes.push_back(code);
        event_names.push_back(ename);
        info_msg("Added event: " + ename);
    }

    if (event_codes.empty()) {
        warn_msg("No events successfully added to EventSet. PAPI will be initialized but no counters measured.");
    }
}

void papito_init() {
    if (papito_inited) return;
    int retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT && retval > 0) {
        die_with_msg("PAPI_library_init version mismatch");
    } else if (retval < 0) {
        die_with_msg("PAPI_library_init failed");
    }
    info_msg("PAPI initialized.");

    // Decide counters file (env PAPITO_COUNTERS overrides)
    const char* envp = std::getenv("PAPITO_COUNTERS");
    std::string counters_file = envp ? std::string(envp) : std::string(DEFAULT_COUNTERS_FILE);
    info_msg("Reading counters from: " + counters_file);
    prepare_eventset_from_file(counters_file);

    papito_inited = true;
}

void papito_start() {
    if (!papito_inited) papito_init();
    if (papito_running) return;

    if (event_codes.empty()) {
        info_msg("No events to start. papito_start will return without starting counters.");
        papito_running = true; // mark running to match start/stop semantics
        return;
    }

    long long zero = 0;
    std::vector<long long> zeros(event_codes.size(), 0LL);

    int ret = PAPI_start(EventSet);
    if (ret != PAPI_OK) {
        warn_msg(std::string("PAPI_start failed: ") + PAPI_strerror(ret));
    } else {
        papito_running = true;
    }
}

void papito_end() {
    if (!papito_inited) {
        warn_msg("papito_end called but papito was not initialized.");
        return;
    }
    if (!papito_running) {
        // still attempt to read if eventset exists
    }

    if (event_codes.empty()) {
        info_msg("No counters were configured; papito_end returning without output.");
        papito_running = false;
        return;
    }

    std::vector<long long> values(event_codes.size(), 0LL);
    int ret = PAPI_stop(EventSet, values.data());
    if (ret != PAPI_OK) {
        warn_msg(std::string("PAPI_stop returned error: ") + PAPI_strerror(ret));
        // Try to read anyway
        if (PAPI_read(EventSet, values.data()) != PAPI_OK) {
            warn_msg("PAPI_read also failed; aborting printing counters.");
            papito_running = false;
            return;
        }
    }

    // Print header (CSV-friendly)
    std::cout << "PAPITO_COUNTERS";
    for (const auto& name : event_names) {
        std::cout << "\t" << name;
    }
    std::cout << std::endl;

    // Print values
    std::cout << "PAPITO_VALUES";
    for (size_t i = 0; i < values.size(); ++i) {
        std::cout << "\t" << values[i];
    }
    std::cout << std::endl;

    papito_running = false;
}

// Optional finalize (destroy eventset, cleanup)
void papito_finalize() {
    if (!papito_inited) return;
    if (EventSet != PAPI_NULL) {
        PAPI_cleanup_eventset(EventSet);
        PAPI_destroy_eventset(&EventSet);
        EventSet = PAPI_NULL;
    }
    PAPI_shutdown();
    papito_inited = false;
    info_msg("papito finalized.");
}

