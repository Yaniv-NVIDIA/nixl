/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils/memos_buffer.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        for (size_t batch : {size_t{1}, size_t{16}, size_t{32}}) {
            for (size_t depth : {size_t{1}, size_t{2}, size_t{4}, size_t{32}}) {
                const size_t size = batch * depth * 512 * 1024;
                for (size_t key = 0; key < 8192; ++key) {
                    const auto offset = nixlbench::memosLocalBufferOffset(key, 512 * 1024, batch, depth, size);
                    if (offset != (key % (batch * depth)) * 512 * 1024 || offset + 512 * 1024 > size) {
                        throw std::runtime_error("incorrect buffer mapping");
                    }
                }
            }
        }
        for (size_t size : {size_t{0}, size_t{63}}) {
            try {
                nixlbench::memosLocalBufferOffset(0, 1, 32, 2, size);
                throw std::runtime_error("undersized allocation accepted");
            } catch (const std::invalid_argument &) {}
        }
        std::cout << "MEMOS local buffer tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
