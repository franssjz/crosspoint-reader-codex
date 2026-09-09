#pragma once
#include <string>
#include <string_view>
namespace FsHelpers {
inline std::string normalisePath(const std::string& path) { return path; }
inline std::string decodeUriEscapes(const std::string& path) { return path; }
inline bool hasJpgExtension(std::string_view path) { return path.ends_with(".jpg"); }
inline bool hasPngExtension(std::string_view path) { return path.ends_with(".png"); }
}  // namespace FsHelpers
