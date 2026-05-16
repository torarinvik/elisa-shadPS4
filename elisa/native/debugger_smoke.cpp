#include "shadps4_elisa_debugger.h"

#include <cstdint>
#include <iostream>

int main() {
    const intptr_t self_pid = shadps4_elisa_current_pid();
    if (self_pid <= 0) {
        std::cerr << "expected current pid to be positive, got " << self_pid << "\n";
        return 1;
    }

    if (!shadps4_elisa_process_exists(self_pid)) {
        std::cerr << "expected current pid " << self_pid << " to exist\n";
        return 1;
    }

    constexpr intptr_t unlikely_pid = 99999999;
    if (shadps4_elisa_process_exists(unlikely_pid)) {
        std::cerr << "unexpectedly found process " << unlikely_pid << "\n";
        return 1;
    }

    if (!shadps4_elisa_wait_for_pid_exit(unlikely_pid, 1, 0)) {
        std::cerr << "expected wait-for-pid to finish immediately for missing process\n";
        return 1;
    }

    const intptr_t debugger_attached = shadps4_elisa_is_debugger_attached();
    if (debugger_attached != 0 && debugger_attached != 1) {
        std::cerr << "expected debugger-attached probe to return a bool-like value, got "
                  << debugger_attached << "\n";
        return 1;
    }

    if (shadps4_elisa_wait_for_debugger_attach(1, 0) != debugger_attached) {
        std::cerr << "expected zero-poll debugger wait to match direct debugger probe\n";
        return 1;
    }

    std::cout << "Elisa debugger FFI smoke ok\n";
    return 0;
}
