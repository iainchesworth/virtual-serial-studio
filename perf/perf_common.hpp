//
// Copyright (c) 2026 Virtual Serial Studio contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared by the throughput benchmarks in perf_main.cpp and the per-component
// micro-benchmarks in component_benchmarks.cpp.
//

#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace vss_perf
{

struct measurement
{
    std::string name;
    double value = 0.0;
    std::string unit;
    std::string note;
};

inline double seconds_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         start)
        .count();
}

/// The per-component micro-benchmarks: one measurement per library component
/// worth watching (byte formatting, traffic log, configuration validation,
/// studio topology operations). Each runs inside a `bench.<name>` zone, so a
/// Tracy capture of `vss_perf` shows what the component's own zones cost.
std::vector<measurement> run_component_benchmarks(bool quick);

} // namespace vss_perf
