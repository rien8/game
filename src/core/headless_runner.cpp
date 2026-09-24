// src/core/headless_runner.cpp
#include "core/headless_runner.hpp"

namespace game {

auto HeadlessRunner::run(const Config& cfg) -> Summary {
    (void)cfg;
    return Summary{};
}

}  // namespace game
