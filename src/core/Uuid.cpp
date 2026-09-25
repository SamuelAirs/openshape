#include "core/Uuid.h"

#include <mutex>
#include <random>

namespace os {

namespace {

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

Uuid Uuid::generate()
{
    static std::mutex mutex;
    static std::mt19937_64 engine = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }();

    Uuid id;
    {
        std::lock_guard lock(mutex);
        for (int i = 0; i < 2; ++i) {
            const std::uint64_t value = engine();
            for (int b = 0; b < 8; ++b)
                id.bytes_[i * 8 + b] = static_cast<std::uint8_t>(value >> (b * 8));
        }
    }
    id.bytes_[6] = static_cast<std::uint8_t>((id.bytes_[6] & 0x0F) | 0x40); // version 4
    id.bytes_[8] = static_cast<std::uint8_t>((id.bytes_[8] & 0x3F) | 0x80); // RFC 4122 variant
    return id;
}

std::optional<Uuid> Uuid::parse(std::string_view text)
{
    if (text.size() == 38 && text.front() == '{' && text.back() == '}')
        text = text.substr(1, 36);
    if (text.size() != 36)
        return std::nullopt;

    Uuid id;
    std::size_t byteIndex = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i] != '-')
                return std::nullopt;
            ++i;
            continue;
        }
        const int hi = hexValue(text[i]);
        const int lo = (i + 1 < text.size()) ? hexValue(text[i + 1]) : -1;
        if (hi < 0 || lo < 0 || byteIndex >= 16)
            return std::nullopt;
        id.bytes_[byteIndex++] = static_cast<std::uint8_t>((hi << 4) | lo);
        i += 2;
    }
    if (byteIndex != 16)
        return std::nullopt;
    return id;
}

bool Uuid::isNil() const
{
    for (auto b : bytes_)
        if (b != 0)
            return false;
    return true;
}

std::string Uuid::toString() const
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out.push_back('-');
        out.push_back(digits[bytes_[i] >> 4]);
        out.push_back(digits[bytes_[i] & 0x0F]);
    }
    return out;
}

} // namespace os
