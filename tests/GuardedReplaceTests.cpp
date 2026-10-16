#include "GuardedReplace.h"
#include "PortableTestSupport.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace inkstow::portable;
using namespace inkstow::portable::test;

namespace
{
TEST_CASE("Guarded replacement rechecks retries and refusals")
{
    TempVault temp;
    const fs::path path = temp.mPath / "Note.md";
    write(path, "old");
    std::string error;
    const auto valid = [](std::string*) { return true; };
    int tries = 0;
    const auto lockThenReplace =
        [&](const fs::path& stage, const fs::path& destination, std::string* reason) {
            if (++tries == 1) {
                *reason = "locked";
                return detail::ReplaceResult::Retryable;
            }
            return detail::replaceFile(stage, destination, reason);
        };
    REQUIRE(detail::guardedReplace(path, "old", "new", valid, lockThenReplace, 3, &error));
    REQUIRE(tries == 2);
    REQUIRE(read(path) == "new");
    tries = 0;
    write(path, "old");
    const auto outsideWrite = [&](const fs::path&, const fs::path&, std::string* reason) {
        ++tries;
        write(path, "outside");
        *reason = "locked";
        return detail::ReplaceResult::Retryable;
    };
    const bool outsideSaved =
        detail::guardedReplace(path, "old", "mine", valid, outsideWrite, 3, &error);
    REQUIRE(outsideSaved == false);
    REQUIRE(tries == 1);
    REQUIRE(read(path) == "outside");
    write(path, "old");
    tries = 0;
    const auto alwaysLocked = [&](const fs::path&, const fs::path&, std::string* reason) {
        ++tries;
        *reason = "locked";
        return detail::ReplaceResult::Retryable;
    };
    const bool lockedSaved =
        detail::guardedReplace(path, "old", "mine", valid, alwaysLocked, 3, &error);
    REQUIRE(lockedSaved == false);
    REQUIRE(tries == 3);
    REQUIRE(read(path) == "old");
    tries = 0;
    const auto stageChange = [&](std::string*) {
        if (++tries == 2) write(path, "outside");
        return true;
    };
    const bool stagedSaved =
        detail::guardedReplace(path, "old", "mine", stageChange, lockThenReplace, 3, &error);
    REQUIRE(stagedSaved == false);
    REQUIRE(read(path) == "outside");
    REQUIRE(tries == 2);
    for (const auto& entry : fs::directory_iterator(temp.mPath))
        REQUIRE(entry.path().filename() == "Note.md");
}
} // namespace
