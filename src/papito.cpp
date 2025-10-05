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

static int EventSet = PAPI_NULL;
static std::vector<int> event_codes;
static std::vector<std::string> event_names;
static bool papito_inited = false;
static bool papito_running = false;
static bool used_multiplex = false;
static bool multiplex_init_ok = false;

static const char* DEFAULT_COUNTERS_FILE = "counters.in";

static void die_with_msg(const std::string &s) {
    std::cerr << "[papito][FATAL] " << s << std::endl;
    std::exit(1);
}
static void warn_msg(const std::string &s) { std::cerr << "[papito][WARN] " << s << std::endl; }
static void info_msg(const std::string &s) { std::cerr << "[papito][INFO] " << s << std::endl; }

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
        warn_msg("Could not open counters file: " + path + " (errno=" + std::to_string(errno) + ")");
        return lines;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#') continue;
        lines.push_back(t);
    }
    return lines;
}

static int get_num_counters_fallback() {
    return PAPI_num_hwctrs();
}

static void show_papi_info() {
    const PAPI_hw_info_t *hw = PAPI_get_hardware_info();
    if (hw) {
        std::string vendor = hw->vendor_string ? hw->vendor_string : "unknown";
        std::string model  = hw->model_string  ? hw->model_string  : "unknown";
        info_msg(std::string("PAPI hardware info: vendor=") + vendor + " model=" + model);
    } else {
        info_msg("PAPI_get_hardware_info() returned NULL or not available.");
    }

    int hwcnt = get_num_counters_fallback();
    info_msg(std::string("Hardware counters available: ") + std::to_string(hwcnt));
}

