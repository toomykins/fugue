// `fugue-qt client --connect host:port --prefetch [--state-dir d] [--rsc-dir d] [--account K [--identity-file P]]`
// Warm-up pass: connect, log in, pull the world's whole resource list into the shared ~/.fugue/rsc cache, print one
// summary line, and exit. No window and no QApplication. It runs as a separate process before a join (the pager's
// -warm step), so the first real join declares nothing. Handled before QApplication is built, so it needs no display.
#include <QByteArray>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "account/login.h"
#include "account/records.h"
#include "pager/hubcert.h"
#include "net/prefetch.h"
#include "net/session.h"

bool prefetchMain(int argc, char** argv, int* rc) {
    bool prefetch = false;
    for (int i = 1; i < argc; i++)
        if (!std::strcmp(argv[i], "--prefetch")) prefetch = true;
    if (!prefetch) return false;
    fugue::net::SessionConfig cfg;
    cfg.holdEntry = true;  // download only: no C2S::Ready, so the world does not log this connection in
    std::string conn, account, ident;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](std::string& dst) {
            if (i + 1 < argc) dst = argv[++i];
        };
        if (a == "--connect")
            val(conn);
        else if (a == "--state-dir")
            val(cfg.stateDir);
        else if (a == "--rsc-dir")
            val(cfg.rscDir);
        else if (a == "--account")
            val(account);
        else if (a == "--identity-file")
            val(ident);
        else if (a == "--no-pacing")
            cfg.humanPacing = false;
    }
    size_t colon = conn.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        std::fprintf(stderr, "usage: fugue-qt client --connect host:port --prefetch\n");
        *rc = 2;
        return true;
    }
    cfg.host = conn.substr(0, colon);
    cfg.port = uint16_t(std::atoi(conn.c_str() + colon + 1));
    if (!account.empty()) {
        if (!ident.empty()) qputenv("FUGUE_IDENTITY", QByteArray::fromStdString(ident));  // a path, not a secret
        std::string aerr;
        auto al = fugue::account::mintLocalAccountLogin(account, "", "", &aerr);
        if (!al) {
            std::fprintf(stderr, "account login: %s\n", aerr.c_str());
            *rc = 2;
            return true;
        }
        cfg.accountRecord = al->record;
        cfg.accountLabel = fugue::account::ckey(account);
        std::string hwhy;  // hub servers send CertChallenge and serve nothing until it is answered (pager/hubcert.h)
        if (!fugue::pager::wireHubCert(cfg, *al, &hwhy))
            std::fprintf(stderr, "[prefetch] no hub login-cert answer: %s\n", hwhy.c_str());
    }
    fugue::net::Session s(cfg, [](const std::string& m) { std::fprintf(stderr, "[prefetch] %s\n", m.c_str()); });
    std::string err = s.connectAndLogin();
    if (!err.empty()) {
        std::fprintf(stderr, "prefetch: login failed: %s\n", err.c_str());
        *rc = 2;
        return true;
    }
    auto r = fugue::net::runPrefetch(s, std::chrono::seconds(30), std::chrono::seconds(90),
                                     [](const std::string& m) { std::fprintf(stderr, "[prefetch] %s\n", m.c_str()); });
    std::printf(
        "PREFETCH ok=%d offered=%llu cached=%llu declared=%llu stored=%llu unverified=%llu bytes=%llu "
        "seconds=%.2f%s%s\n",
        r.ok ? 1 : 0, (unsigned long long)r.offered, (unsigned long long)r.cached, (unsigned long long)r.declared,
        (unsigned long long)r.stored, (unsigned long long)r.unverified, (unsigned long long)r.bytes, r.seconds,
        r.error.empty() ? "" : " error=", r.error.c_str());
    *rc = r.ok ? 0 : 1;
    return true;
}
