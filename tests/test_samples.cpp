// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The sample projects that come with the app (samples/Samples.h): each one
// builds step by step without a failed step, has the size it is meant to
// have, and the file the app ships (resources/samples/<name>.openshape,
// written by OpenShape --write-samples) opens, rebuilds every step and has
// the same bodies and volumes as a fresh build: a change to the build steps
// without writing the files again fails here.
#include "TestFonts.h"

#include "commands/Command.h"
#include "core/Log.h"
#include "document/Document.h"
#include "document/Feature.h"
#include "geometry/Modeling.h"
#include "io/ProjectFile.h"
#include "samples/Samples.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace os;

namespace {

struct Measured {
    int bodies = 0;
    int shown = 0;
    double volume = 0; // of the shown bodies, mm³
    geom::BoundingBox box;
    int steps = 0;
};

// Every body's every step rebuilt: none may fail.
Measured measure(const doc::Document& document, const std::string& name)
{
    Measured m;
    bool first = true;
    for (const auto& body : document.bodies()) {
        ++m.bodies;
        for (int i = 0; i < int(body->features().size()); ++i) {
            ++m.steps;
            const doc::FeatureState& state = body->state(i);
            EXPECT_EQ(state.status, doc::FeatureStatus::Ok)
                << name << ": " << body->name() << " step " << i << ": " << state.userMessage << " / " << state.developerMessage;
            EXPECT_TRUE(state.note.empty()) << name << ": " << body->name() << " step " << i << ": " << state.note;
        }
        if (!body->isVisible())
            continue;
        ++m.shown;
        EXPECT_EQ(body->shape().solidCount(), 1) << name << ": " << body->name();
        m.volume += geom::volume(body->shape());
        const geom::BoundingBox b = geom::boundingBox(body->shape());
        if (first) {
            m.box = b;
            first = false;
        } else {
            m.box.min = {std::min(m.box.min.x, b.min.x), std::min(m.box.min.y, b.min.y), std::min(m.box.min.z, b.min.z)};
            m.box.max = {std::max(m.box.max.x, b.max.x), std::max(m.box.max.y, b.max.y), std::max(m.box.max.z, b.max.z)};
        }
    }
    return m;
}

std::filesystem::path shippedFile(const std::string& name)
{
    return std::filesystem::path(OPENSHAPE_SOURCE_DIR) / "resources" / "samples" / (name + ".openshape");
}

class Samples : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        ASSERT_FALSE(test::registerTestFont(doc::kTextFontRegular).empty());
        ASSERT_FALSE(test::registerTestBoldFont(doc::kTextFontBold).empty());
    }
};

} // namespace

TEST_F(Samples, EachBuildsWithEveryStepWorking)
{
    ASSERT_EQ(samples::sampleList().size(), 5u);
    for (const samples::SampleInfo& sample : samples::sampleList()) {
        doc::Document document;
        cmd::UndoStack stack;
        const Status built = samples::buildSample(sample.name, document, stack);
        ASSERT_TRUE(built.ok()) << sample.name << ": " << built.developerMessage();
        const Measured m = measure(document, sample.name);
        std::printf("%s: %d bodies (%d shown), %d steps, %.3f mm3, box %.2f..%.2f x %.2f..%.2f x %.2f..%.2f\n",
                    sample.name.c_str(), m.bodies, m.shown, m.steps, m.volume, m.box.min.x, m.box.max.x, m.box.min.y,
                    m.box.max.y, m.box.min.z, m.box.max.z);
        EXPECT_GE(m.steps, 2) << sample.name;
        EXPECT_FALSE(sample.summary.empty());
    }
}

TEST_F(Samples, ShippedFilesMatchAFreshBuild)
{
    for (const samples::SampleInfo& sample : samples::sampleList()) {
        doc::Document fresh;
        cmd::UndoStack stack;
        ASSERT_TRUE(samples::buildSample(sample.name, fresh, stack).ok()) << sample.name;
        const std::filesystem::path file = shippedFile(sample.name);
        ASSERT_TRUE(std::filesystem::exists(file)) << file.string() << ": write it with OpenShape --write-samples resources/samples";
        auto loaded = io::loadProject(file);
        ASSERT_TRUE(loaded.ok()) << sample.name << ": " << loaded.developerMessage();
        loaded.value()->recomputeAll(); // every step from its parameters, not the file's cached shapes
        const Measured a = measure(fresh, sample.name), b = measure(*loaded.value(), sample.name + " (file)");
        EXPECT_EQ(a.bodies, b.bodies) << sample.name;
        EXPECT_EQ(a.shown, b.shown) << sample.name;
        EXPECT_EQ(a.steps, b.steps) << sample.name;
        EXPECT_NEAR(a.volume, b.volume, 1e-6 * a.volume) << sample.name << ": the file is older than the build steps";
        const auto thumbnail = io::readProjectThumbnail(file);
        EXPECT_TRUE(thumbnail.ok() && !thumbnail.value().empty()) << sample.name << ": no thumbnail";
    }
}

// Opening adds the bodies in the order they use each other, whatever their
// order in the file: the lamp shade's Combine uses its Windows body, listed
// after it, and failed on a first pass until that body arrived (Sam's build
// 58 log, "Combine: tool body ... missing"). The bodies keep the file's order.
TEST_F(Samples, OpenWithoutAFailedFirstPass)
{
    for (const samples::SampleInfo& sample : samples::sampleList()) {
        std::vector<std::string> warnings;
        setLogSink([&warnings](LogLevel level, LogCategory category, const std::string& text) {
            if (level >= LogLevel::Warning && category == LogCategory::Document)
                warnings.push_back(text);
        });
        auto loaded = io::loadProject(shippedFile(sample.name));
        setLogSink({});
        ASSERT_TRUE(loaded.ok()) << sample.name << ": " << loaded.developerMessage();
        EXPECT_TRUE(warnings.empty()) << sample.name << ": " << (warnings.empty() ? std::string() : warnings.front());
        doc::Document fresh;
        cmd::UndoStack stack;
        ASSERT_TRUE(samples::buildSample(sample.name, fresh, stack).ok()) << sample.name;
        ASSERT_EQ(fresh.bodies().size(), loaded.value()->bodies().size()) << sample.name;
        for (std::size_t i = 0; i < fresh.bodies().size(); ++i)
            EXPECT_EQ(fresh.bodies()[i]->name(), loaded.value()->bodies()[i]->name()) << sample.name << ": body " << i;
        const Measured m = measure(*loaded.value(), sample.name + " (opened)");
        EXPECT_GT(m.volume, 0) << sample.name;
    }
}
