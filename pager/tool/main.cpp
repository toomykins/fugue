// fugue-pager: a command-line helper for the pager's logic (listing, targets, sign-in). Commands:
//   parse-listing   stdin = RAW listing bytes            -> {"worlds":[...]} | {"error":...}   (exit 1 on error)
//   parse-target    stdin = JSON array of strings        -> {"results":[{"in":..,"addr":..}|{"in":..,"error":..}]}
//   list            (env HOME etc.)                      -> the `fugue-qt pager -list` text, RAW on stdout
//   rows            stdin = RAW listing bytes            -> {"rows":[...]}  the Live-games tab's display rows
//   sign-in         stdin = {"hub":"host:port","username":..,"password_hex":..,"keytxt":..}
//                                                        -> {"ok":true,"account":..,"keytxt":..} | {"ok":false,"failure":..,"message":..,"detail":..}
// Only sign-in opens a socket, and only to the hub address it is given. The password arrives on stdin as hex, never in
// argv. State is HOME-relative.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>
#include "account/store.h"
#include "pager/listing.h"
#include "pager/model.h"
#include "pager/signin.h"
#include "pager/target.h"

using namespace fugue::pager;

namespace {
QString Q(const std::string& s) { return QString::fromStdString(s); }
int out(const QJsonObject& o, int rc = 0) {
    std::string s = QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString();
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    return rc;
}
std::string readAllStdin() {
    QFile f;
    if (!f.open(stdin, QIODevice::ReadOnly)) std::exit(out({{"error", "cannot open stdin: " + f.errorString()}}, 1));
    return f.readAll().toStdString();
}
QJsonArray strs(const std::vector<std::string>& v) {
    QJsonArray a;
    for (auto& s : v) a.append(Q(s));
    return a;
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) return out({{"error", "usage: fugue-pager parse-listing|parse-target|list|rows|sign-in"}}, 1);
    std::string cmd = argv[1], err;
    if (cmd == "parse-listing" || cmd == "rows") {
        auto raw = readAllStdin();
        auto w = parseListing(raw, &err);
        if (!w) return out({{"error", Q(err)}}, 1);
        QJsonArray arr;
        if (cmd == "rows") {
            for (auto& r : liveRows(*w))
                arr.append(QJsonObject{{"title", Q(r.title)},
                                       {"detail", Q(r.detail)},
                                       {"id", Q(r.id)},
                                       {"players", r.players},
                                       {"version", Q(std::to_string(r.version))}});
            return out({{"rows", arr}});
        }
        for (auto& x : *w)
            arr.append(QJsonObject{{"url", Q(x.url)},
                                   {"id", Q(x.id)},
                                   {"version", Q(std::to_string(x.serverVersion))},
                                   {"status", Q(x.status)},
                                   {"players", strs(x.players)},
                                   {"host", Q(x.host)},
                                   {"lines", strs(x.lines())},
                                   {"title", Q(x.title())}});
        return out({{"worlds", arr}});
    }
    if (cmd == "parse-target") {
        auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(readAllStdin()));
        QJsonArray res;
        for (auto v : doc.array()) {
            std::string in = v.toString().toStdString(), e;
            auto a = parseTarget(in, &e);
            res.append(a ? QJsonObject{{"in", v}, {"addr", Q(*a)}} : QJsonObject{{"in", v}, {"error", Q(e)}});
        }
        return out({{"results", res}});
    }
    if (cmd == "list") {
        std::string e;
        auto cfg = fugue::account::loadConfig(&e);
        if (!cfg) {
            std::fprintf(stderr, "config: %s\n", e.c_str());
            return 1;
        }
        CheckOptions opt;
        opt.selfExe = QCoreApplication::applicationFilePath().toStdString();
        std::string s = renderList(*cfg, check(*cfg, opt));
        std::fwrite(s.data(), 1, s.size(), stdout);
        return 0;
    }
    if (cmd == "sign-in") {
        auto o = QJsonDocument::fromJson(QByteArray::fromStdString(readAllStdin())).object();
        std::string hub = o["hub"].toString().toStdString();
        if (hub.empty())
            return out({{"error", "sign-in needs an explicit \"hub\" address (this helper never dials the real hub)"}},
                       1);
        std::string pw = QByteArray::fromHex(o["password_hex"].toString().toLatin1()).toStdString();
        auto r = signIn(o["username"].toString().toStdString(), pw, o["keytxt"].toString().toStdString(), hub, 10000);
        static const char* names[] = {"none",      "missing-input", "wrong-password", "unreachable",
                                      "no-answer", "refused",       "bad-key",        "disk"};
        if (!r.ok)
            return out({{"ok", false},
                        {"failure", names[int(r.failure)]},
                        {"message", Q(plainSignInMessage(r.failure))},
                        {"detail", Q(r.detail)}},
                       1);
        return out({{"ok", true}, {"account", Q(r.account)}, {"keytxt", Q(r.keyTxtPath)}});
    }
    return out({{"error", Q("unknown command " + cmd)}}, 1);
}
