// Fugue's own version, set once by project(VERSION) in CMakeLists.txt. Unrelated to the BYOND client version Fugue
// reports to servers.
#pragma once

#ifndef FUGUE_VERSION
#error "FUGUE_VERSION comes from project(VERSION ...) in the top-level CMakeLists.txt"
#endif

namespace fugue {

constexpr const char* kVersion = FUGUE_VERSION;                     // "0.5.0"
constexpr const char* kVersionRelease = "fugue-qt " FUGUE_VERSION;  // what a FUGUE_RELEASE build prints and discloses
constexpr const char* kVersionDev = "fugue-qt " FUGUE_VERSION "-dev";

}  // namespace fugue
