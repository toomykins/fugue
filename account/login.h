// Local account login record for a non-hub server. Pure (no sockets).
// The identity comes from the pager's cache or from a key.txt block. The install block is minted per session.
// cauthKey is left 0 here; net::Session stamps in the key cached for this (ckey, server) at send time (net/cauth.h).
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include "net/bytes.h"

namespace fugue::account {

struct AccountLogin {
    fugue::Bytes record;    // the 0x1a body (a bearer token: never log or print)
    fugue::Bytes identity;  // len(account)+13 (secret)
    uint32_t installId = 0,
             innerKey = 0;  // per-account hub constants from the key block (0 when from the cache w/o them)
    std::string source;     // "identity cache" | "key.txt" : a label, safe to print
};

// password may be empty when the identity cache holds the account. keytxtPath may be empty (default discovery).
std::optional<AccountLogin> mintLocalAccountLogin(const std::string& account, const std::string& keytxtPath,
                                                  std::string_view password, std::string* err = nullptr);

// Flips one bit of the identity in a login record. The identity is len(account)+13 bytes: the account name in the clear,
// then 13 bytes that are not the name.
//   "name": flip a bit inside the name (yields a different, well-formed account name)
//   "tail": flip a bit inside the 13 non-name bytes
// Same length and framing either way. Any other value returns the record unchanged.
fugue::Bytes fabricateRecord(const fugue::Bytes& record, const std::string& where);

}  // namespace fugue::account
