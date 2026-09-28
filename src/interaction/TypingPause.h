// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace os::interact {

// A value typed key by key is previewed once typing pauses, not at every
// key: typing "100" over "95" would otherwise show the model at 1 mm, then
// 10 mm, then 100 mm (the owner's report, 2026-09-27). Confirming the value
// (Tab or Next, Enter or the check mark, leaving the field, a tap elsewhere)
// takes it at once. Qt-free; the time is passed in, so tests step a clock.
class TypingPause {
public:
    using Clock = std::chrono::steady_clock;
    // How long typing must pause before the text typed so far is previewed:
    // longer than the gap between keys typed in one go (on a keypad too),
    // short enough that the model follows a slow typist.
    static constexpr std::chrono::milliseconds kPause{700};

    // A key typed: `text` is the whole text typed so far. The pause starts again.
    void type(std::string text, Clock::time_point now)
    {
        text_ = std::move(text);
        deadline_ = now + kPause;
    }
    bool pending() const { return deadline_.has_value(); }
    // When the text typed will be previewed (only while pending()).
    Clock::time_point deadline() const { return deadline_.value_or(Clock::time_point{}); }
    const std::string& text() const { return text_; }
    // The text typed, once the pause is over (nothing is pending then).
    std::optional<std::string> takeIfDue(Clock::time_point now)
    {
        if (!deadline_ || now < *deadline_)
            return std::nullopt;
        return take();
    }
    // The text typed, now (it is confirmed), or nullopt when nothing is pending.
    std::optional<std::string> take()
    {
        if (!deadline_)
            return std::nullopt;
        deadline_.reset();
        return std::exchange(text_, std::string());
    }
    // Forgets the text typed (Esc, the value went away).
    void clear()
    {
        deadline_.reset();
        text_.clear();
    }

private:
    std::string text_;
    std::optional<Clock::time_point> deadline_;
};

} // namespace os::interact
