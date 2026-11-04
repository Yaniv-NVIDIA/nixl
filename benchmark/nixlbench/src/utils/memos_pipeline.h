/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace nixlbench {

enum class MemosBatchStatus { pending, complete, error };

// Each bank owns disjoint descriptors and one prepared request. Refill a lane
// when its batch completes, without waiting for the other lanes. The caller
// owns request preparation, timing, and cleanup (including on failure).
template<typename Post, typename Poll, typename Cancelled>
bool
runMemosPipeline(size_t banks,
                 int pipeline_depth,
                 int iterations,
                 Post post,
                 Poll poll,
                 Cancelled cancelled) {
    if (banks == 0 || pipeline_depth <= 0 || iterations < 0 ||
        static_cast<size_t>(pipeline_depth) > banks) {
        return false;
    }
    const size_t depth = static_cast<size_t>(std::min(pipeline_depth, iterations));
    std::vector<size_t> active(depth);
    std::vector<bool> busy(banks, false);
    int issued = 0;
    int completed = 0;
    for (size_t lane = 0; lane < depth; ++lane) {
        if (cancelled() || !post(lane)) {
            return false;
        }
        active[lane] = lane;
        busy[lane] = true;
        ++issued;
    }
    size_t next_bank = depth % banks;
    while (completed < iterations) {
        if (cancelled()) {
            return false;
        }
        for (size_t lane = 0; lane < depth; ++lane) {
            const size_t bank = active[lane];
            if (bank == banks) {
                continue;
            }
            const auto status = poll(bank);
            if (status == MemosBatchStatus::pending) {
                continue;
            }
            if (status == MemosBatchStatus::error) {
                return false;
            }
            busy[bank] = false;
            active[lane] = banks;
            ++completed;
            if (issued == iterations) {
                continue;
            }
            // At least the just-completed bank is free. Never reuse the local
            // buffers of another lane while its transfer is still in flight.
            while (busy[next_bank]) {
                next_bank = (next_bank + 1) % banks;
            }
            if (cancelled() || !post(next_bank)) {
                return false;
            }
            active[lane] = next_bank;
            busy[next_bank] = true;
            next_bank = (next_bank + 1) % banks;
            ++issued;
        }
    }
    return true;
}

} // namespace nixlbench
