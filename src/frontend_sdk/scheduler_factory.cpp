#include "scheduler_factory.hpp"

#include <utility>

namespace mnemos::frontend_sdk {

    runtime::scheduler default_scheduler_factory::create(
        std::vector<runtime::scheduled_chip> chips, chips::ivideo* frame_source) {
        return runtime::scheduler(std::move(chips), frame_source);
    }

} // namespace mnemos::frontend_sdk
