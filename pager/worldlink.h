// The second client a clicked byond:// link opens (a pushed link() joins in the same window, app/sessionworker.cpp). Its
// command line is this run's with only the target replaced, so it plays as the same account with the same identity file,
// state dir and options.
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace fugue::pager {

// args, hostTaken: as for parseDsArgv. Returns the arguments after argv[0], led by the `client` subcommand unless
// asDreamSeeker. nullopt with *err when url is not a host:port address (parseTarget refuses hub world names) or the
// new run would not play as this one does.
std::optional<std::vector<std::string>> worldLinkArgs(const std::vector<std::string>& args,
                                                      const std::vector<bool>& hostTaken, bool asDreamSeeker,
                                                      const std::string& url, std::string* err = nullptr);

}  // namespace fugue::pager
