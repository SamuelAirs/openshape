#pragma once

#include "commands/Command.h"
#include "core/Uuid.h"
#include "document/Body.h"
#include "document/Feature.h"

#include <memory>
#include <string>

namespace os::cmd {

// Creates a new body from a base feature (e.g. a box).
class CreateBodyCommand final : public Command {
public:
    CreateBodyCommand(std::string name, std::unique_ptr<doc::Feature> baseFeature);
    std::string label() const override { return "Create " + name_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;
    const Uuid& bodyId() const { return bodyId_; }

private:
    Uuid bodyId_ = Uuid::generate(); // fixed at construction: redo recreates the same identity
    std::string name_;
    std::unique_ptr<doc::Feature> prototype_;
};

// Appends (or inserts) a feature into a body's history. Fails, leaving the
// document untouched, if the feature itself cannot be computed.
class AddFeatureCommand final : public Command {
public:
    AddFeatureCommand(Uuid bodyId, std::unique_ptr<doc::Feature> feature, int index = -1);
    std::string label() const override;
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;
    const Uuid& featureId() const { return prototype_->id(); }

private:
    Uuid bodyId_;
    std::unique_ptr<doc::Feature> prototype_;
    int index_;
};

class DeleteBodyCommand final : public Command {
public:
    explicit DeleteBodyCommand(Uuid bodyId) : bodyId_(bodyId) {}
    std::string label() const override { return "Delete body"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid bodyId_;
    std::unique_ptr<doc::Body> removed_;
    int index_ = -1;
};

class DeleteFeatureCommand final : public Command {
public:
    explicit DeleteFeatureCommand(Uuid featureId) : featureId_(featureId) {}
    std::string label() const override { return "Delete step"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid featureId_;
    Uuid bodyId_;
    std::unique_ptr<doc::Feature> removed_;
    int index_ = -1;
};

// Changes one scalar parameter of a feature. Downstream features recompute.
// With rejectIfFeatureFails, an edit that makes this feature fail is refused
// (used by direct manipulation); otherwise it is kept and the failure shown
// in the history (used by history editing).
class SetParameterCommand final : public Command {
public:
    SetParameterCommand(Uuid featureId, std::string key, double value, bool rejectIfFeatureFails = true);
    std::string label() const override { return "Change " + key_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid featureId_;
    std::string key_;
    double newValue_;
    double oldValue_ = 0;
    bool rejectIfFeatureFails_;
};

class SetBodyVisibilityCommand final : public Command {
public:
    SetBodyVisibilityCommand(Uuid bodyId, bool visible) : bodyId_(bodyId), visible_(visible) {}
    std::string label() const override { return visible_ ? "Show body" : "Hide body"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid bodyId_;
    bool visible_;
    bool previous_ = true;
};

} // namespace os::cmd
