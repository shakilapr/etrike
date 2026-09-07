// Differential testing driver for DiagnosticManager (§3.2 & §3.3)
#include <iostream>
#include <string>
#include <sstream>
#include "shared/diagnostics.h"

namespace diag = etrike::diagnostics;

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    diag::DiagnosticManager mgr{};
    std::string line;

    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        char cmd;
        if (!(iss >> cmd)) continue;

        if (cmd == 'Q') {
            break;
        } else if (cmd == 'R') {
            std::uint16_t raw_id = 0;
            iss >> std::hex >> raw_id;
            std::uint32_t snap = 0;
            if (iss >> std::dec >> snap) {
                mgr.raise(static_cast<diag::DiagId>(raw_id), static_cast<std::uint16_t>(snap));
            } else {
                mgr.raise(static_cast<diag::DiagId>(raw_id));
            }
            std::cout << "OK\n";
        } else if (cmd == 'V') {
            std::uint16_t raw_id = 0;
            iss >> std::hex >> raw_id;
            mgr.recover(static_cast<diag::DiagId>(raw_id));
            std::cout << "OK\n";
        } else if (cmd == 'C') {
            std::uint16_t raw_id = 0;
            iss >> std::hex >> raw_id;
            mgr.clear(static_cast<diag::DiagId>(raw_id));
            std::cout << "OK\n";
        } else if (cmd == 'A') {
            mgr.clear_all();
            std::cout << "OK\n";
        } else if (cmd == 'E') {
            mgr.on_estop_episode_cleared();
            std::cout << "OK\n";
        } else if (cmd == 'P') {
            diag::DiagReport rep{};
            if (mgr.pop_pending_report(rep)) {
                std::cout << "REP " << std::hex << static_cast<std::uint16_t>(rep.id)
                          << " " << std::dec << static_cast<int>(rep.state)
                          << " " << static_cast<int>(rep.occurrence_count)
                          << " " << static_cast<int>(rep.report_counter)
                          << " " << static_cast<int>(rep.flags)
                          << " " << static_cast<int>(rep.snapshot_data) << "\n";
            } else {
                std::cout << "NONE\n";
            }
        } else if (cmd == 'S') {
            std::uint16_t raw_id = 0;
            iss >> std::hex >> raw_id;
            std::cout << "STATE " << static_cast<int>(mgr.state_of(static_cast<diag::DiagId>(raw_id))) << "\n";
        } else if (cmd == 'O') {
            std::uint16_t raw_id = 0;
            iss >> std::hex >> raw_id;
            std::cout << "OCC " << static_cast<int>(mgr.occurrence_of(static_cast<diag::DiagId>(raw_id))) << "\n";
        }
        std::cout.flush();
    }
    return 0;
}
