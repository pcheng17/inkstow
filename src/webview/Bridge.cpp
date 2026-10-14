#include "Bridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <initializer_list>
#include <string>

namespace inkstow::webview_shell
{
namespace
{
using Json = nlohmann::json;

bool hasFields(const Json& request, std::initializer_list<const char*> fields)
{
    if (request.size() != fields.size()) return false;
    return std::all_of(fields.begin(), fields.end(), [&request](const char* field) {
        return request.contains(field);
    });
}

bool stringField(const Json& request, const char* field, std::size_t maxBytes)
{
    return request.at(field).is_string() &&
           request.at(field).get_ref<const std::string&>().size() <= maxBytes;
}

} // namespace

std::string Bridge::handle(const std::string& arguments)
{
    Json reply;
    bool success = false;
    std::string error;
    try {
        if (arguments.size() > std::size_t{8} * 1024 * 1024) {
            error = "Request exceeds 8 MiB.";
        } else {
            const Json args = Json::parse(arguments);
            if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
                !args[0].contains("operation") || !args[0]["operation"].is_string())
            {
                error = "Expected one request object with an operation string.";
            } else {
                const Json& request = args[0];
                const std::string operation = request.at("operation").get<std::string>();
                if (operation == "open" && hasFields(request, {"operation", "folder"}) &&
                    stringField(request, "folder", 4096))
                {
                    success = mSession.openVault(request.at("folder").get<std::string>(), &error);
                } else if (operation == "list" && hasFields(request, {"operation"})) {
                    success = true;
                } else if (
                    operation == "select" && hasFields(request, {"operation", "path"}) &&
                    stringField(request, "path", 4096))
                {
                    success = mSession.selectNote(request.at("path").get<std::string>(), &error);
                } else if (
                    operation == "edit" && hasFields(request, {"operation", "content"}) &&
                    stringField(request, "content", std::size_t{4} * 1024 * 1024))
                {
                    if (mSession.getPath().empty()) {
                        error = "No note selected.";
                    } else {
                        mSession.setContent(request.at("content").get<std::string>());
                        success = true;
                    }
                } else if (operation == "save" && hasFields(request, {"operation"})) {
                    success = mSession.save(&error);
                } else {
                    error = "Unknown operation or invalid request fields.";
                }
            }
        }
        reply = {
            {     "ok",               success},
            {  "notes",   mSession.getNotes()},
            {   "path",    mSession.getPath()},
            {"content", mSession.getContent()},
            {  "dirty",    mSession.isDirty()}
        };
        if (!success) reply["error"] = error;
        return reply.dump();
    } catch (const std::exception&) {
        // Includes malformed JSON and invalid UTF-8; never expose raw parser exceptions.
        return R"({"ok":false,"error":"Invalid JSON or UTF-8 in request or response."})";
    }
}

} // namespace inkstow::webview_shell
