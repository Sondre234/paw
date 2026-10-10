// SPDX-License-Identifier: GPL-3.0-or-later
#include "power_mode.hpp"

void PowerMode::update(State state) {
    if (!state.available)
        state = State{};
    if (state.available == state_.available && state.profile == state_.profile &&
        state.profiles == state_.profiles && state.degraded == state_.degraded)
        return;
    state_ = std::move(state);
    Q_EMIT changed();
}
void PowerMode::setProfile(const QString &profile) {
    if (!state_.available || profile == state_.profile || !state_.profiles.contains(profile))
        return;
    state_.profile = profile;
    Q_EMIT changed();
    sendProfile(profile);
}

#if !PAW_POWER_PROFILES
// Without Qt's D-Bus module there is no daemon to find.
std::unique_ptr<PowerMode> makePowerMode() { return std::make_unique<PowerMode>(); }
#endif
