// Fuzz the online interface: Ruckig<DynamicDOFs>::update() in a control loop
// (validate -> TargetCalculator -> brake / position_* / velocity_* step1+step2 -> roots),
// feeding each new state back with OutputParameter::pass_to_input, then sample the
// final trajectory. Disabled DoFs, the velocity interface and infinite jerk reach the
// code fixed upstream in 996b169 (uninitialised per-DoF values / brake times).
//
// Layout: [dofs byte][flags byte] then per DoF: 9 numbers (current p/v/a, target p/v/a,
// max v/a/j) + an enabled byte; then a time-fraction byte, a DoF byte and a position number.
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

    Ruckig<DynamicDOFs> otg(dofs, 0.01);
    InputParameter<DynamicDOFs> input(dofs);
    OutputParameter<DynamicDOFs> output(dofs);

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

    // A bounded control loop: Ruckig returns Working until the target is reached.
    Result result = Result::Working;
    for (int step = 0; step < 64; ++step) {
        result = otg.update(input, output);
        if (result != Result::Working) {
            break;
        }
        output.pass_to_input(input);
    }
    if (result < 0) {
        return 0;  // invalid input or no solution: a documented error result, not a bug
    }

    const auto& trajectory = output.trajectory;
    std::vector<double> p(dofs), v(dofs), a(dofs);
    trajectory.at_time(trajectory.get_duration() * 0.5, p, v, a);
    // Query times are fractions of the duration (0 .. 1.5x), so this target spends its time in the
    // update loop; fuzz_calculate passes arbitrary query times (NaN, negative) to at_time().
    trajectory.at_time(trajectory.get_duration() * (r.byte() / 170.0), p, v, a);
    std::vector<Bound> extrema(dofs);
    trajectory.get_position_extrema(extrema);
    (void)trajectory.get_first_time_at_position(r.byte() % dofs, r.number());
    return 0;
}
