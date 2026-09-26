// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace os::test {

// Seeded random numbers that are the same with every standard library.
// std::mt19937's output is fixed by the standard, but the distributions
// (uniform_real_distribution, uniform_int_distribution) and std::shuffle
// are not: libstdc++ (Windows, MSYS2) and libc++ (macOS CI) draw different
// values from the same seed. Seeded tests use this so a seed that fails in
// CI replays the same session on any machine.
class PortableRandom {
public:
    explicit PortableRandom(std::uint32_t seed) : engine_(seed) {}

    std::uint32_t next() { return static_cast<std::uint32_t>(engine_()); }

    // In [0, 1), 53 random bits.
    double unit()
    {
        const std::uint64_t high = next() >> 5, low = next() >> 6; // 27 + 26 bits
        return double((high << 26) | low) / 9007199254740992.0;    // 2^53
    }

    // In [lo, hi).
    double uniform(double lo, double hi) { return lo + (hi - lo) * unit(); }

    // In [0, n), n >= 1, without modulo bias.
    std::uint64_t below(std::uint64_t n)
    {
        if (n <= 1)
            return 0;
        const std::uint64_t range = std::uint64_t(1) << 32;
        if (n > range) // wider than one draw: combine two
            return ((std::uint64_t(next()) << 32) | next()) % n;
        const std::uint64_t limit = range - range % n;
        std::uint64_t x = next();
        while (x >= limit)
            x = next();
        return x % n;
    }

    // In [lo, hi], both included.
    int integer(int lo, int hi) { return lo + int(below(std::uint64_t(std::int64_t(hi) - lo + 1))); }
    std::size_t index(std::size_t lo, std::size_t hi) { return lo + std::size_t(below(std::uint64_t(hi - lo) + 1)); }

    // Fisher-Yates.
    template <typename T>
    void shuffle(std::vector<T>& v)
    {
        for (std::size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[std::size_t(below(i))]);
    }

private:
    std::mt19937 engine_;
};

} // namespace os::test
