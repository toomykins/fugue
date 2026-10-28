// skin/dmf.h - the .dmf (DreamSeeker interface file) parser and typed value accessors.
//
// The format is a flat, tab-indented list of `window|menu|macro "name"` datums holding `elem ["name"]` records of
//   `key = value` rows. Values are "double-quoted with C escapes", 'single-quoted resource refs' or bare tokens.
// Edge rules the format does not state: the header kind is cut at the first space only (so `window<TAB>"x"` is an
//   error); a re-stated key row is last-wins; `none` counts as absent for int, point, size and colour; an unknown escape
//   keeps both characters; text after a closing quote is ignored.
// The sparse-file rule lives in the caller: a missing key means "the default applies" (get() == nullptr).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fugue::skin {

struct KV {
    std::string key, value;
    int line = 0;
};

struct Elem {
    std::string name;
    std::vector<KV> rows;
    int line = 0;
    std::map<std::string, int> idx;  // key -> index into rows (last wins)

    const std::string* get(const std::string& key) const;
    std::string str(const std::string& key, const std::string& def) const;
    int64_t intv(const std::string& key, int64_t def) const;      // `none`/empty/unparseable -> def
    double floatv(const std::string& key, double def) const;      // `none`/empty/unparseable -> def
    bool boolv(const std::string& key, bool def) const;
    bool point(const std::string& key, int64_t& x, int64_t& y) const;   // `none` -> false
    bool size(const std::string& key, int64_t& w, int64_t& h) const;    // `WxH`
    bool color(const std::string& key, uint8_t rgba[4]) const;          // #rrggbb / #rrggbbaa
    std::string type() const;  // upper-cased, trimmed `type`; "" when absent
};

struct Block {
    std::string kind;  // "window" | "menu" | "macro"
    std::string name;
    std::vector<Elem> elems;
    int line = 0;

    const Elem* elem(const std::string& name) const;  // first elem with that name
    const Elem* main() const;                         // first type == MAIN
    std::vector<const Elem*> controls() const;        // every elem except MAIN, file order
};

struct File {
    std::vector<Block> blocks;
    // Lookups by name return the LAST block with that name (a re-declared datum replaces the earlier).
    const Block* window(const std::string& name) const;
    const Block* macro(const std::string& name) const;
};

struct ParseResult {
    bool ok = false;
    int errLine = 0;
    std::string error;
    File file;
};

ParseResult parseDmf(std::string_view src);

// ASCII helpers shared by the skin module.
std::string trimSpace(std::string_view s);
std::string lowerAscii(std::string_view s);
std::string upperAscii(std::string_view s);
bool parseInt64(std::string_view s, int64_t& out);  // optional sign, then digits, no spaces

}  // namespace fugue::skin