// Substituir a implementação anterior por esta:
static void prepare_eventset_from_file(const std::string &path) {
    auto lines = read_counters_file(path);
    if (lines.empty()) {
        warn_msg("No events read from file '" + path + "'. No events will be measured.");
        return;
    }

    int num_hw_counters = get_num_counters_fallback();
    info_msg("Hardware counters available: " + std::to_string(num_hw_counters));

    int ret = PAPI_create_eventset(&EventSet);
    if (ret != PAPI_OK) {
        die_with_msg(std::string("PAPI_create_eventset failed: ") + PAPI_strerror(ret));
    }

    // We'll add the first event first (so PAPI can set the component for the eventset).
    bool first_added = false;

    for (size_t idx = 0; idx < lines.size(); ++idx) {
        const std::string &ename = lines[idx];
        int code = 0;
        int tret = PAPI_event_name_to_code(const_cast<char*>(ename.c_str()), &code);
        if (tret != PAPI_OK) {
            // try uppercase fallback
            std::string alt = ename;
            std::transform(alt.begin(), alt.end(), alt.begin(), ::toupper);
            tret = PAPI_event_name_to_code(const_cast<char*>(alt.c_str()), &code);
        }
        if (tret != PAPI_OK) {
            warn_msg("Event not available (skipping): " + ename);
            continue;
        }

        // If this is the first event to add, add it directly (to set component)
        if (!first_added) {
            int addret = PAPI_add_event(EventSet, code);
            if (addret == PAPI_OK) {
                event_codes.push_back(code);
                event_names.push_back(ename);
                first_added = true;
                info_msg("Added first event (component set): " + ename);

                // Now that the eventset has a component, enable multiplex if possible
                if (multiplex_init_ok) {
                    int mret = PAPI_set_multiplex(EventSet);
                    if (mret != PAPI_OK) {
                        warn_msg(std::string("PAPI_set_multiplex(EventSet) failed after first add: ") + PAPI_strerror(mret) + ". Continuing without multiplex.");
                        used_multiplex = false;
                    } else {
                        used_multiplex = true;
                        info_msg("PAPI_set_multiplex succeeded after first add: multiplex enabled for eventset.");
                    }
                }
                continue; // go to next event
            } else {
                // Failed to add the first event. If multiplex is available, try enabling it and retry.
                warn_msg(std::string("Failed to add first event '") + ename + "': " + PAPI_strerror(addret));
                if (multiplex_init_ok) {
                    info_msg("Attempting to enable multiplex and retry adding this event...");
                    int mret = PAPI_set_multiplex(EventSet);
                    if (mret != PAPI_OK) {
                        warn_msg(std::string("Retry PAPI_set_multiplex failed: ") + PAPI_strerror(mret));
                        // give up on this event
                        continue;
                    } else {
                        used_multiplex = true;
                        int addret2 = PAPI_add_event(EventSet, code);
                        if (addret2 == PAPI_OK) {
                            event_codes.push_back(code);
                            event_names.push_back(ename);
                            first_added = true;
                            info_msg("Added first event after enabling multiplex: " + ename);
                            continue;
                        } else {
                            warn_msg(std::string("Still failed to add event '") + ename + "' after enabling multiplex: " + PAPI_strerror(addret2));
                            continue;
                        }
                    }
                } else {
                    // multiplex not available - skip this event
                    continue;
                }
            }
        } // end first event handling

        // For subsequent events, just add normally (EventSet already has component).
        int addret = PAPI_add_event(EventSet, code);
        if (addret != PAPI_OK) {
            warn_msg(std::string("Failed to add event '") + ename + "': " + PAPI_strerror(addret));
            // If we didn't have multiplex and multiplex_init_ok is true, we can try enabling multiplex now and retry
            if (!used_multiplex && multiplex_init_ok) {
                info_msg("Attempting to enable multiplex now and retry adding event: " + ename);
                int mret = PAPI_set_multiplex(EventSet);
                if (mret == PAPI_OK) {
                    used_multiplex = true;
                    int addret2 = PAPI_add_event(EventSet, code);
                    if (addret2 == PAPI_OK) {
                        event_codes.push_back(code);
                        event_names.push_back(ename);
                        info_msg("Added event after enabling multiplex: " + ename);
                        continue;
                    } else {
                        warn_msg(std::string("Retry add failed for '") + ename + "': " + PAPI_strerror(addret2));
                        continue;
                    }
                } else {
                    warn_msg(std::string("PAPI_set_multiplex failed when retrying: ") + PAPI_strerror(mret));
                    continue;
                }
            } else {
                // couldn't add and can't enable multiplex - skip
                continue;
            }
        } else {
            // success
            event_codes.push_back(code);
            event_names.push_back(ename);
            info_msg("Added event: " + ename);
        }
    } // end for

    if (event_codes.empty()) {
        warn_msg("No events successfully added to EventSet. PAPI will run but not measure counters.");
    } else {
        info_msg("Total events added: " + std::to_string(event_codes.size()));
        info_msg(std::string("Multiplexing: ") + (used_multiplex ? "ON" : "OFF"));
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

    // show build/version info
    show_papi_info();

    // Try to initialize multiplexing support AS EARLY AS POSSIBLE.
    int mret = PAPI_multiplex_init();
    if (mret != PAPI_OK) {
        warn_msg(std::string("PAPI_multiplex_init() failed: ") + PAPI_strerror(mret)
                 + ". Multiplexing unavailable. Consider rebuilding PAPI with --enable-multiplex.");
        multiplex_init_ok = false;
    } else {
        info_msg("PAPI_multiplex_init() succeeded.");
        multiplex_init_ok = true;
    }

    // counters file
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
        info_msg("No events configured; papito_start will do nothing.");
        papito_running = true;
        return;
    }

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

    if (event_codes.empty()) {
        info_msg("No counters configured; papito_end returning without output.");
        papito_running = false;
        return;
    }

    std::vector<long long> values(event_codes.size(), 0LL);
    int ret = PAPI_stop(EventSet, values.data());
    if (ret != PAPI_OK) {
        warn_msg(std::string("PAPI_stop returned error: ") + PAPI_strerror(ret));
        // Try PAPI_read as fallback
        if (PAPI_read(EventSet, values.data()) != PAPI_OK) {
            warn_msg("PAPI_read also failed; cannot produce counter values.");
            papito_running = false;
            return;
        }
    }

    // *** FIX: Print to stderr to avoid interfering with stdout data ***
    std::cerr << "PAPITO_COUNTERS";
    for (const auto& name : event_names) std::cerr << "\t" << name;
    std::cerr << std::endl;

    std::cerr << "PAPITO_VALUES";
    for (size_t i = 0; i < values.size(); ++i) std::cerr << "\t" << values[i];
    std::cerr << std::endl;

    papito_running = false;
}

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
