#pragma once

#include "core/Math.h"
#include "document/Feature.h"

#include <nlohmann/json.hpp>

#include <optional>

// Defensive JSON conversions. Every reader validates type and shape and
// returns nullopt instead of throwing: project files are untrusted input.
namespace os::doc {

nlohmann::json vecToJson(const Vec3& v);
std::optional<Vec3> vecFromJson(const nlohmann::json& parent, const char* key);
std::optional<Vec3> vecFromJson(const nlohmann::json& value);

nlohmann::json faceRefToJson(const FaceRef& ref);
std::optional<FaceRef> faceRefFromJson(const nlohmann::json& parent, const char* key);

nlohmann::json edgeRefToJson(const EdgeRef& ref);
std::optional<EdgeRef> edgeRefFromJson(const nlohmann::json& value);

std::optional<double> numberFrom(const nlohmann::json& parent, const char* key);
std::optional<int> intFrom(const nlohmann::json& parent, const char* key);

} // namespace os::doc
