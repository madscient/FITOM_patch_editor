#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace fpe {

// Thrown for structurally invalid patch/profile JSON (missing required
// key, wrong type, etc). Carries a human-readable path hint.
class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& msg) : std::runtime_error(msg) {}
};

namespace json_util {

// Reads j[key], returning `def` if the key is absent or null.
// FITOM_X's documented philosophy is that missing patch fields fall back
// to documented defaults rather than erroring (see docs/*-reference.md),
// so every loader in this project should read optional fields this way.
template <typename T>
T getOr(const nlohmann::json& j, const char* key, T def) {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    return it->get<T>();
}

// Reads a required field, throwing JsonError with a useful message if
// absent. Use for keys the format cannot function without (e.g. `prog`).
template <typename T>
T getRequired(const nlohmann::json& j, const char* key, const char* context) {
    if (!j.is_object() || !j.contains(key) || j.at(key).is_null()) {
        throw JsonError(std::string(context) + ": missing required field \"" + key + "\"");
    }
    return j.at(key).get<T>();
}

// Sets j[key] = value only if value != def, to keep authored files terse
// and close to the hand-written examples in the FITOM_X docs. Pass
// alwaysWrite=true (default) to just always write the field, which is
// simpler to reason about for an editor-authored file; kept configurable
// per call site.
template <typename T>
void setField(nlohmann::json& j, const char* key, const T& value) {
    j[key] = value;
}

} // namespace json_util
} // namespace fpe
