// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "TestHelpers.h"

using namespace os;
using namespace os::test;

TEST(Document, AddAndRemoveBody)
{
    doc::Document d;
    auto body = std::make_unique<doc::Body>();
    const Uuid id = body->id();
    body->insertFeature(boxFeature(10, 20, 30), 0);
    d.addBody(std::move(body));
    ASSERT_EQ(d.bodies().size(), 1u);
    doc::Body* b = d.body(id);
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(geom::volume(b->shape()), 6000.0, 1e-6);
    EXPECT_EQ(b->state(0).status, doc::FeatureStatus::Ok);

    auto removed = d.removeBody(id);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->id(), id);
    EXPECT_TRUE(d.bodies().empty());
    EXPECT_EQ(d.body(id), nullptr);
}

TEST(Document, RevisionAndListeners)
{
    doc::Document d;
    int calls = 0;
    const int handle = d.addListener([&] { ++calls; });
    const auto r0 = d.revision();
    auto body = std::make_unique<doc::Body>();
    body->insertFeature(boxFeature(1, 1, 1), 0);
    d.addBody(std::move(body));
    EXPECT_GT(d.revision(), r0);
    EXPECT_EQ(calls, 1);
    d.removeListener(handle);
    d.recomputeAll();
    EXPECT_EQ(calls, 1);
}

TEST(Document, UuidsAreStableAcrossRecompute)
{
    doc::Document d;
    auto body = std::make_unique<doc::Body>();
    auto box = boxFeature(20, 20, 20);
    const Uuid featureId = box->id();
    const Uuid bodyId = body->id();
    body->insertFeature(std::move(box), 0);
    d.addBody(std::move(body));
    d.recomputeAll();
    d.recomputeAll();
    ASSERT_NE(d.body(bodyId), nullptr);
    EXPECT_EQ(d.body(bodyId)->features().front()->id(), featureId);
    EXPECT_EQ(d.bodyOfFeature(featureId), d.body(bodyId));
}

TEST(Document, DownstreamFeaturesRecomputeAfterParameterChange)
{
    // Box 20^3 -> push top +15 -> edit box height to 30 -> expect 45.
    doc::Document d;
    auto body = std::make_unique<doc::Body>();
    const Uuid bodyId = body->id();
    auto box = boxFeature(20, 20, 20);
    const Uuid boxId = box->id();
    body->insertFeature(std::move(box), 0);
    d.addBody(std::move(body));

    doc::Body& b = *d.body(bodyId);
    d.insertFeature(bodyId, pushPull(b.shape(), {0, 0, 1}, 15.0));
    EXPECT_NEAR(height(b), 35.0, 1e-6);

    ASSERT_TRUE(b.feature(boxId)->setParameter("height", 30.0).ok());
    d.featureChanged(boxId);
    EXPECT_EQ(b.state(1).status, doc::FeatureStatus::Ok);
    EXPECT_NEAR(height(b), 45.0, 1e-6);
}

TEST(Document, FailingFeatureDoesNotDestroyDocument)
{
    doc::Document d;
    auto body = std::make_unique<doc::Body>();
    const Uuid bodyId = body->id();
    auto box = boxFeature(20, 20, 20);
    const Uuid boxId = box->id();
    body->insertFeature(std::move(box), 0);
    d.addBody(std::move(body));
    doc::Body& b = *d.body(bodyId);

    d.insertFeature(bodyId, filletVertical(b.shape(), 3.0));
    ASSERT_EQ(b.state(1).status, doc::FeatureStatus::Ok);
    d.insertFeature(bodyId, pushPull(b.shape(), {0, 0, 1}, 5.0));
    ASSERT_EQ(b.state(2).status, doc::FeatureStatus::Ok);

    // Shrinking the box makes the 3 mm fillet impossible.
    ASSERT_TRUE(b.feature(boxId)->setParameter("width", 4.0).ok());
    d.featureChanged(boxId);
    EXPECT_EQ(b.state(0).status, doc::FeatureStatus::Ok);
    EXPECT_EQ(b.state(1).status, doc::FeatureStatus::Failed);
    EXPECT_FALSE(b.state(1).userMessage.empty());
    EXPECT_EQ(b.state(2).status, doc::FeatureStatus::NotComputed);
    EXPECT_TRUE(b.hasFailures());
    // The body shows the last good state, and all features are still present.
    EXPECT_EQ(b.features().size(), 3u);
    EXPECT_NEAR(geom::boundingBox(b.shape()).size().x, 4.0, 1e-6);

    // Fixing the parameter heals the history.
    ASSERT_TRUE(b.feature(boxId)->setParameter("width", 20.0).ok());
    d.featureChanged(boxId);
    EXPECT_FALSE(b.hasFailures());
    EXPECT_NEAR(height(b), 25.0, 1e-6);
}

TEST(Document, SuppressedFeaturePassesThrough)
{
    doc::Document d;
    auto body = std::make_unique<doc::Body>();
    const Uuid bodyId = body->id();
    body->insertFeature(boxFeature(20, 20, 20), 0);
    d.addBody(std::move(body));
    doc::Body& b = *d.body(bodyId);
    auto pp = pushPull(b.shape(), {0, 0, 1}, 10.0);
    const Uuid ppId = pp->id();
    d.insertFeature(bodyId, std::move(pp));
    EXPECT_NEAR(height(b), 30.0, 1e-6);
    b.feature(ppId)->setSuppressed(true);
    d.featureChanged(ppId);
    EXPECT_EQ(b.state(1).status, doc::FeatureStatus::Suppressed);
    EXPECT_NEAR(height(b), 20.0, 1e-6);
}

TEST(Document, PreviewDoesNotMutate)
{
    doc::Document d;
    auto body = std::make_unique<doc::Body>();
    const Uuid bodyId = body->id();
    body->insertFeature(boxFeature(20, 20, 20), 0);
    d.addBody(std::move(body));
    const auto rev = d.revision();
    const auto shapeRev = d.body(bodyId)->shapeRevision();
    auto pp = pushPull(d.body(bodyId)->shape(), {0, 0, 1}, 7.0);
    auto preview = d.preview(bodyId, *pp);
    ASSERT_TRUE(preview.ok());
    EXPECT_NEAR(geom::boundingBox(preview.value()).size().z, 27.0, 1e-6);
    EXPECT_EQ(d.revision(), rev);
    EXPECT_EQ(d.body(bodyId)->shapeRevision(), shapeRev);
    EXPECT_NEAR(height(*d.body(bodyId)), 20.0, 1e-6);
}

TEST(Document, BodyNames)
{
    doc::Document d;
    EXPECT_EQ(d.nextBodyName(), "Body 1");
    auto body = std::make_unique<doc::Body>();
    body->setName("Body 1");
    body->insertFeature(boxFeature(1, 1, 1), 0);
    d.addBody(std::move(body));
    EXPECT_EQ(d.nextBodyName(), "Body 2");
}
