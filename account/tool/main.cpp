// fugue-account: command-line helper for the pager's on-disk formats. Each command prints one line of compact JSON on
// stdout (keys sorted by Qt); exit 0 on success, exit 1 with {"error":...} on failure.
// Secrets (passwords, identity hex) are read from stdin as JSON, never argv.
// Semantics are those of ../store.h, ../keyblock.h, ../records.h and ../desc.h.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "account/desc.h"
#include "account/keyblock.h"
#include "account/records.h"
#include "account/store.h"

using namespace fugue;
using namespace fugue::account;

namespace {
int out(const QJsonObject& o, int rc = 0) {
    std::string s = QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    return rc;
}
int fail(const std::string& e) { return out(QJsonObject{{"error", QString::fromStdString(e)}}, 1); }
QString Q(const std::string& s) { return QString::fromStdString(s); }
QJsonObject readStdin() {
    QFile f;
    if (!f.open(stdin, QIODevice::ReadOnly)) std::exit(fail("cannot open stdin: " + f.errorString().toStdString()));
    auto d = QJsonDocument::fromJson(f.readAll());
    return d.object();
}
Bytes hx(const QJsonValue& v) {
    auto b = hexDecode(v.toString().toStdString());
    return b ? *b : Bytes();
}
uint32_t u32(const QJsonValue& v) { return uint32_t(v.toDouble()); }
std::string S(const QJsonValue& v) { return v.toString().toStdString(); }
QJsonObject tsJson(const Timestamp& t) { return QJsonObject{{"sec", double(t.sec)}, {"nsec", t.nsec}}; }
Timestamp tsFrom(const QJsonValue& v) {
    Timestamp t;
    if (v.isObject()) {
        t.sec = int64_t(v.toObject().value("sec").toDouble());
        t.nsec = int32_t(v.toObject().value("nsec").toInt());
    }
    return t;
}
QJsonObject configView(const Config& c, bool withExtra) {
    QJsonArray sv;
    for (const auto& s : c.servers) {
        QJsonArray fl;
        for (const auto& f : s.flags) fl.append(Q(f));
        QJsonObject o{{"name", Q(s.name)},
                      {"addr", Q(s.addr)},
                      {"hub", s.hub},
                      {"flags", fl},
                      {"last_played", tsJson(s.lastPlayed)},
                      {"last_prefetch", tsJson(s.lastPrefetch)}};
        if (withExtra && !s.extra.isEmpty()) o["extra"] = s.extra;
        sv.append(o);
    }
    QJsonObject o{{"account", Q(c.account)}, {"keytxt", Q(c.keytxt)}, {"client", Q(c.client)}, {"servers", sv}};
    if (withExtra && !c.extra.isEmpty()) o["extra"] = c.extra;
    return o;
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.empty()) return fail("usage: fugue-account <command> [args]; see account/tool/main.cpp");
    const std::string& cmd = a[0];
    std::string err;
    bool withExtra = false;
    for (auto& x : a)
        if (x == "--with-extra") withExtra = true;

