// Hub live-game listing (?format=text): a DM-literal text format. No network here: the caller supplies the bytes.
// Known limit: the HTML entity table is a subset of the standard unescaper (the common named entities and all numeric
// forms, including the windows-1252 remap); a rarer named entity is left undecoded.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fugue::pager {

struct LiveWorld {
    std::string url, id;
    long long serverVersion = 0;
    std::string status;  // HTML written by the world's own code: read as text only, never rendered
    std::vector<std::string> players;
    std::string host;

    std::vector<std::string> lines() const;  // status as plain text, one entry per line, tags dropped, entities decoded
    std::string title() const;               // first line, else the url
};

// nullopt + *err when the bytes are not in the hub's text format (no column-0 section line at all).
std::optional<std::vector<LiveWorld>> parseListing(std::string_view raw, std::string* err = nullptr);

std::string htmlUnescape(std::string_view s);
std::string dmString(std::string_view s, std::string_view* rest);
std::vector<std::string> dmStringList(std::string_view s);
// The URL host for the cases that matter here; false when it would fail to parse.
bool urlHost(std::string_view raw, std::string* host);

}  // namespace fugue::pager
