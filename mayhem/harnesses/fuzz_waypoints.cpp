// Fuzz the waypoint path: Ruckig<DynamicDOFs>::filter_intermediate_positions() (index
// arithmetic over the active-waypoint mask) and validate_input() / calculate() with
// intermediate positions and per-section limits. Built without the cloud client, so
// nothing reaches the network (calculate() then plans straight to the target).
// Bit 0 of the flags byte turns on the current-state-within-limits checks.
//
// Layout: [dofs][n waypoints][flags], current/target position + limits per DoF,
// n waypoint vectors, a threshold vector, then optional per-section limits.
#include <cstddef>
#include <cstdint>
#include <vector>

#include <ruckig/ruckig.hpp>

#include "fuzz_reader.hpp"

using namespace ruckig;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzReader r {data, size};
    const size_t dofs = 1 + r.byte() % 3;
    const size_t n_waypoints = r.byte() % 17;
    const uint8_t flags = r.byte();

    // Without the cloud client Ruckig has no waypoint-capacity constructor (max_number_of_waypoints
    // is 0), so validate_input() rejects waypoints for the position interface and accepts them,
    // unused, for the velocity interface.
    Ruckig<DynamicDOFs> otg(dofs, 0.01);
    InputParameter<DynamicDOFs> input(dofs);

    input.control_interface = (flags & 2) ? ControlInterface::Velocity : ControlInterface::Position;
    for (size_t dof = 0; dof < dofs; ++dof) {
        input.current_position[dof] = r.number();
        input.target_position[dof] = r.number();
        input.max_velocity[dof] = r.number();
        input.max_acceleration[dof] = r.number();
        input.max_jerk[dof] = r.number();
    }

    std::vector<std::vector<double>> waypoints(n_waypoints, std::vector<double>(dofs));
    for (auto& w : waypoints) {
        for (auto& x : w) x = r.number();
    }
    input.intermediate_positions = waypoints;

    std::vector<double> threshold(dofs);
    for (auto& x : threshold) x = r.number();

    const auto filtered = otg.filter_intermediate_positions(input, threshold);
    if (filtered.size() > n_waypoints) {
        __builtin_trap();  // the filter may only remove waypoints
    }
    if (flags & 4) {
        input.intermediate_positions = filtered;
    }

    const size_t sections = input.intermediate_positions.size() + 1;
    auto per_section = [&]() {
        std::vector<std::vector<double>> s(sections, std::vector<double>(dofs));
        for (auto& v : s) {
            for (auto& x : v) x = r.number();
        }
        return s;
    };
    if (flags & 0x08) input.per_section_max_velocity = per_section();
    if (flags & 0x10) input.per_section_max_acceleration = per_section();
    if (flags & 0x20) input.per_section_max_jerk = per_section();
    if (flags & 0x40) input.per_section_max_position = per_section();
    if (flags & 0x80) input.per_section_min_position = per_section();

    if (!otg.validate_input<false>(input, flags & 1, true)) {
        return 0;
    }
    Trajectory<DynamicDOFs> trajectory(dofs);
    if (otg.calculate(input, trajectory) >= 0) {
        std::vector<double> p(dofs);
        trajectory.at_time(trajectory.get_duration() * 0.5, p);
    }
    return 0;
}
