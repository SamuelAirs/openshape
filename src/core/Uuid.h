// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace os {

// RFC 4122 version-4 UUID. Used as the persistent identity of every
// document object. Never derived from memory addresses or kernel handles.
class Uuid {
public:
    Uuid() = default; // nil UUID

    static Uuid generate();
    static std::optional<Uuid> parse(std::string_view text);

    bool isNil() const;
    std::string toString() const; // lowercase 8-4-4-4-12 form

    const std::array<std::uint8_t, 16>& bytes() const { return bytes_; }

    friend bool operator==(const Uuid&, const Uuid&) = default;
    friend auto operator<=>(const Uuid&, const Uuid&) = default;

private:
    std::array<std::uint8_t, 16> bytes_{};
};

} // namespace os

template <>
struct std::hash<os::Uuid> {
    std::size_t operator()(const os::Uuid& id) const noexcept
    {
        std::size_t h = 1469598103934665603ull;
        for (auto b : id.bytes()) {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }
};
