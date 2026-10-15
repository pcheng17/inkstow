#pragma once

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace inkstow::portable::test
{
inline std::string utf8Path(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

inline std::filesystem::path nativePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline void write(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out);
    REQUIRE(out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
}

inline std::string read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>{in}, {});
}

struct TempVault
{
    std::filesystem::path mPath =
        std::filesystem::temp_directory_path() /
        ("inkstow-portable-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempVault() { REQUIRE(std::filesystem::create_directory(mPath)); }
    TempVault(const TempVault&) = delete;
    TempVault& operator=(const TempVault&) = delete;
    TempVault(TempVault&&) = delete;
    TempVault& operator=(TempVault&&) = delete;
    ~TempVault()
    {
        std::error_code ignored;
        std::filesystem::remove_all(mPath, ignored);
    }
};
} // namespace inkstow::portable::test
