#include "Bridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <exception>
#include <initializer_list>
#include <string>
#include <string_view>

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

const char* changeName(portable::DiskChange change)
{
    switch (change) {
    case portable::DiskChange::Conflict:
        return "conflict";
    case portable::DiskChange::Missing:
        return "missing";
    case portable::DiskChange::Reloaded:
        return "reloaded";
    case portable::DiskChange::None:
        return "none";
    }
    return "none";
}

bool validExternalUrl(std::string_view url)
{
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos ||
        (url.substr(0, schemeEnd) != "http" && url.substr(0, schemeEnd) != "https"))
        return false;
    const auto hostStart = schemeEnd + 3;
    const auto hostEnd = url.find_first_of("/?#", hostStart);
    const auto authority = url.substr(hostStart, hostEnd - hostStart);
    const auto portStart = authority.find(':');
    const auto host = authority.substr(0, portStart);
    if (host.empty() || host.size() > 253 || authority.find('@') != std::string_view::npos)
        return false;
    if (portStart != std::string_view::npos) {
        const auto port = authority.substr(portStart + 1);
        unsigned number = 0;
        const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), number);
        if (port.empty() || error != std::errc{} || end != port.data() + port.size() ||
            number == 0 || number > 65535)
            return false;
    }
    for (std::size_t start = 0; start < host.size();) {
        const auto end = host.find('.', start);
        const auto label = host.substr(start, end == std::string_view::npos ? end : end - start);
        if (label.empty() || label.size() > 63 ||
            !std::isalnum(static_cast<unsigned char>(label.front())) ||
            !std::isalnum(static_cast<unsigned char>(label.back())) ||
            !std::all_of(label.begin(), label.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '-';
            }))
            return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return std::none_of(url.begin(), url.end(), [](unsigned char ch) {
        return ch <= 0x20 || ch == 0x7f || ch == '\\';
    });
}

bool stringField(const Json& request, const char* field, std::size_t maxBytes)
{
    return request.at(field).is_string() &&
           request.at(field).get_ref<const std::string&>().size() <= maxBytes;
}

} // namespace

Bridge::Bridge(
    ShellServices* services,
    OpenAttachment openAttachment,
    portable::Vault::TrashFunction trash,
    OpenExternal openExternal)
    : mServices(services)
    , mOpenAttachment(std::move(openAttachment))
    , mOpenExternal(std::move(openExternal))
    , mTrash(std::move(trash))
{
}

Bridge::Bridge(
    OpenAttachment openAttachment, portable::Vault::TrashFunction trash, OpenExternal openExternal)
    : Bridge(nullptr, std::move(openAttachment), std::move(trash), std::move(openExternal))
{
}

void Bridge::syncWithDisk(std::string* error)
{
    mChange = mSession.syncWithDisk(error);
    if (mChange == portable::DiskChange::Missing && mDismissedMissing && !mSession.isDirty()) {
        mChange = portable::DiskChange::None;
    } else if (mChange != portable::DiskChange::Missing || mSession.isDirty()) {
        mDismissedMissing = false;
    }
}

