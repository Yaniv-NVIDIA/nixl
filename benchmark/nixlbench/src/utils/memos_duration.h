/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "memos_pipeline.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace nixlbench {

inline uint64_t steadyTimeUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline uint64_t completedBytes(uint64_t batches, uint64_t batch, uint64_t block) {
    const auto limit = std::numeric_limits<uint64_t>::max();
    if (batch == 0 || block == 0 || batch > limit / block || batches > limit / (batch * block)) {
        throw std::overflow_error("duration-mode byte count overflow");
    }
    return batches * batch * block;
}

inline void validateDurationSeconds(int warmup, int duration) {
    if (warmup < 0 || duration < 0 || warmup > 86400 || duration > 86400 ||
        (warmup != 0 && duration == 0)) {
        throw std::invalid_argument(
            "duration/warmup seconds must be in [0,86400]; timed warmup requires duration");
    }
}

struct MemosDurationStats {
    uint64_t window_start_us = 0, window_end_us = 0, last_completion_us = 0;
    uint64_t warmup_batches = 0, measured_batches = 0, drain_batches = 0;
    uint64_t issued_batches = 0, completed_batches = 0, latency_batches = 0;
    uint64_t peak_batches = 0;
    uint64_t workers = 0, workers_with_completions = 0, workers_with_warmup = 0;

    void add(const MemosDurationStats &other) {
        if (other.window_end_us == 0) return;
        if (window_end_us == 0) {
            window_start_us = other.window_start_us;
            window_end_us = other.window_end_us;
        }
        last_completion_us = std::max(last_completion_us, other.last_completion_us);
        warmup_batches += other.warmup_batches;
        measured_batches += other.measured_batches;
        drain_batches += other.drain_batches;
        issued_batches += other.issued_batches;
        completed_batches += other.completed_batches;
        latency_batches += other.latency_batches;
        workers += other.workers;
        workers_with_completions += other.workers_with_completions;
        workers_with_warmup += other.workers_with_warmup;
        peak_batches += other.peak_batches; // Sum of per-worker API batch peaks.
    }
    uint64_t drainUs() const {
        return last_completion_us > window_end_us ? last_completion_us - window_end_us : 0;
    }
};

// Prepared requests remain live across warmup and measurement. Completion
// timestamps are observation times, not hardware completion timestamps.
// The caller coordinates a common start, owns cleanup on all return paths,
// and supplies a monotonic clock (a deterministic fake clock in tests).
template<typename Post, typename Poll, typename Complete, typename Clock, typename Cancelled>
bool runMemosDurationPipeline(size_t banks, int pipeline_depth, bool reuse_local,
                               uint64_t start_us, uint64_t warmup_us, uint64_t duration_us,
                               Post post, Poll poll, Complete complete, Clock now,
                               Cancelled cancelled, MemosDurationStats &stats) {
    stats = {};
    if (banks == 0 || pipeline_depth <= 0 || static_cast<size_t>(pipeline_depth) > banks ||
        (reuse_local && banks % static_cast<size_t>(pipeline_depth) != 0) || duration_us == 0 ||
        warmup_us > std::numeric_limits<uint64_t>::max() - start_us ||
        duration_us > std::numeric_limits<uint64_t>::max() - start_us - warmup_us) return false;
    const size_t depth = static_cast<size_t>(pipeline_depth);
    stats.workers = 1;
    stats.window_start_us = start_us + warmup_us;
    stats.window_end_us = stats.window_start_us + duration_us;
    std::vector<size_t> active(depth, banks);
    std::vector<bool> busy(banks, false);
    std::vector<uint64_t> posted_at(banks, 0);
    size_t inflight = 0, next_bank = depth % banks;
    auto issue = [&](size_t lane, size_t bank) {
        if (cancelled()) return false;
        const uint64_t t = now();
        if (t >= stats.window_end_us) return true;
        if (busy[bank] || !post(bank)) return false;
        posted_at[bank] = t;
        busy[bank] = true;
        active[lane] = bank;
        ++stats.issued_batches;
        ++inflight;
        stats.peak_batches = std::max(stats.peak_batches, static_cast<uint64_t>(inflight));
        return true;
    };
    for (size_t lane = 0; lane < depth; ++lane) {
        if (!issue(lane, lane)) return false;
    }
    while (inflight != 0) {
        if (cancelled()) return false;
        for (size_t lane = 0; lane < depth; ++lane) {
            const size_t bank = active[lane];
            if (bank == banks) continue;
            const auto status = poll(bank);
            if (status == MemosBatchStatus::pending) continue;
            if (status == MemosBatchStatus::error) return false;
            const uint64_t t = now();
            stats.last_completion_us = std::max(stats.last_completion_us, t);
            const bool measured = t >= stats.window_start_us && t < stats.window_end_us;
            const bool latency_sample = measured && posted_at[bank] >= stats.window_start_us;
            if (t < stats.window_start_us) ++stats.warmup_batches;
            else if (measured) ++stats.measured_batches;
            else ++stats.drain_batches;
            if (latency_sample) ++stats.latency_batches;
            complete(bank, latency_sample);
            ++stats.completed_batches;
            busy[bank] = false;
            active[lane] = banks;
            --inflight;
            if (t >= stats.window_end_us) continue;
            if (reuse_local) {
                next_bank = (bank + depth) % banks;
            } else {
                while (busy[next_bank]) next_bank = (next_bank + 1) % banks;
            }
            if (!issue(lane, next_bank)) return false;
            next_bank = (next_bank + 1) % banks;
        }
    }
    stats.workers_with_completions = stats.measured_batches != 0 ? 1 : 0;
    stats.workers_with_warmup = stats.warmup_batches != 0 ? 1 : 0;
    return stats.issued_batches == stats.completed_batches;
}

} // namespace nixlbench
