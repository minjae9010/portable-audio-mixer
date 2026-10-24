#pragma once
#include <windows.h>
#include <cfgmgr32.h>
#include <string>

namespace cable_install_finish {
enum class Status { Disabled, NotFound, Ambiguous, Failed };
struct Result { Status status; CONFIGRET error; };
// PnP may publish the device after the vendor process has exited. Bound retries
// and require an observed disabled problem code before reporting success.
template<class Enumerate, class Disable, class Verify, class Pause>
Result settle(Enumerate enumerate, Disable disable, Verify verify, Pause pause) {
    bool seen = false;
    CONFIGRET last = CR_NO_SUCH_DEVNODE;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        const auto devices = enumerate();
        if (devices.size() > 1) return {Status::Ambiguous, CR_INVALID_DEVNODE};
        if (devices.size() == 1) {
            seen = true;
            last = disable(devices.front());
            if (last == CR_SUCCESS && verify(devices.front())) return {Status::Disabled, CR_SUCCESS};
            if (last == CR_SUCCESS) last = CR_FAILURE;
        }
        if (attempt != 19) pause();
    }
    return {seen ? Status::Failed : Status::NotFound, last};
}
}