void Bridge::finishVaultOpen(const std::string& folder, bool remember)
{
    mChange = portable::DiskChange::None;
    mDismissedMissing = false;
    if (!mServices) return;
    if (remember) mServices->setLastVault(folder);
    mServices->watchVault(folder);
}

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
                if (operation == "startup" && hasFields(request, {"operation"}) && mServices) {
                    const std::string folder = mServices->lastVault();
                    if (folder.empty()) {
                        success = true;
                    } else {
                        success = mSession.openVault(folder, &error);
                        if (success) finishVaultOpen(folder, false);
                        if (!success)
                            error = "Could not reopen last vault (" + folder + "): " + error;
                    }
                } else if (
                    operation == "pickVault" && hasFields(request, {"operation"}) && mServices)
                {
                    const std::string folder =
                        mSession.isDirty() ? std::string{} : mServices->pickVault();
                    if (mSession.isDirty()) {
                        error = "Save the current note before opening another vault.";
                    } else if (folder.empty()) {
                        error = "Folder selection cancelled.";
                    } else {
                        success = mSession.openVault(folder, &error);
                        if (success) finishVaultOpen(folder, true);
                    }
                } else if (
                    operation == "open" && hasFields(request, {"operation", "folder"}) &&
                    stringField(request, "folder", 4096))
                {
                    success = mSession.openVault(request.at("folder").get<std::string>(), &error);
                    if (success) finishVaultOpen(request.at("folder").get<std::string>(), true);
                } else if (
                    operation == "follow" && hasFields(request, {"operation", "link"}) &&
                    stringField(request, "link", 4096))
                {
                    const auto link = request.at("link").get<std::string>();
                    success = mSession.followWikilink(link, &error);
                    if (!success && error == "Link does not point to a supported Markdown note." &&
                        mServices)
                    {
                        const auto path = mSession.followAttachment(link, &error);
                        if (path) {
                            success = mServices->openAttachment(*path);
                            if (!success)
                                error = "Could not open attachment in the default application.";
                        }
                    }
                    if (success) {
                        mChange = portable::DiskChange::None;
                        if (mServices) mServices->watchNote(mSession.getPath());
                    }
                } else if (operation == "list" && hasFields(request, {"operation"})) {
                    success = true;
                } else if (
                    operation == "select" && hasFields(request, {"operation", "path"}) &&
                    stringField(request, "path", 4096))
                {
                    success = mSession.selectNote(request.at("path").get<std::string>(), &error);
                    if (success) {
                        mChange = portable::DiskChange::None;
                        mDismissedMissing = false;
                        if (mServices) mServices->watchNote(mSession.getPath());
                    }
                } else if (
                    operation == "edit" && hasFields(request, {"operation", "content"}) &&
                    stringField(request, "content", std::size_t{4} * 1024 * 1024))
                {
                    if (mSession.getPath().empty()) {
                        error = "No note selected.";
                    } else {
                        mSession.setContent(request.at("content").get<std::string>());
                        if (mSession.isDirty()) mDismissedMissing = false;
                        success = true;
                    }
                } else if (
                    operation == "create" && hasFields(request, {"operation", "path"}) &&
                    stringField(request, "path", 4096))
                {
                    success = mSession.createNote(request.at("path").get<std::string>(), &error);
                    if (success) mChange = portable::DiskChange::None;
                } else if (
                    operation == "move" &&
                    hasFields(request, {"operation", "path", "updateLinks"}) &&
                    stringField(request, "path", 4096) && request.at("updateLinks").is_boolean())
                {
                    success = mSession.moveNote(
                        request.at("path").get<std::string>(),
                        request.at("updateLinks").get<bool>(),
                        &error);
                    if (success) mChange = portable::DiskChange::None;
                } else if (operation == "trash" && hasFields(request, {"operation"})) {
                    success =
                        mTrash ? mSession.trashNote(mTrash, &error) : mSession.trashNote(&error);
                    if (success) mChange = portable::DiskChange::None;
                } else if (
                    operation == "link" && hasFields(request, {"operation", "link"}) &&
                    stringField(request, "link", 4096))
                {
                    std::optional<std::filesystem::path> attachment;
                    success = mSession.followLink(
                        request.at("link").get<std::string>(),
                        &attachment,
                        &error);
                    if (success && attachment) {
                        if (mOpenAttachment)
                            success = mOpenAttachment(*attachment, &error);
                        else {
                            success = false;
                            error = "Attachment shell service is unavailable.";
                        }
                    }
                    if (success && !attachment) mChange = portable::DiskChange::None;
                } else if (
                    operation == "attachment" && hasFields(request, {"operation", "link"}) &&
                    stringField(request, "link", 4096))
                {
                    const auto path =
                        mSession.followAttachment(request.at("link").get<std::string>(), &error);
                    if (path) {
                        if (mOpenAttachment)
                            success = mOpenAttachment(*path, &error);
                        else if (mServices)
                            success = mServices->openAttachment(*path);
                        else
                            error = "Attachment shell service is unavailable.";
                    }
                } else if (
                    operation == "external" && hasFields(request, {"operation", "url"}) &&
                    stringField(request, "url", 4096))
                {
                    const auto& url = request.at("url").get_ref<const std::string&>();
                    if (mSession.getPath().empty() || mSession.isDirty()) {
                        error = "Select and save a note before following a link.";
                    } else if (!validExternalUrl(url)) {
                        error = "Invalid external URL.";
                    } else if (mOpenExternal) {
                        success = mOpenExternal(url, &error);
                    } else {
                        error = "External URL shell service is unavailable.";
                    }
                } else if (operation == "refresh" && hasFields(request, {"operation"})) {
                    // A folder event or focus refresh updates discovery as well as the selected
                    // note.
                    success = !mSession.hasVault() ? true : mSession.refreshNotes(&error);
                    if (success) syncWithDisk(&error);
                } else if (operation == "save" && hasFields(request, {"operation"})) {
                    success = mSession.save(&error);
                    if (success) {
                        mChange = portable::DiskChange::None;
                        mDismissedMissing = false;
                    } else {
                        std::string ignored;
                        syncWithDisk(&ignored);
                    }
                } else if (
                    operation == "reload" && hasFields(request, {"operation"}) &&
                    mChange != portable::DiskChange::None)
                {
                    success = mSession.reloadFromDisk(&error);
                    if (success) mChange = portable::DiskChange::None;
                } else if (
                    operation == "keepMine" && hasFields(request, {"operation"}) &&
                    mChange == portable::DiskChange::Conflict)
                {
                    success = mSession.overwriteDisk(&error);
                    if (success) mChange = portable::DiskChange::None;
                } else if (
                    operation == "merge" && hasFields(request, {"operation"}) &&
                    mChange == portable::DiskChange::Conflict)
                {
                    success = mSession.mergeOutsideChanges(&error);
                    if (success) mChange = portable::DiskChange::None;
                } else if (
                    operation == "saveAsNew" && hasFields(request, {"operation"}) &&
                    mChange == portable::DiskChange::Missing)
                {
                    success = mSession.saveAsNewNote(&error);
                    if (success) mChange = portable::DiskChange::None;
                } else if (
                    operation == "discard" && hasFields(request, {"operation"}) &&
                    mChange == portable::DiskChange::Missing)
                {
                    mSession.discardEdits();
                    mChange = portable::DiskChange::None;
                    mDismissedMissing = true;
                    success = true;
                } else {
                    error = "Unknown operation or invalid request fields.";
                }
            }
        }
        reply = {
            {              "ok",                        success},
            {           "notes",            mSession.getNotes()},
            {            "path",             mSession.getPath()},
            {         "content",          mSession.getContent()},
            {           "dirty",             mSession.isDirty()},
            {          "change",            changeName(mChange)},
            {"conflictRevision", mSession.getConflictRevision()}
        };
        if (!error.empty()) reply["error"] = error;
        return reply.dump();
    } catch (const std::exception&) {
        // Includes malformed JSON and invalid UTF-8; never expose raw parser exceptions.
        return R"({"ok":false,"error":"Invalid JSON or UTF-8 in request or response."})";
    }
}

} // namespace inkstow::webview_shell
