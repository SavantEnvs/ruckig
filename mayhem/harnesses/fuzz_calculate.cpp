// Fuzz the offline interface: Ruckig<DynamicDOFs>::calculate(InputParameter, Trajectory)
// with every optional input field (min velocity/acceleration, position limits, per-DoF
// control interface and synchronization, minimum duration, infinite limits for the
// first/second-order paths), then query the resulting Trajectory.
//
// Layout: [dofs][flags][presence bits][delta_time choice], per DoF 9 numbers + enabled
// byte, then each present optional field, then query numbers.
#include <cstddef>
#include <cstdint>
#include <vector>

#include <ruckig/ruckig.hpp>

#include "fuzz_reader.hpp"

using namespace ruckig;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzReader r {data, size};
    const size_t dofs = 1 + r.byte() % 3;
    const uint8_t flags = r.byte();
    const uint8_t present = r.byte();
    static const double delta_times[] = {0.01, 0.001, 0.1, 0.0};
    const double delta_time = delta_times[r.byte() % 4];

    Ruckig<DynamicDOFs> otg(dofs, delta_time);
    InputParameter<DynamicDOFs> input(dofs);
    Trajectory<DynamicDOFs> trajectory(dofs);

    input.control_interface = (flags & 1) ? ControlInterface::Velocity : ControlInterface::Position;
    input.synchronization = static_cast<Synchronization>((flags >> 1) % 4);
    input.duration_discretization = (flags & 8) ? DurationDiscretization::Discrete : DurationDiscretization::Continuous;

    for (size_t dof = 0; dof < dofs; ++dof) {
        input.current_position[dof] = r.number();
        input.current_velocity[dof] = r.number();
        input.current_acceleration[dof] = r.number();
        input.target_position[dof] = r.number();
        input.target_velocity[dof] = r.number();
        input.target_acceleration[dof] = r.number();
        input.max_velocity[dof] = r.number();
        input.max_acceleration[dof] = r.number();
        input.max_jerk[dof] = r.number();
        input.enabled[dof] = !(r.byte() & 1);
    }

    if (present & 0x01) {
        std::vector<double> vmin(dofs);
        for (auto& x : vmin) x = r.number();
        input.min_velocity = vmin;
    }
    if (present & 0x02) {
        std::vector<double> amin(dofs);
        for (auto& x : amin) x = r.number();
        input.min_acceleration = amin;
    }
    if (present & 0x04) {
        for (size_t dof = 0; dof < dofs; ++dof) {
            input.max_position[dof] = r.number();
            input.min_position[dof] = r.number();
        }
    }
    if (present & 0x08) {
        std::vector<ControlInterface> ci(dofs);
        for (auto& x : ci) x = (r.byte() & 1) ? ControlInterface::Velocity : ControlInterface::Position;
        input.per_dof_control_interface = ci;
    }
    if (present & 0x10) {
        std::vector<Synchronization> sync(dofs);
        for (auto& x : sync) x = static_cast<Synchronization>(r.byte() % 4);
        input.per_dof_synchronization = sync;
    }
    if (present & 0x20) {
        input.minimum_duration = r.number();
    }

    const Result result = otg.calculate(input, trajectory);
    if (result < 0) {
        return 0;  // invalid input or no solution: a documented error result, not a bug
    }

    std::vector<double> p(dofs), v(dofs), a(dofs), j(dofs);
    size_t section = 0;
    trajectory.at_time(r.number(), p, v, a, j, section);
    trajectory.at_time(-1.0, p, v, a);
    trajectory.at_time(trajectory.get_duration() + 1.0, p, v, a);
    std::vector<Bound> extrema(dofs);
    trajectory.get_position_extrema(extrema);
    const size_t dof = r.byte() % dofs;
    const double position = r.number();
    (void)trajectory.get_first_time_at_position(dof, position, r.number());
    (void)trajectory.get_independent_min_durations();
    (void)trajectory.get_intermediate_durations();
    return 0;
}
