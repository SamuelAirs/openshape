// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/Body.h"

#include "core/Log.h"
#include "core/Timer.h"

#include <algorithm>

namespace os::doc {

namespace {
std::uint64_t nextShapeRevision()
{
    // Global counter so revisions are unique across bodies and never reused
    // after undo (a restored body gets a fresh revision, forcing re-tessellation).
    static std::uint64_t counter = 0;
    return ++counter;
}
} // namespace

Body::Body(const Body& other)
    : id_(other.id_), name_(other.name_), visible_(other.visible_), states_(other.states_), shape_(other.shape_),
      shapeRevision_(other.shapeRevision_)
{
    features_.reserve(other.features_.size());
    for (const auto& f : other.features_)
        features_.push_back(f->clone());
}

Feature* Body::feature(const Uuid& id) const
{
    for (const auto& f : features_)
        if (f->id() == id)
            return f.get();
    return nullptr;
}

int Body::featureIndex(const Uuid& id) const
{
    for (std::size_t i = 0; i < features_.size(); ++i)
        if (features_[i]->id() == id)
            return static_cast<int>(i);
    return -1;
}

void Body::insertFeature(std::unique_ptr<Feature> feature, int index)
{
    if (index < 0 || index > static_cast<int>(features_.size()))
        index = static_cast<int>(features_.size());
    features_.insert(features_.begin() + index, std::move(feature));
    states_.insert(states_.begin() + index, FeatureState{});
}

std::unique_ptr<Feature> Body::removeFeature(const Uuid& id, int* removedIndex)
{
    const int index = featureIndex(id);
    if (removedIndex)
        *removedIndex = index;
    if (index < 0)
        return nullptr;
    auto feature = std::move(features_[static_cast<std::size_t>(index)]);
    features_.erase(features_.begin() + index);
    states_.erase(states_.begin() + index);
    return feature;
}

geom::Shape Body::shapeBefore(int index) const
{
    for (int i = std::min(index, static_cast<int>(states_.size())) - 1; i >= 0; --i) {
        const auto& s = states_[static_cast<std::size_t>(i)];
        if (s.status == FeatureStatus::Ok || s.status == FeatureStatus::Suppressed)
            return s.output;
        if (s.status == FeatureStatus::Failed || s.status == FeatureStatus::NotComputed)
            break;
    }
    return {};
}

void Body::recompute(int fromIndex, const EvalContext& context)
{
    ScopedTimer timer("Body::recompute");
    fromIndex = std::clamp(fromIndex, 0, static_cast<int>(features_.size()));
    geom::Shape current = shapeBefore(fromIndex);
    bool blocked = false;
    // If an earlier feature is not Ok, everything from here on is blocked,
    // and the body keeps showing the last good shape: the output of the
    // step before the first failure (as a recompute from the start leaves it).
    for (int i = 0; i < fromIndex && !blocked; ++i) {
        const auto& state = states_[static_cast<std::size_t>(i)];
        if (state.status == FeatureStatus::Failed || state.status == FeatureStatus::NotComputed)
            blocked = true;
        else
            current = state.output;
    }

    for (std::size_t i = static_cast<std::size_t>(fromIndex); i < features_.size(); ++i) {
        FeatureState& state = states_[i];
        const Feature& feature = *features_[i];
        state = FeatureState{};
        if (blocked) {
            state.status = FeatureStatus::NotComputed;
            state.userMessage = "Not computed because an earlier step failed.";
            continue;
        }
        if (feature.isSuppressed()) {
            state.status = FeatureStatus::Suppressed;
            state.output = current;
            continue;
        }
        if (!feature.isBaseFeature() && current.isNull()) {
            state.status = FeatureStatus::Failed;
            state.error = ErrorCode::InvalidReference;
            state.userMessage = "This step has no body to work on.";
            state.developerMessage = "feature has null input shape";
            blocked = true;
            continue;
        }
        EvalContext local = context;
        local.body = this;
        local.featureIndex = static_cast<int>(i);
        auto result = feature.compute(current, local);
        if (!result && result.error() == ErrorCode::NoEffect) {
            // A step that no longer changes anything (a cut an upstream edit
            // moved off the body, or one saved by an older version that
            // allowed it) passes its input on with a warning: the steps after
            // it still build. New steps that would do nothing are refused
            // when they are added (AddFeatureCommand checks `error`).
            state.status = FeatureStatus::Ok;
            state.output = current;
            state.error = ErrorCode::NoEffect;
            state.userMessage = result.userMessage();
            state.developerMessage = result.developerMessage();
            state.note = result.userMessage();
            OS_LOG(Info, Document) << "feature " << feature.id().toString() << " (" << toString(feature.kind())
                                   << ") changes nothing: " << result.developerMessage();
        } else if (result) {
            state.status = FeatureStatus::Ok;
            state.output = result.value();
            current = result.value();
            for (const auto& warning : result.warnings())
                state.note += (state.note.empty() ? "" : " ") + warning;
        } else {
            state.status = FeatureStatus::Failed;
            state.error = result.error();
            state.userMessage = result.userMessage();
            state.developerMessage = result.developerMessage();
            OS_LOG(Warning, Document) << "feature " << feature.id().toString() << " (" << toString(feature.kind())
                                      << ") failed: " << result.developerMessage();
            blocked = true;
        }
    }

    if (!current.sameAs(shape_)) {
        shape_ = current;
        shapeRevision_ = nextShapeRevision();
    }
}

int Body::adoptResults(const Body& original)
{
    if (original.states_.size() > states_.size() || original.features_.size() != original.states_.size())
        return 0;
    for (std::size_t i = 0; i < original.features_.size(); ++i)
        if (features_[i]->kind() != original.features_[i]->kind()
            || features_[i]->isSuppressed() != original.features_[i]->isSuppressed())
            return 0; // not a copy of it after all: compute everything
    std::copy(original.states_.begin(), original.states_.end(), states_.begin());
    return static_cast<int>(original.states_.size());
}

bool Body::hasFailures() const
{
    return std::any_of(states_.begin(), states_.end(), [](const FeatureState& s) {
        return s.status == FeatureStatus::Failed;
    });
}

} // namespace os::doc
