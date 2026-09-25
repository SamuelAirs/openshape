#include "commands/DocumentCommands.h"

#include "document/Document.h"

namespace os::cmd {

namespace {

Status failureFrom(const doc::FeatureState& state)
{
    return Status::failure(state.error == ErrorCode::None ? ErrorCode::KernelFailure : state.error,
                           state.userMessage.empty() ? "The operation failed." : state.userMessage, state.developerMessage);
}

Status missingBody()
{
    return Status::failure(ErrorCode::InvalidReference, "The body no longer exists.", "command: unknown body");
}

Status missingFeature()
{
    return Status::failure(ErrorCode::InvalidReference, "That step no longer exists.", "command: unknown feature");
}

} // namespace

// ---- CreateBody -----------------------------------------------------------------

CreateBodyCommand::CreateBodyCommand(std::string name, std::unique_ptr<doc::Feature> baseFeature)
    : name_(std::move(name)), prototype_(std::move(baseFeature))
{
}

Status CreateBodyCommand::execute(doc::Document& document)
{
    if (!prototype_ || !prototype_->isBaseFeature())
        return Status::failure(ErrorCode::InvalidArgument, "Unable to create the body.", "CreateBody without base feature");
    auto body = std::make_unique<doc::Body>(bodyId_);
    body->setName(name_);
    body->insertFeature(prototype_->clone(), 0);
    doc::Body& added = document.addBody(std::move(body));
    if (added.state(0).status != doc::FeatureStatus::Ok) {
        Status failure = failureFrom(added.state(0));
        document.removeBody(bodyId_);
        return failure;
    }
    return okStatus();
}

void CreateBodyCommand::undo(doc::Document& document)
{
    document.removeBody(bodyId_);
}

// ---- AddFeature -----------------------------------------------------------------

AddFeatureCommand::AddFeatureCommand(Uuid bodyId, std::unique_ptr<doc::Feature> feature, int index)
    : bodyId_(bodyId), prototype_(std::move(feature)), index_(index)
{
}

std::string AddFeatureCommand::label() const
{
    switch (prototype_->kind()) {
    case doc::FeatureKind::PushPull: return "Push/Pull";
    case doc::FeatureKind::Fillet: return "Fillet";
    case doc::FeatureKind::Chamfer: return "Chamfer";
    case doc::FeatureKind::Box: return "Add box";
    case doc::FeatureKind::Extrude: return "Extrude";
    }
    return "Add step";
}

Status AddFeatureCommand::execute(doc::Document& document)
{
    if (!document.body(bodyId_))
        return missingBody();
    const doc::FeatureState& state = document.insertFeature(bodyId_, prototype_->clone(), index_);
    if (state.status != doc::FeatureStatus::Ok) {
        Status failure = failureFrom(state);
        document.removeFeature(prototype_->id());
        return failure;
    }
    return okStatus();
}

void AddFeatureCommand::undo(doc::Document& document)
{
    document.removeFeature(prototype_->id());
}

// ---- DeleteBody -----------------------------------------------------------------

Status DeleteBodyCommand::execute(doc::Document& document)
{
    removed_ = document.removeBody(bodyId_, &index_);
    return removed_ ? okStatus() : missingBody();
}

void DeleteBodyCommand::undo(doc::Document& document)
{
    if (removed_)
        document.addBody(std::move(removed_), index_);
}

// ---- DeleteFeature --------------------------------------------------------------

Status DeleteFeatureCommand::execute(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    if (!body)
        return missingFeature();
    if (body->featureIndex(featureId_) == 0)
        return Status::failure(ErrorCode::InvalidArgument, "The first step of a body cannot be deleted. Delete the body instead.",
                               "delete base feature");
    bodyId_ = body->id();
    removed_ = document.removeFeature(featureId_, &index_);
    return removed_ ? okStatus() : missingFeature();
}

void DeleteFeatureCommand::undo(doc::Document& document)
{
    if (removed_)
        document.insertFeature(bodyId_, std::move(removed_), index_);
}

// ---- SetParameter ---------------------------------------------------------------

SetParameterCommand::SetParameterCommand(Uuid featureId, std::string key, double value, bool rejectIfFeatureFails)
    : featureId_(featureId), key_(std::move(key)), newValue_(value), rejectIfFeatureFails_(rejectIfFeatureFails)
{
}

Status SetParameterCommand::execute(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    doc::Feature* feature = body ? body->feature(featureId_) : nullptr;
    if (!feature)
        return missingFeature();
    const auto old = feature->parameter(key_);
    if (!old)
        return Status::failure(ErrorCode::InvalidArgument, "This value cannot be edited.", "unknown parameter " + key_);
    oldValue_ = *old;
    if (Status s = feature->setParameter(key_, newValue_); !s)
        return s;
    document.featureChanged(featureId_);

    const doc::FeatureState& state = body->state(body->featureIndex(featureId_));
    if (rejectIfFeatureFails_ && state.status == doc::FeatureStatus::Failed) {
        Status failure = failureFrom(state);
        (void)feature->setParameter(key_, oldValue_);
        document.featureChanged(featureId_);
        return failure;
    }
    return okStatus();
}

void SetParameterCommand::undo(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    doc::Feature* feature = body ? body->feature(featureId_) : nullptr;
    if (!feature)
        return;
    (void)feature->setParameter(key_, oldValue_);
    document.featureChanged(featureId_);
}

// ---- SetBodyVisibility ----------------------------------------------------------

Status SetBodyVisibilityCommand::execute(doc::Document& document)
{
    const doc::Body* body = document.body(bodyId_);
    if (!body)
        return missingBody();
    previous_ = body->isVisible();
    document.setBodyVisible(bodyId_, visible_);
    return okStatus();
}

void SetBodyVisibilityCommand::undo(doc::Document& document)
{
    document.setBodyVisible(bodyId_, previous_);
}

// ---- SetFeatureSuppressed --------------------------------------------------------

Status SetFeatureSuppressedCommand::execute(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    doc::Feature* feature = body ? body->feature(featureId_) : nullptr;
    if (!feature)
        return missingFeature();
    if (feature->isBaseFeature() && suppressed_)
        return Status::failure(ErrorCode::InvalidArgument, "The first step of a body cannot be suppressed.",
                               "suppress base feature");
    previous_ = feature->isSuppressed();
    feature->setSuppressed(suppressed_);
    document.featureChanged(featureId_);
    return okStatus();
}

void SetFeatureSuppressedCommand::undo(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    if (doc::Feature* feature = body ? body->feature(featureId_) : nullptr) {
        feature->setSuppressed(previous_);
        document.featureChanged(featureId_);
    }
}

// ---- Sketches ---------------------------------------------------------------------

Status CreateSketchCommand::execute(doc::Document& document)
{
    if (document.sketch(sketch_.id()))
        return Status::failure(ErrorCode::InvalidArgument, "Unable to create the sketch.", "sketch id already exists");
    document.addSketch(std::make_unique<sketch::Sketch>(sketch_));
    return okStatus();
}

void CreateSketchCommand::undo(doc::Document& document)
{
    document.removeSketch(sketch_.id());
}

Status EditSketchCommand::execute(doc::Document& document)
{
    const sketch::Sketch* current = document.sketch(after_.id());
    if (!current)
        return Status::failure(ErrorCode::InvalidReference, "The sketch no longer exists.", "EditSketch: unknown sketch");
    before_ = *current;
    document.replaceSketch(after_);
    return okStatus();
}

void EditSketchCommand::undo(doc::Document& document)
{
    if (before_)
        document.replaceSketch(*before_);
}

Status DeleteSketchCommand::execute(doc::Document& document)
{
    if (!document.dependentFeatures(sketchId_).empty())
        return Status::failure(ErrorCode::InvalidArgument,
                               "This sketch is used by a 3D feature. Delete that feature first.",
                               "DeleteSketch: sketch has dependents");
    removed_ = document.removeSketch(sketchId_, &index_);
    return removed_ ? okStatus()
                    : Status::failure(ErrorCode::InvalidReference, "The sketch no longer exists.", "DeleteSketch: unknown");
}

void DeleteSketchCommand::undo(doc::Document& document)
{
    if (removed_)
        document.addSketch(std::move(removed_), index_);
}

} // namespace os::cmd
