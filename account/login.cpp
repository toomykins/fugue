#include "account/login.h"
#include <fstream>
#include <random>
#include <sstream>
#include "account/desc.h"
#include "account/keyblock.h"
#include "account/records.h"
#include "account/store.h"

namespace fugue::account {

std::optional<AccountLogin> mintLocalAccountLogin(const std::string& account, const std::string& keytxtPath,
                                                  std::string_view password, std::string* err) {
    AccountLogin out;
    std::string e;
    if (auto a = loadAccount(account, &e)) {
        out.identity = a->identity;
        out.installId = a->installId;
        out.innerKey = a->innerKey;
        out.source = "identity cache";
    } else if (!password.empty()) {
        auto path = resolveKeyTxt(keytxtPath, account, &e);
        if (!path) {
            if (err) *err = "no identity cache and " + e;
            return std::nullopt;
        }
        std::ifstream f(*path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        auto line1 = keyBlockLine1(ss.str(), account, &e);
        if (!line1) {
            if (err) *err = "key.txt: " + e;
            return std::nullopt;
        }
        Bytes pw(password.begin(), password.end());
        auto fields = decodeKeyBlock(*line1, account, pwordCryptLevel2(pw), &e);
        if (!fields) {
            if (err) *err = "decode key block: " + e;
            return std::nullopt;
        }
        out.identity = fields->identity;
        out.installId = fields->installId;
        out.innerKey = fields->innerKey;
        out.source = "key.txt";
    } else {
        if (err)
            *err = "no cached identity for " + account + " (" + e +
                   "); run the pager's -login once, or (dev tools only) give the password to decode key.txt";
        return std::nullopt;
    }
    std::random_device rd;
    Bytes seed(17);
    for (auto& b : seed) b = uint8_t(rd());
    auto ib = watermarkedInstallBlock(seed, account, &e);
    if (!ib) {
        if (err) *err = e;
        return std::nullopt;
    }
    // cauthKey 0 is a placeholder; net::Session stamps the cached per-server key in when it sends the record.
    auto rec = mintAccountLoginRecord(out.identity, 0, *ib, 0x16, &e);
    if (!rec) {
        if (err) *err = e;
        return std::nullopt;
    }
    out.record = *rec;
    return out;
}

Bytes fabricateRecord(const Bytes& record, const std::string& where) {
    Bytes r = record;
    if (r.size() < 44) return r;
    size_t nameLen = r.size() - 43;  // record = len(account)+43
    size_t idStart = 2;              // [0:2] zeros, then the identity
    if (where == "name")
        r[idStart + nameLen / 2] ^= 0x01;
    else if (where == "tail")
        r[idStart + nameLen + 3] ^= 0x01;  // inside the 13 non-name bytes
    return r;
}

}  // namespace fugue::account