    // ---------------- pager files ----------------
    if (cmd == "config-view") {
        auto c = loadConfig(&err);
        if (!c) return fail(err);
        return out(configView(*c, withExtra));
    }
    if (cmd == "config-write") {  // stdin: the view shape
        QJsonObject in = readStdin();
        Config c;
        c.account = S(in["account"]);
        c.keytxt = S(in["keytxt"]);
        c.client = S(in["client"]);
        for (auto v : in["servers"].toArray()) {
            QJsonObject o = v.toObject();
            Server s;
            s.name = S(o["name"]);
            s.addr = S(o["addr"]);
            s.hub = o["hub"].toBool();
            for (auto f : o["flags"].toArray()) s.flags.push_back(S(f));
            s.lastPlayed = tsFrom(o["last_played"]);
            s.lastPrefetch = tsFrom(o["last_prefetch"]);
            c.servers.push_back(std::move(s));
        }
        if (!saveConfig(c, &err)) return fail(err);
        return out({{"ok", true}});
    }
    if (cmd == "config-edit" && a.size() >= 2) {  // load, mutate, save
        auto c = loadConfig(&err);
        if (!c) return fail(err);
        const std::string& op = a[1];
        if (op == "remember" && a.size() >= 3) {
            Server s;
            s.addr = a[2];
            if (a.size() >= 4 && a[3] != "--with-extra") s.name = a[3];
            c->remember(s);
        } else if (op == "mark-prefetched" && a.size() >= 3) {
            Server s;
            s.addr = a[2];
            c->markPrefetched(s);
        } else if (op == "remove" && a.size() >= 3)
            c->remove(a[2]);
        else if (op == "clear-prefetched")
            c->clearPrefetched();
        else if (op == "set-account" && a.size() >= 3)
            c->account = a[2];
        else if (op == "noop") {
        } else
            return fail("config-edit: unknown op");
        if (!saveConfig(*c, &err)) return fail(err);
        return out({{"ok", true}});
    }
    if (cmd == "config-path") {
        auto p = configPath(&err);
        return p.empty() ? fail(err) : out({{"path", Q(p)}});
    }
    if (cmd == "identity-view" && a.size() >= 2) {
        auto acc = loadAccount(a[1], &err);
        if (!acc) return fail(err);
        return out({{"identity_hex", Q(hexEncode(acc->identity))},
                    {"install_id", double(acc->installId)},
                    {"inner_key", double(acc->innerKey)}});
    }
    if (cmd == "identity-write" && a.size() >= 2) {  // stdin {identity_hex, install_id, inner_key}
        QJsonObject in = readStdin();
        Account acc{hx(in["identity_hex"]), u32(in["install_id"]), u32(in["inner_key"])};
        if (!saveAccount(a[1], acc, &err)) return fail(err);
        auto p = identityPath(a[1]);
        return out({{"path", Q(p)}});
    }
    if (cmd == "identity-path" && a.size() >= 2) return out({{"path", Q(identityPath(a[1]))}});
    if (cmd == "sid") {
        auto s = pagerSid(&err);
        return s ? out({{"sid", Q(*s)}}) : fail(err);
    }
    if (cmd == "install-sid") {
        auto s = installSid(&err);
        return s ? out({{"sid", Q(*s)}}) : fail(err);
    }
    if (cmd == "keytxt-resolve") {
        std::string ex, ac;
        for (size_t i = 1; i + 1 < a.size(); i++) {
            if (a[i] == "--explicit") ex = a[i + 1];
            if (a[i] == "--account") ac = a[i + 1];
        }
        auto p = resolveKeyTxt(ex, ac, &err);
        return p ? out({{"path", Q(*p)}}) : fail(err);
    }
    if (cmd == "keytxt-candidates") {
        QJsonArray arr;
        for (auto& c : keyTxtCandidates(a.size() >= 2 && a[1] != "--with-extra" ? a[1] : "")) arr.append(Q(c));
        return out({{"candidates", arr}});
    }
    if (cmd == "keynames" && a.size() >= 2) {  // names only
        QFile f(Q(a[1]));
        if (!f.open(QIODevice::ReadOnly)) return fail("cannot open " + a[1]);
        QJsonArray arr;
        for (auto& n : keyNames(f.readAll().toStdString())) arr.append(Q(n));
        return out({{"names", arr}});
    }
    if (cmd == "keytxt-with-block") {
        QJsonObject in = readStdin();
        return out({{"result", Q(keyTxtWithBlock(S(in["existing"]), S(in["block"])))}});
    }

