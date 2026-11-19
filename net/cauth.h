// The login-certificate cache, kept at <stateDir>/cauth.txt in the stock client's cfg/cauth.txt format (config set 10).
// One line per endpoint: <ckey>@<host>:<port> key=<%x>;pwd=03<24 hex>;ctime=<%llx>
// `key` is the client-minted u32 that login records carry; `pwd` is the certStr minted with it.
// The cache is engaged only when the server's Handshake reply sends flag B = 1.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace fugue::net {

struct CauthEntry {
    uint32_t key = 0;
    std::string pwd;     // the certStr ("03" + 24 hex) minted with the key
    uint64_t ctime = 0;  // seconds since the epoch at the last write
};

// <stateDir>/cauth.txt
std::string cauthPath(const std::string& stateDir);
// Entry name: ckey(account) + "@" + url-encoded host [+ ":" + url-encoded port when `port` is non-empty]. Guests use
// "guest". fugue-qt always dials host:port (pager::parseTarget refuses a portless address), so its names carry a port.
std::string cauthEntryName(std::string_view account, std::string_view host, std::string_view port);
std::string cauthEntryName(std::string_view account, std::string_view host, uint16_t port);

// Cert state for ONE connection. `now` = 0 means the wall clock. A cache file that cannot be written still yields a
// usable key/certStr for this connection; *err says why.
class CauthCache {
public:
    CauthCache(std::string stateDir, std::string name, uint64_t now = 0);
    // The Login key. engaged = the server's Handshake reply flag B.
    uint32_t loginKey(bool engaged, std::string* err = nullptr);
    // The cauthKey and certStr of the hub request that answers a CertChallenge.
    void certRequest(uint32_t* key, std::string* certStr, std::string* err = nullptr);
    // The certStr the CertAck re-keys with.
    const std::string& certStr() const { return pwd_; }
    uint32_t key() const { return key_; }

private:
    void mint(bool keepKey, std::string* err);
    std::string dir_, name_;
    uint64_t now_;
    uint32_t key_ = 0;
    std::string pwd_;
    bool minted_ = false;
};

// The Login key with the cache engaged.
uint32_t cauthKeyFor(const std::string& stateDir, const std::string& name, std::string* err = nullptr,
                     uint64_t now = 0);
// Read-only lookup (tests, diagnostics). false when absent.
bool cauthLookup(const std::string& stateDir, const std::string& name, CauthEntry* out);

constexpr uint64_t kCauthMaxAge = 0x278d00;  // 30 days in seconds: the prune bound

}  // namespace fugue::net
