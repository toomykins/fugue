// UTF-8 text helpers for the pager module (no Qt). Trim, field-split, quoting and width rules are Unicode-aware.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fugue::pager {

// Decode one rune at s[i]; invalid sequences yield U+FFFD with width 1.
char32_t decodeRune(std::string_view s, size_t i, size_t* width);
void appendRune(std::string& out, char32_t r);
bool isGoSpace(char32_t r);                              // white space: Latin-1 + the Z category + U+0085
std::string trimSpace(std::string_view s);               // trim white space from both ends
std::vector<std::string> fields(std::string_view s);     // split on runs of white space
size_t runeCount(std::string_view s);                    // rune count
std::string padRight(std::string_view s, size_t runes);  // left-justify to N runes
std::string goQuote(
    std::string_view s);  // quoted string with escapes (non-ASCII printable runes kept as-is; see text.cpp)
std::string latin1ToUtf8(std::string_view raw);
bool goAtoi(std::string_view s,
            long long* out);  // integer parse: false on syntax error AND range error; *out clamped on range

}  // namespace fugue::pager