    // ---------------- key.txt blocks ----------------
    if (cmd == "pword-levels") {  // stdin {password_hex}
        Bytes pw = hx(readStdin()["password_hex"]);
        return out({{"level1", Q(pwordCrypt(pw))},
                    {"level2", Q(pwordCryptLevel2(pw))},
                    {"credential", Q(downloadKeyCredential(pw))}});
    }
    if (cmd == "decode-block") {  // stdin {keytxt|line1, account, password_hex}
        QJsonObject in = readStdin();
        std::string account = S(in["account"]);
        std::string line1;
        if (in.contains("line1"))
            line1 = S(in["line1"]);
        else {
            auto l = keyBlockLine1(S(in["keytxt"]), account, &err);
            if (!l) return fail(err);
            line1 = *l;
        }
        auto f = decodeKeyBlock(line1, account, pwordCryptLevel2(hx(in["password_hex"])), &err);
        if (!f) return fail(err);
        return out({{"identity_hex", Q(hexEncode(f->identity))},
                    {"install_id", double(f->installId)},
                    {"inner_key", double(f->innerKey)},
                    {"have_hub_ids", f->haveHubIds}});
    }
    if (cmd == "encode-block") {  // stdin {identity_hex, install_id, inner_key, tail3_hex, password_hex, seed?}
        QJsonObject in = readStdin();
        Bytes blob =
            buildKeyBlob(hx(in["identity_hex"]), u32(in["install_id"]), u32(in["inner_key"]), hx(in["tail3_hex"]));
        std::string seed = in.contains("seed") ? S(in["seed"]) : "xq";
        return out({{"line1", Q(encodeKeyLine1(blob, pwordCryptLevel2(hx(in["password_hex"])), seed))}});
    }
    if (cmd == "download-key-request") {
        QJsonObject in = readStdin();
        Bytes b = buildDownloadKeyRequest(S(in["username"]), S(in["domain"]), S(in["cred1"]), S(in["crypt"]),
                                          S(in["install_id"]), u32(in["nonce"]));
        return out({{"body_hex", Q(hexEncode(b))}});
    }
    if (cmd == "download-key-reply") {
        QJsonObject in = readStdin();
        auto blk = parseDownloadKeyReply(hx(in["reply_body_hex"]), u32(in["nonce"]), &err);
        return blk ? out({{"block", Q(*blk)}}) : fail(err);
    }

    // ---------------- login record ----------------
    if (cmd == "login-record") {  // stdin {identity_hex, cauth_key, install_block_hex, marker, watermark_key?}
        QJsonObject in = readStdin();
        Bytes ib = hx(in["install_block_hex"]);
        if (in.contains("watermark_key")) {
            auto w = watermarkedInstallBlock(ib, S(in["watermark_key"]), &err);
            if (!w) return fail(err);
            ib = *w;
        }
        auto r = mintAccountLoginRecord(hx(in["identity_hex"]), u32(in["cauth_key"]), ib, uint8_t(in["marker"].toInt()),
                                        &err);
        return r ? out({{"record_hex", Q(hexEncode(*r))}, {"install_block_hex", Q(hexEncode(ib))}}) : fail(err);
    }
    if (cmd == "rekey") {
        QJsonObject in = readStdin();
        std::string c = S(in["cert_str"]);
        uint32_t seed = u32(in["seed"]);
        return out({{"rekey", double(rekey(c, seed))}, {"rekeyed_login_key", double(rekeyedLoginKey(c, seed))}});
    }
    if (cmd == "challenge-answer") {
        QJsonObject in = readStdin();
        auto r = mintChallengeAnswer(hx(in["challenge_hex"]), S(in["account_id"]), u32(in["install_id"]),
                                     u32(in["inner_key"]), hx(in["pad_hex"]), &err);
        if (!r) return fail(err);
        return out(
            {{"answer_hex", Q(hexEncode(r->answer))}, {"sigma_pad", double(r->sigmaPad)}, {"inner_sum", r->innerSum}});
    }
    if (cmd == "cid-digest") {
        QJsonObject in = readStdin();
        auto d = digestForCkey(S(in["key"]));
        return d ? out({{"digest_hex", Q(hexEncode(*d))}, {"cid", double(cidFromDigest(*d))}}) : fail("no digest");
    }
    if (cmd == "cert-str") {
        QJsonObject in = readStdin();
        return out({{"cert_str", Q(certStrFrom(u32(in["r1"]), u32(in["r2"]), u32(in["r3"])))},
                    {"random_ok", generateCertStr().size() == 26}});
    }
    return fail("unknown command: " + cmd);
}
