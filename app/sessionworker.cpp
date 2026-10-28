#include "app/sessionworker.h"
#include "net/guest.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "app/mapcadence.h"
#include "app/choicefetches.h"
#include "app/defaultskin.h"
#include "app/renderthread.h"
#include "audio/player.h"
#include "audio/note.h"
#include "model/browsing.h"
#include "model/prompt.h"
#include "model/resync.h"
#include "model/statpanel.h"
#include "model/verbmenu.h"
#include "model/world.h"
#include "net/input.h"
#include "net/reconnect.h"
#include "pager/target.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/ddmi.h"
#include "render/mousechain.h"
#include "render/hittest.h"
#include "render/scene.h"
#include "skin/skinfeed.h"
#include "shell/recipefiler.h"
#include "skin/winicon.h"

using namespace fugue;

namespace {
// True while anything on the map is moving at `now` (ns, the map's assembly clock): the eye or an atom gliding, an
// animate() chain (an atom's or the client's), a flick() still playing, or a particle set on screen still active. Only
// then does the map need the fast cadence.
bool mapInMotion(const model::World& w, int64_t now) {
    auto live = [&](int64_t start, int64_t dur) { return start > 0 && dur > 0 && now < start + dur; };
    if (w.eyeGliding && live(w.eyeGlide.startNs, w.eyeGlide.durNs)) return true;
    for (const model::AnimSeq& q : model::animSequences(w.clientChain))
        if (q.endNs < 0 || now < q.endNs + 1) return true;
    for (const auto& [id, a] : w.atoms) {
        if (a.gliding && live(a.glide.startNs, a.glide.durNs)) return true;
        if (a.animating)
            for (const model::AnimSeq& q : model::animSequences(a.chain))
                if (q.endNs < 0 || now < q.endNs + 1) return true;
    }
    for (const auto& [id, f] : w.flicks)
        if (f.startNs > 0 && now - f.startNs < int64_t(5e9)) return true;   // 5 s bounds a flick's length
    // A particle set drawn within the last second that still spawns or has particles alive.
    for (const auto& [key, ps] : w.particles)
        if (ps.drawnNs > 0 && now - ps.drawnNs < int64_t(1e9) && model::particlesActive(ps)) return true;
    return false;
}

constexpr int kTickMs = 90;     // the UI tick: chat, stat panel, browse windows

// The INFO control's data: the tab strip, and the selected tab's rows. An atom row carries its icon cell
// (render::statRowIcon, animated at clockNs).
shell::StatView buildStatView(const model::Session& m, render::ResStore& res, int64_t clockNs) {
    const model::World& w = m.world();
    shell::StatView v;
    model::VerbPanel panel = model::verbPanel(w);   // once per tick: the strip and the selected tab's rows share it
    for (const std::string& t : model::statTabs(w, panel)) v.tabs << QString::fromStdString(t);
    v.selected = w.stat.selected;
    std::string cat;
    if (model::verbTabCategory(w, panel, w.stat.selected, cat)) {
        v.verbTab = true;
        for (const model::VerbDef& e : panel.byCategory[cat]) v.rows.append({QString::fromStdString(e.name), QString(), false});
    } else {
        for (const auto& l : w.stat.lines) {
            model::StatValue sv;
            proto::enc::AtomRef ref;
            bool parsed = model::parseStatValue(l.valueJson, sv);
            bool click = parsed && model::statRowRef(w, sv, ref);
            shell::StatRowView r{QString::fromStdString(l.name), QString::fromStdString(model::statText(w, l)), click, {}};
            std::vector<uint8_t> px;
            int cell = model::iconSizeOf(w);
            if (parsed && sv.isAtom() && render::statRowIcon(m, res, sv.appid, cell, clockNs, px, l.icon.ns))
                r.icon = QImage(px.data(), cell, cell, cell * 4, QImage::Format_RGBA8888).copy();
            v.rows.append(r);
        }
    }
    return v;
}
}  // namespace

void SessionWorker::post(Job j) {
    std::lock_guard<std::mutex> g(inMu_);
    if (inbox_.size() < 4096) inbox_.push_back(std::move(j));   // bounded, so a stalled session cannot grow it without limit
}
void SessionWorker::postMouse(const fugue::shell::MapPointer& p, bool rightClickToWire) { post(MouseJob{p, rightClickToWire}); }
void SessionWorker::postCommand(const QString& line, bool promptNull) { post(CmdJob{line.toStdString(), promptNull}); }
void SessionWorker::postStockMove(int dir) { post(MoveJob{dir}); }
void SessionWorker::postTopic(const QString& href) { post(TopicJob{href.toStdString()}); }
void SessionWorker::postMenuQuery(const fugue::shell::MapPointer& p, const QPoint& globalPos) { post(MenuJob{p, globalPos}); }
void SessionWorker::postAtomVerb(quint16 verbId, quint8 kind, quint32 ref, bool viaArgument, const QString& atom) {
    post(AtomVerbJob{verbId, kind, ref, viaArgument, atom.toStdString()});
}
void SessionWorker::postPromptAnswer(quint32 id, bool alert, const QString& text) { post(PromptJob{id, alert, false, text.toStdString()}); }
void SessionWorker::postPromptCancel(quint32 id, bool alert) { post(PromptJob{id, alert, true, {}}); }
void SessionWorker::postArgAnswer(quint32 id, const QString& text) { post(ArgJob{id, false, text.toStdString()}); }
void SessionWorker::postArgCancel(quint32 id) { post(ArgJob{id, true, {}}); }
void SessionWorker::postArgNone(quint32 id) { post(ArgJob{id, false, {}, true}); }
void SessionWorker::postStatTab(int index) { post(StatTabJob{index}); }
void SessionWorker::postStatRow(int row, const std::string& control, const std::string& button, int mods, bool dbl, bool icon, int iconX,
                                int iconY) {
    post(StatRowJob{row, control, button, mods, dbl, icon, iconX, iconY});
}

void SessionWorker::run() {
    std::string dir = cfg_.stateDir.empty() ? fugue::net::defaultStateDir() : cfg_.stateDir;
    if (!dir.empty()) {
        // Status also goes to files, since a desktop-menu launch has no terminal (audio/note.h).
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string nerr;
        if (!audio::setNoteFile(dir + "/audio.log", &nerr)) std::fprintf(stderr, "[session] audio status file: %s\n", nerr.c_str());
        sessionLog_ = dir + "/session.log";
    }
    auto log = [](const std::string& m) { std::fprintf(stderr, "[session] %s\n", m.c_str()); };
    std::unique_ptr<net::Session> s;
    bool sameConnection = false, viaLink = false;
    for (bool first = true;; first = false) {
        reconnect_ = false;
        if (!sameConnection) s = std::make_unique<net::Session>(cfg_, log);
        const auto started = std::chrono::steady_clock::now();
        net::SessionEnding e = runSession(*s, first, sameConnection);
        e.afterLink = viaLink;
        viaLink = !linkTarget_.empty();
        if (stop_) return;
        net::ReconnectStep step = net::reconnectStep(e, first, reconnect_);
        if (viaLink) {   // at most one a second, so worlds linking to each other (or to themselves) cannot spin the client
            reconnect_ = false;
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            step.delayMs = int(std::max<int64_t>(0, 1000 - age));
            const size_t colon = linkTarget_.rfind(':');
            const std::string host = linkTarget_.substr(0, colon);
            const uint16_t port = uint16_t(std::atoi(linkTarget_.c_str() + colon + 1));
            linkTarget_.clear();
            if (host != cfg_.host || port != cfg_.port) {
                cfg_.host = host;
                cfg_.port = port;
                std::lock_guard<std::mutex> g(verbsMu_);
                verbSnap_.reset();   // the old world's verbs, until the new one's 0x0011
                emit worldAddress(QString::fromStdString(host), port);
            }
        }
        sameConnection = step.sameConnection;
        if (!sameConnection) s->close();
        for (const QString& l : QString::fromStdString(step.message).split('\n', Qt::SkipEmptyParts)) emit localOutput(l);
        int ms = step.delayMs;
        if (ms < 0) {   // only .reconnect starts another
            while (!stop_ && !reconnect_) QThread::msleep(50);
        } else {
            for (auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms); !stop_ && !reconnect_ && std::chrono::steady_clock::now() < end;)
                QThread::msleep(50);
        }
        if (stop_) return;
        if (reconnect_) sameConnection = false;
    }
}

void SessionWorker::noteSessionEnd(const std::string& line) {
    std::fprintf(stderr, "[session] ended %s\n", line.c_str());
    if (sessionLog_.empty()) return;
    std::error_code ec;
    const char* mode = std::filesystem::file_size(sessionLog_, ec) > (1u << 20) ? "w" : "a";
    if (std::FILE* f = std::fopen(sessionLog_.c_str(), mode)) {
        std::time_t t = std::time(nullptr);
        char ts[32] = "";
        if (std::tm* tm = std::localtime(&t)) std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", tm);
        std::fprintf(f, "%s %s:%u %s\n", ts, cfg_.host.c_str(), unsigned(cfg_.port), line.c_str());
        std::fclose(f);
    }
}

net::SessionEnding SessionWorker::runSession(net::Session& s, bool first, bool afterReboot) {
    auto log = [](const std::string& m) { std::fprintf(stderr, "[session] %s\n", m.c_str()); };
    {
        std::lock_guard<std::mutex> g(inMu_);
        inbox_.clear();
    }
    s.setManualPrompts(true);       // the GUI's PromptHost answers alert()/input(); never auto-cancel them
    PromptFlow promptFlow;          // pending dialogs and the choices request in flight (app/choicefetches.h)
    model::PromptBook& prompts = promptFlow.book;
    // Missing-verb-argument dialogs held for the GUI, by a client-local id. An entry leaves on the dialog's one outcome
    // and dies with this session.
    std::map<quint32, net::MissingArg> pendingArgs;
    std::map<quint32, net::SrcChoice> pendingSrcs;   // `<Verb> word` src dialogs, same id space and lifetime
    quint32 nextArgId = 1;
    std::map<quint32, shell::PromptRequest> argHeld;   // verb-argument dialogs held for their 0x003f (by pendingArgs id)
    // Verb-argument dialogs whose listbox the 0x003f filled: the listbox shows the choices as model::parseExpansionReply
    // decodes them, and a pick goes back in the 0x0039 as net::listPickText.
    std::set<quint32> argServerLists;
    // List resolves in flight (c2s 0x0039 sent, s2c 0x0039 awaited), in SEND order: the answer names no request, so
    // the oldest owns it. Stock DreamSeeker sends the verb when the answer arrives and not before
    // (net::InputModel::completeResolve). An input() in list's pick resolves on the same s2c 0x0039
    // (model::Prompt::resolvesThroughServer), so it queues here too.
    struct Resolving { net::ListResolve r; std::string line; bool prompt = false; quint32 promptId = 0; };
    std::deque<Resolving> resolving;
    model::Session m;
    // Atom-table resync poll (c2s 0x000b / 0x003d, model/resync.h): after every ingested frame, and from the pump hook.
    model::ResyncPoller resync;
    auto pumpResync = [&] {
        int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        for (const auto& f : resync.poll(m.worldMut(), now)) s.sendInput(f.type, f.body);
    };
    std::function<void()> pumpPrompts;   // PromptFlow::pump: once the frames read are handled, and after each answer
    s.setPumpHook([&] {
        pumpResync();
        if (pumpPrompts) pumpPrompts();
    });
    render::ResStore res;
    render::SceneCache sceneCache; // main and auxiliary maps share parsed fields, never resolved placements/resources
    QFile sceneTrace;
    const auto sceneTracePath = qEnvironmentVariable("FUGUE_SCENE_TRACE");
    if (!sceneTracePath.isEmpty()) {
        sceneTrace.setFileName(sceneTracePath);
        if (sceneTrace.open(QIODevice::WriteOnly | QIODevice::Truncate))
            sceneTrace.write("map,clock_ns,prepare_ms,appearance_ms,appearance_parses,appearance_hits,width,height,layers,success,cache_bytes,cache_entries\n");
        else
            std::fprintf(stderr, "[scene-trace] cannot open %s: %s\n", qPrintable(sceneTracePath), qPrintable(sceneTrace.errorString()));
    }
    const bool sceneProfile = qEnvironmentVariableIntValue("FUGUE_SCENE_PROFILE") != 0;
    const bool profilePreparation = sceneProfile || sceneTrace.isOpen();
    auto sceneLogAt = std::chrono::steady_clock::now();
    double scenePrepareSum = 0, sceneAppearanceSum = 0;
    uint64_t sceneSamples = 0, sceneParses = 0, sceneHits = 0;
    auto tracePreparation = [&](const std::string& id, int64_t clock, double ms, const render::SceneStats& stats,
                                const render::DrawList& dl, bool ok) {
        if (sceneTrace.isOpen()) {
            QByteArray row = '"' + QByteArray::fromStdString(id).replace("\"", "\"\"") + '"';
            row += ',' + QByteArray::number(qlonglong(clock));
            for (double v : {ms, stats.appearanceBuildMs}) row += ',' + QByteArray::number(v, 'f', 6);
            for (uint64_t v : {stats.appearanceParses, stats.appearanceCacheHits}) row += ',' + QByteArray::number(qulonglong(v));
            for (int v : {dl.outW, dl.outH, stats.layers, int(ok)}) row += ',' + QByteArray::number(v);
            for (size_t v : {sceneCache.retainedBytes(), sceneCache.entries()}) row += ',' + QByteArray::number(qulonglong(v));
            row += '\n';
            if (sceneTrace.write(row) != row.size()) sceneTrace.close();
        }
        if (id.empty()) {
            scenePrepareSum += ms; sceneAppearanceSum += stats.appearanceBuildMs;
            ++sceneSamples; sceneParses += stats.appearanceParses; sceneHits += stats.appearanceCacheHits;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - sceneLogAt >= std::chrono::seconds(1)) {
            if (sceneProfile && sceneSamples)
                std::fprintf(stderr, "[scene-profile] main prepare=%.3fms appearance=%.3fms parses=%.1f hits=%.1f samples=%llu\n",
                             scenePrepareSum / sceneSamples, sceneAppearanceSum / sceneSamples,
                             double(sceneParses) / sceneSamples, double(sceneHits) / sceneSamples,
                             (unsigned long long)sceneSamples);
            if (sceneTrace.isOpen()) sceneTrace.flush();
            sceneLogAt = now; scenePrepareSum = sceneAppearanceSum = 0; sceneSamples = sceneParses = sceneHits = 0;
        }
    };
    net::InputModel im;              // verbs + the mouse subscription mask (0x0011 / 0x003b), session thread only
    render::MouseChain chain;        // press/drag/double-click state, session thread only
    render::HoverTracker hover;      // one for every map control, as stock keeps one; session thread only
    // The live skin: skin and winset traffic goes in, and wingets are answered from it. Shared with the GUI.
    std::shared_ptr<skin::LiveSkin> live = live_ ? live_ : std::make_shared<skin::LiveSkin>();
    skin::SkinFeed feed(live);
    feed.setDefaultSkin(app::kDefaultSkin);   // a world that sends no skin gets this one
#ifdef FUGUE_RELEASE
    feed.setClientVersion(kVersionRelease);   // winget "fugue-version"
#endif
    // winget "url" is the address the client dialed, as "IP:port" (stock DreamSeeker 516 does the same).
    live->setClientEnv("url", cfg_.host + ":" + std::to_string(cfg_.port));
    m.setTextTarget([live](const std::string& c) { return live->printsOutput(c); });
    auto nowMs = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    auto winReply = [&](uint32_t reqId, const std::string& v) {
        s.replyWinget(reqId, v);
        std::fprintf(stderr, "[skin] winget req %u -> \"%s\"\n", reqId, v.substr(0, 120).c_str());
    };
    std::map<std::string, std::string> lastBrowse;   // window -> the html the GUI was last sent
    std::map<std::string, uint64_t> cloneOpen;       // winclone()d window -> the page seq the GUI was last sent
    // Windows opened by a browse_file offer (never seen as 0x009f), and the popups still open when the last session ended:
    // stock DreamSeeker keeps them open across a reconnect, a reboot and a link().
    std::set<std::string> fileBrowse = openPopups_;
    std::vector<model::RscRecord> rscUnresolved;     // 0x00fe records whose ordinal is not yet in the 0x000e list
    auto syncBrowseNames = [&] {
        std::set<std::string> names = fileBrowse;
        for (const auto& [n, b] : m.world().browsers) names.insert(n);
        openPopups_ = names;
        feed.setBrowseNames(std::move(names));
    };
    // A browse_rsc file that a page is about to load jumps the declaration queue: stock DreamSeeker 516 answers a 0x00fe
    // for an undownloaded kind-0 file with a 0x003c for that ordinal, ahead of anything else queued.
    // Collected per frame and handed over in one prioritize() call.
    std::vector<uint32_t> rscWanted;
    auto declareRsc = [&](const model::RscRecord& r) {
        const auto& list = m.world().resources;
        if (r.ordinal >= list.size()) return false;
        uint32_t id = list[r.ordinal].first;
        if (rsc_) rsc_->declare(r.name, id);   // 0x00fe's u32 is an ORDINAL into the 0x000e list, not an id
        rscWanted.push_back(id);
        return true;
    };
    auto emitPrompt = [&](const model::Prompt& p) {
        shell::PromptRequest r;
        r.alert = p.kind == model::Prompt::Alert;
        r.id = p.id;
        r.title = QString::fromStdString(p.title);
        r.message = QString::fromStdString(p.message);
        r.def = QString::fromStdString(p.def);
        for (const std::string& b : p.buttons) r.buttons << QString::fromStdString(b);
        r.typeMask = p.typeMask;
        r.inList = p.inList;
        for (const std::string& c : p.choices) r.choices << QString::fromStdString(c);
        std::fprintf(stderr, "[prompt] %s id=%u title=\"%s\" %s%s -> GUI\n", r.alert ? "alert" : "input", p.id, p.title.c_str(),
                     r.alert ? "" : p.typeName().c_str(), p.inList ? " in list" : "");
        emit promptRequested(r);
    };
    // Sound (s2c 0x006d) bypasses the model: the feed resolves its ordinal against the 0x000e list as it stands after
    // this frame's ingest, and reads the bytes from the resource store.
    audio::Player player(audioSpec_);
    audio::SoundFeed sounds(player, audio::SoundFeed::Env{
        [&](uint32_t ord, uint32_t& id) {
            const auto& list = m.world().resources;
            if (ord >= list.size()) return false;
            id = list[ord].first;
            return true;
        },
        [&](uint32_t id) { return s.resources().storedThisRun().count(id) != 0; },
        [&](uint32_t id, Bytes& b, std::string& name) {
            if (!s.resources().readStored(id, b)) return false;
            auto it = s.resources().storedThisRun().find(id);
            if (it != s.resources().storedThisRun().end()) name = it->second;
            return true;
        },
    });
    MapCadence mapPace;
    s.setObserver([&](uint16_t type, ByteSpan body) {
        if (type == 0x009f) return;   // resource-pump frames are not model input
        if (type == model::kAlertMsg || type == model::kInputMsg || type == model::kExpansionReplyMsg || type == model::kPromptWithdrawMsg ||
            type == ChoiceFetches::kEchoMsg) {
            // alert()/input() requests, 0x009d withdrawals, the 0x003f answer to a choices request and the 0x0090 echo
            // behind it. Dialogs open and requests go out from pumpPrompts.
            PromptFlow::Step st = promptFlow.onFrame(type, body);
            if (st.replaced) emit promptWithdrawn(st.id, st.alert);
            switch (st.kind) {
            case PromptFlow::Step::Withdraw:
                std::fprintf(stderr, "[prompt] %s id=%u withdrawn by the server (0x009d)\n", st.alert ? "alert" : "input", st.id);
                emit promptWithdrawn(st.id, st.alert);
                break;
            case PromptFlow::Step::Dropped:
                if (st.abandoned) std::fprintf(stderr, "[prompt] late 0x003f for timed-out verb-argument id=%u - dropped\n", st.id);
                else std::fprintf(stderr, "[prompt] 0x003f for no in-list prompt of ours - dropped\n");
                break;
            case PromptFlow::Step::VerbArgChoices:
                if (auto it = argHeld.find(st.id); it != argHeld.end()) {
                    shell::PromptRequest r = std::move(it->second);
                    argHeld.erase(it);
                    std::vector<std::string> ch;
                    if (model::parseExpansionReply(body, ch)) for (const std::string& c : ch) r.choices << QString::fromStdString(c);
                    argServerLists.insert(r.id);
                    std::string raw;
                    for (const std::string& c : ch) raw += (raw.empty() ? "" : "|") + c;
                    std::fprintf(stderr, "[prompt] verb-argument id=%u: %d choices from 0x003f (%s)\n", r.id, int(r.choices.size()), raw.substr(0, 200).c_str());
                    emit promptRequested(r);
                }
                break;
            default: break;
            }
        }
        im.onFrame(type, body);
        if (type == 0x003b) emit popupMenus(im.showPopupMenus());
        if (type == 0x0011) {
            auto snap = std::make_shared<const net::InputModel>(im);
            std::lock_guard<std::mutex> g(verbsMu_);
            verbSnap_ = std::move(snap);
        }
        // Stamp the frame with the same wall clock the map is assembled at (clockNs below). Without it, glides and
        // animate() chains start at the model's default clock and snap instead of running.
        m.setIngestClockNs(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        m.ingest(type, body);
        pumpResync();
        feed.onFrame(type, body);     // 0x00af / 0x00ef / 0x00a8
        if (type == audio::kSoundMsg) sounds.onFrame(body, nowMs());
        if (type == 0x0084) {
            // Every browse is delivered, even an unchanged body: stock DreamSeeker treats an unchanged body as no change
            // and does not reopen a window the player closed.
            model::BrowseFrame bf;
            if (model::parseBrowseFrame(body, bf)) {
                std::string name = model::parseBrowseParams(bf.params).window;
                if (bf.close) { fileBrowse.erase(name); lastBrowse.erase(name); emit browseClosed(QString::fromStdString(name)); }
                else { lastBrowse[name] = bf.html; emit browseHtml(QString::fromStdString(name), QString::fromStdString(bf.params), QString::fromStdString(bf.html)); }
            }
            syncBrowseNames();
        } else if (type == 0x00a7) {
            model::OutputFrame of;
            model::PageCall pc;
            if (model::parseOutputFrame(body, of) && model::parsePageCall(of.control, of.value, pc)) {
                QStringList args;
                for (const std::string& a : pc.args) args << QString::fromStdString(a);
                emit pageCall(QString::fromStdString(pc.target), QString::fromStdString(pc.fn), args);
            }
        } else if (type == 0x0079) {
            model::LinkFrame lf;
            if (model::parseLinkFrame(body, lf)) {
                if (lf.kind == model::LinkFrame::Topic) {
                    bool ok = s.sendInput(proto::enc::kTopicMsg, proto::enc::encodeTopic(lf.url));
                    std::fprintf(stderr, "[link] topic %s %s\n", lf.url.substr(0, 120).c_str(), ok ? "sent" : "NOT SENT");
                } else if (lf.kind == model::LinkFrame::External) {
                    std::fprintf(stderr, "[link] open %s\n", lf.url.substr(0, 200).c_str());
                    emit externalLink(QString::fromStdString(lf.url));
                } else if (lf.kind == model::LinkFrame::World) {
                    // Stock DreamSeeker joins the linked world in the same window, whichever world it is (pager/target.h
                    // refuses a hub name or a missing port). An identical skin keeps every control's state across it.
                    std::string why;
                    std::optional<std::string> t = pager::parseTarget(lf.url, &why);
                    std::fprintf(stderr, "[link] world %s: %s\n", lf.url.substr(0, 200).c_str(), t ? ("joining " + *t + " in this window").c_str() : why.c_str());
                    if (!t) {
                        emit localOutput(QString::fromStdString("Link not followed: " + why));
                    } else {
                        linkTarget_ = *t;
                        reconnect_ = true;
                    }
                } else {
                    std::fprintf(stderr, "[link] scheme not opened: %s\n", lf.url.substr(0, 200).c_str());
                }
            }
        } else if (type == 0x00fe) {
            for (auto& r : model::parseBrowseRscFrame(body)) if (!declareRsc(r)) rscUnresolved.push_back(r);
            s.resources().prioritize(rscWanted);
            rscWanted.clear();
        }
        framesIngested++;
        if (mapPace.ingested()) s.wake();   // a still map: draw this frame now, not at the end of the idle wait
    });
    // Main window icon (skin/winicon.h): this world's files by name, and its 0x000e list by ordinal.
    skin::IconResolver icon(skin::IconResolver::Env{
        [&](uint32_t ord, uint32_t& id) {
            const auto& list = m.world().resources;
            if (ord >= list.size()) return false;
            id = list[ord].first;
            return true;
        },
        [&](uint32_t id) { return s.resources().storedThisRun().count(id) != 0; },
        [&](uint32_t id, std::string& out) {
            Bytes b;
            if (!s.resources().readStored(id, b)) return false;
            out.assign(reinterpret_cast<const char*>(b.data()), b.size());
            return true;
        },
        [&](uint32_t id) { return s.resources().store().nameOf(id); },
    });
    shell::BrowseRecipeFiler browseRecipes;
    s.setResourceObserver([&](uint32_t id, const std::string& name, ByteSpan data) {
        icon.noteResource(id, name);
        m.noteResource(id, data, name);
        if (rsc_ && !render::isDdmi(data.data(), data.size())) rsc_->put(id, name, std::string(reinterpret_cast<const char*>(data.data()), data.size()));
        // browse(file(...)) arrives as a resource plus a directive in the offer's params (not as a 0x0084): open it as a
        // browse window, or file it under the alias a page will ask for.
        std::string opts;
        if (model::browseFileOptions(s.resources().offerParams(id), opts)) {
            model::BrowseParams bp = model::parseBrowseParams(opts);
            if (!bp.window.empty()) {
                fileBrowse.insert(bp.window);
                syncBrowseNames();
                std::string html(reinterpret_cast<const char*>(data.data()), data.size());
                lastBrowse[bp.window] = html;
                log("browse_file \"" + name + "\" -> window " + bp.window + " (" + std::to_string(data.size()) + " bytes)");
                emit browseHtml(QString::fromStdString(bp.window), QString::fromStdString(opts), QString::fromStdString(html));
            } else if (std::string alias = model::browseFileAlias(opts); !alias.empty() && rsc_) {
                rsc_->alias(alias, id);
                log("browse_file \"" + name + "\" filed as " + alias);
            }
        } else if (std::string ctl; model::outputFileControl(s.resources().offerParams(id), ctl)) {
            log("output_file \"" + name + "\" -> control " + ctl + " (" + std::to_string(data.size()) + " bytes)");
            emit outputFile(QString::fromStdString(ctl), QString::fromStdString(std::string(reinterpret_cast<const char*>(data.data()), data.size())));
        }
        std::string err;
        uint32_t claimed = id;
        if (!res.add(data, &claimed, &err)) log("resource refused: " + err);
        if (rsc_) browseRecipes.onResource(id, name, data, res, m.world().resources, *rsc_);
        feed.onResource(id, name, data);   // the skin may arrive as a .dmf resource named by 0x00ef
    });
    // winget (0x00a9): the skin holds a query until it has a measured layout.
    s.setWingetHandler([&](ByteSpan body) {
        skin::WingetQuery q;
        if (!skin::parseWingetBody(body, q)) return;   // malformed: nothing is sent either
        feed.submitWinget(q, nowMs(), winReply);
    });
    s.setMapStyleProvider([&] { return feed.live().defaultMapStyle(); });
    net::SessionEnding out;
    out.afterReboot = afterReboot;
    std::string err = afterReboot ? s.relogin() : s.connectAndLogin();
    if (first) emit loggedIn(QString::fromStdString(err));
    out.connected = s.stats().connected;
    if (!err.empty()) {
        out.why = err;
        noteSessionEnd("at login: " + err);
        return out;
    }
    out.loggedIn = true;

    uint64_t skinLoadsSeen = 0;
    uint64_t chatSeen = 0;
    std::optional<shell::StatView> lastStatView;   // none yet: a session's first view replaces the last session's panel
    QString lastName;
    int64_t lastTickNs = 0, lastFrameNs = 0;
    std::string lastAssembleErr;
    std::map<std::string, std::string> lastAuxErr;   // per secondary map (render::assembleSecondaryMap)
    bool dlLogged = false;
    auto flushChat = [&] {
        const model::World& w = m.world();
        if (w.chatTotal == chatSeen) return;
        size_t take = size_t(std::min<uint64_t>(w.chatTotal - chatSeen, w.chatHtml.size()));
        QStringList l;
        for (size_t i = w.chatHtml.size() - take; i < w.chatHtml.size(); i++) l << QString::fromStdString(w.chatHtml[i]);
        chatSeen = w.chatTotal;
        emit chatLines(l);
    };
    // ---- input: everything below runs on this thread, the socket's only writer ----
    // A verb's candidate srcs (model::verbSrcCandidates): client, own mob, the movables it can see, usr.contents,
    // client.screen, turfs in view, the own area. With one candidate the verb is sent; with several, the player is asked
    // (srcChoice), as stock DreamSeeker does.
    auto srcFor = [&](uint16_t id) {
        std::vector<net::SrcCandidate> out;
        const model::World& w = m.world();
        for (const model::VerbSrcCandidate& c : model::verbSrcCandidates(w, id, true)) {
            bool own = c.src.kind == proto::enc::kVerbSrcClient || (c.src.kind == proto::enc::kVerbSrcMob && w.haveSelf && c.src.ref == w.selfRef);
            out.push_back({{c.src.kind, c.src.ref}, c.name, !own, std::nullopt, c.rank.dist, c.rank.cell, c.rank.carried});
        }
        return out;
    };
    // A typed atom argument's candidates (model::verbArgCandidates; net::InputModel::ArgResolver): the atoms a view,
    // range or usr argument may name, matched by name on this side, as stock DreamSeeker does. nullopt means none.
    auto argAtoms = [&](const net::VerbArgSpec& a) -> std::optional<std::vector<net::SrcCandidate>> {
        model::VerbArgSpec ma{a.typeMask, a.filter, a.filterKind};
        if (!model::argResolvesLocally(ma)) return std::nullopt;
        std::vector<net::SrcCandidate> out;
        const model::World& w = m.world();
        for (const model::VerbSrcCandidate& c : model::verbArgCandidates(w, ma)) {
            bool own = c.src.kind == proto::enc::kVerbSrcMob && w.haveSelf && c.src.ref == w.selfRef;
            out.push_back({{c.src.kind, c.src.ref}, c.name, !own, std::nullopt, c.rank.dist, c.rank.cell, c.rank.carried});
        }
        return out;
    };
    auto sendBodies = [&](const char* what, const std::vector<Bytes>& bodies) {
        for (const Bytes& b : bodies) {
            bool ok = s.sendInput(proto::enc::kClientCmd, b);
            std::fprintf(stderr, "[input] %s: 0x0002 sub=%02x len=%zu %s\n", what, b.empty() ? 0 : b[0], b.size(), ok ? "sent" : "NOT SENT");
        }
    };
    // Runs a typed or macro command line. A missing argument opens a dialog, as stock DreamSeeker does, rather than
    // printing usage. `line` names the command in the log.
    auto runResult = [&](const std::string& line, const net::CommandResult& r) {
        if (r.srcChoice) {
            // Several atoms carry the verb: ask which one, listing their names as stock DreamSeeker does.
            if (!pendingArgs.empty() || !pendingSrcs.empty()) {
                std::fprintf(stderr, "[input] command \"%s\": a dialog is already open - ignored\n", line.c_str());
                return;
            }
            shell::PromptRequest pr;
            pr.alert = false;
            pr.verbArg = true;
            pr.id = nextArgId++;
            pr.title = QString::fromStdString(r.srcChoice->title);
            pr.typeMask = model::kInputText;
            pr.inList = true;
            for (const net::SrcCandidate& c : r.srcChoice->candidates) pr.choices << QString::fromStdString(c.name);
            pendingSrcs[pr.id] = *r.srcChoice;
            std::fprintf(stderr, "[input] command \"%s\": %zu srcs carry it - asking \"%s\"\n", line.c_str(), r.srcChoice->candidates.size(),
                         r.srcChoice->title.c_str());
            emit promptRequested(pr);
            return;
        }
        if (r.resolve) {
            // An `in <list>` argument: the server names its value first, and the 0x0002 waits for that answer.
            bool ok = s.sendInput(net::kListResolveMsg, net::encodeVerbExpansionRequest(r.resolve->at, r.resolve->text));
            std::fprintf(stderr, "[input] command \"%s\": argument %zu \"%s\" -> 0x0039 %s\n", line.c_str(), r.resolve->at.index + 1,
                         r.resolve->text.c_str(), ok ? "sent" : "NOT SENT");
            if (ok) resolving.push_back({*r.resolve, line});
            return;
        }
        if (r.missing) {
            if (!pendingArgs.empty() || !pendingSrcs.empty()) {
                // One dialog at a time, so a held or auto-repeating macro key cannot stack them.
                std::fprintf(stderr, "[input] command \"%s\": an argument dialog is already open - ignored\n", line.c_str());
                return;
            }
            net::ArgPrompt ap = net::argPrompt(*r.missing);
            shell::PromptRequest pr;
            pr.alert = false;
            pr.verbArg = true;
            pr.id = nextArgId++;
            pr.title = QString::fromStdString(ap.title);
            pr.message = QString::fromStdString(ap.message);
            pr.typeMask = ap.kind == net::ArgPrompt::Message ? model::kInputMessage : ap.kind == net::ArgPrompt::Num ? model::kInputNum : model::kInputText;
            pr.inList = ap.kind == net::ArgPrompt::List;
            pr.allowNull = ap.allowNull;
            for (const std::string& c : ap.choices) pr.choices << QString::fromStdString(c);
            pendingArgs[pr.id] = *r.missing;
            std::fprintf(stderr, "[input] command \"%s\": argument %zu missing - prompting \"%s\"\n", line.c_str(), r.missing->index + 1, ap.title.c_str());
            if (pr.inList && !r.missing->atoms) {
                // The listbox is filled by a 0x003f round trip addressed by the argument's ordinal. The dialog is held until
                // it answers (or ChoiceFetches::kWait passes), so it opens complete, as an input() in list does.
                std::fprintf(stderr, "[input] verb expansion ordinal %u src %02x:%u: 0x003f queued\n", ap.ordinal, r.missing->src.kind, r.missing->src.ref);
                argHeld[pr.id] = pr;
                promptFlow.askVerbArg(pr.id, net::kVerbExpansionMsg, net::encodeVerbExpansionRequest(*r.missing), std::chrono::steady_clock::now());
            } else {
                emit promptRequested(pr);
            }
            return;
        }
        if (!r.error.empty() && r.quiet) {
            std::fprintf(stderr, "[input] command \"%s\" not sent: %s\n", line.c_str(), r.error.c_str());
        } else if (!r.error.empty()) {
            std::fprintf(stderr, "[input] command \"%s\" refused locally: %s\n", line.c_str(), r.error.c_str());
            // The refusal is two output lines, as stock DreamSeeker prints them.
            for (const QString& l : QString::fromStdString(r.error).split('\n')) emit localOutput(l);
        }
        for (auto& f : r.frames) {
            bool ok = s.sendInput(f.type, f.body);
            std::fprintf(stderr, "[input] command \"%s\": 0x%04x len=%zu %s\n", line.c_str(), f.type, f.body.size(), ok ? "sent" : "NOT SENT");
        }
    };
    auto runCommand = [&](const std::string& line, bool promptNull) {
        runResult(line, im.verbCommand(line, m.world().haveSelf, srcFor, argAtoms, promptNull));
    };
    auto drainInbox = [&] {
        std::deque<Job> jobs;
        { std::lock_guard<std::mutex> g(inMu_); jobs.swap(inbox_); }
        for (Job& j : jobs) {
            if (auto* mj = std::get_if<MouseJob>(&j)) {
                render::ChainEnv env;
                env.scene = mj->p.scene.get();
                // Until the world's 0x003b subscription arrives, send every hook: an unwanted frame is a no-op, but a
                // missing one can break the world.
                env.hooks = im.haveMouseSubs() ? im.hooks() : proto::enc::allMouseHooks();
                env.rightClickToWire = mj->rightClick;
                env.control = mj->p.control;
                env.offMap = mj->p.offMap;
                env.clientWheel = !im.haveMouseSubs() || im.clientWheel();
                env.clientHover = im.clientHover();
                env.clientMove = im.clientMove();
                std::vector<Bytes> fr;
                using K = fugue::shell::MapPointer;
                if (mj->p.kind == K::Wheel) {
                    sendBodies("mouse-wheel", render::wheelFrames(mj->p.ev, env, mj->p.wheelX, mj->p.wheelY));
                    continue;
                }
                if (mj->p.kind == K::Hover || mj->p.kind == K::Leave) {
                    fr = mj->p.kind == K::Hover ? hover.at(mj->p.ev, env, mj->p.motion) : hover.leave();
                    for (const Bytes& b : fr)   // unlogged when sent: a MouseMove world reports every pixel step
                        if (!s.sendInput(proto::enc::kClientCmd, b)) std::fprintf(stderr, "[input] mouse-hover: len=%zu NOT SENT\n", b.size());
                    continue;
                }
                if (mj->p.kind == K::Press) { hover.forget(); fr = chain.press(mj->p.ev, env); }
                else if (mj->p.kind == K::Move) fr = chain.move(mj->p.ev, env);
                else fr = chain.release(mj->p.ev, env);
                std::string what = std::string("mouse-") + (mj->p.kind == K::Press ? "press" : mj->p.kind == K::Move ? "move" : "release") + "(" + chain.note() + ")";
                sendBodies(what.c_str(), fr);
            } else if (auto* mv = std::get_if<MoveJob>(&j)) {
                net::FrameOut f = net::stockMoveFrame(mv->dir);
                bool ok = s.sendInput(f.type, f.body);
                std::fprintf(stderr, "[input] move dir=%d: 0x%04x len=%zu %s (sub 0: stock move command)\n", mv->dir, f.type, f.body.size(),
                             ok ? "sent" : "NOT SENT");
            } else if (auto* cj = std::get_if<CmdJob>(&j)) {
                runCommand(cj->line, cj->promptNull);
            } else if (auto* aj = std::get_if<ArgJob>(&j)) {
                if (auto sc = pendingSrcs.find(aj->id); sc != pendingSrcs.end()) {
                    net::SrcChoice choice = std::move(sc->second);
                    pendingSrcs.erase(sc);
                    if (aj->cancel) { std::fprintf(stderr, "[input] \"%s\" cancelled - nothing sent\n", choice.title.c_str()); continue; }
                    // The picked name is the src. Names are unique in the list.
                    std::vector<net::SrcCandidate> picked;
                    for (const net::SrcCandidate& c : choice.candidates)
                        if (c.name == aj->text) { picked.push_back(c); break; }
                    if (picked.empty()) { std::fprintf(stderr, "[input] \"%s\": \"%s\" is not in the list - nothing sent\n", choice.title.c_str(), aj->text.c_str()); continue; }
                    runResult(choice.line + " <- src \"" + aj->text + "\"",
                              im.verbCommand(choice.line, m.world().haveSelf, [&](uint16_t) { return picked; }, argAtoms, choice.promptNull));
                    continue;
                }
                auto it = pendingArgs.find(aj->id);
                if (it == pendingArgs.end()) continue;
                net::MissingArg miss = std::move(it->second);
                pendingArgs.erase(it);
                bool serverList = argServerLists.erase(aj->id) > 0;
                if (aj->cancel) { std::fprintf(stderr, "[input] %s argument %zu cancelled - nothing sent\n", miss.verb.name.c_str(), miss.index + 1); continue; }
                if (aj->none) { runResult(miss.line + " <- None", net::InputModel::completeNull(miss, m.world().haveSelf, argAtoms)); continue; }
                // The value is the argument, verbatim: it is never re-parsed as a typed line, whose quote rules would
                // eat a leading ' or ". A pick from a 0x003f listbox goes back as net::listPickText.
                std::string value = serverList ? net::listPickText(aj->text) : aj->text;
                runResult(miss.line + " <- \"" + aj->text + "\"", net::InputModel::completeArg(miss, value, m.world().haveSelf, argAtoms));
            } else if (auto* qj = std::get_if<MenuJob>(&j)) {
                // Right-click verb menu: hit-test the scene the player saw, then apply the model's eligibility rules
                // (model/verbmenu.h). One row per atom under the pointer (render::hitStack), topmost first, each with its
                // icon and a submenu of that atom's verbs.
                std::vector<render::Hit> hits;
                if (qj->p.scene) hits = render::hitStack(*qj->p.scene, qj->p.ev.imgX, qj->p.ev.imgY);
                if (hits.empty()) { std::fprintf(stderr, "[menu] no atom under the cursor\n"); continue; }
                QList<MenuAtom> atoms;
                bool actionable = false;
                for (const render::Hit& h : hits) {
                    model::HitTarget ht = model::targetFromHit(m.world(), h.kind, h.ref, h.worldX, h.worldY, h.screen);
                    MenuAtom ma;
                    ma.name = QString::fromStdString(h.name);
                    for (const model::MenuVerb& mv : model::atomMenu(m.world(), ht.appid, ht.target)) {
                        VerbMenuItem it;
                        it.name = QString::fromStdString(mv.verb.name);
                        it.id = mv.verb.id;
                        it.viaArgument = mv.viaArgument;
                        it.kind = ht.target.kind;
                        it.ref = ht.target.kind == model::kTargetTurf ? (uint32_t(std::max(1, ht.target.x) - 1) | uint32_t(std::max(1, ht.target.y) - 1) << 16) : ht.target.ref;
                        it.atom = ma.name;
                        ma.verbs.append(it);
                    }
                    int iw = 0, ih = 0;
                    std::vector<uint8_t> px;
                    // Drawn from the atom's appearance (render/scene.h appearanceIcon), not from the map layers: those miss
                    // HUD atoms and carry map-only colour and offsets.
                    if (render::appearanceIcon(m, res, h.appid, h.dir, iw, ih, px))
                        ma.icon = QImage(px.data(), iw, ih, iw * 4, QImage::Format_RGBA8888).copy();
                    if (!ma.verbs.isEmpty()) actionable = true;
                    std::fprintf(stderr, "[menu] \"%s\" kind=%u ref=%u appid=%u cell=(%d,%d): %d verbs, icon %s\n", h.name.c_str(), ht.target.kind, ht.target.ref,
                                 ht.appid, ht.target.x, ht.target.y, int(ma.verbs.size()), ma.icon.isNull() ? "NONE" : (std::to_string(iw) + "x" + std::to_string(ih)).c_str());
                    atoms.append(ma);
                }
                if (!actionable) continue;   // no popup when nothing on the tile offers a verb
                emit verbStackReady(qj->global, atoms);
            } else if (auto* aj = std::get_if<AtomVerbJob>(&j)) {
                Bytes body;
                if (aj->viaArgument) {
                    // A verb the player carries, taking the atom as its first argument. Its src is the owner of the
                    // client or own-mob verb section that holds it (model::ownVerbIds).
                    model::VerbSrc vs;
                    auto ve = im.verbs().find(aj->verbId);
                    if (!model::ownVerbSrc(m.world(), aj->verbId, vs) || ve == im.verbs().end()) {
                        std::fprintf(stderr, "[input] atom verb %u via-argument: neither the client nor the own mob carries it now - not sent\n", aj->verbId);
                        continue;
                    }
                    // Sent as [tag 0][kind][ref], or, for a runtime-list argument, as the 0x0039 naming the atom
                    // (net::InputModel::atomArgument).
                    model::AtomTarget t;
                    t.kind = aj->kind; t.ref = aj->ref;
                    if (t.kind == model::kTargetTurf) { t.x = int(aj->ref & 0xffff) + 1; t.y = int(aj->ref >> 16) + 1; }
                    std::string text = net::atomArgumentText(aj->atom, model::dmRefOf(m.world(), t));
                    runResult(ve->second.name + " <- menu \"" + text + "\"",
                              net::InputModel::atomArgument(ve->second, net::VerbSrc{vs.kind, vs.ref}, aj->kind, aj->ref, text, m.world().haveSelf,
                                                            aj->atom, argAtoms));
                    continue;
                } else if (auto ve = im.verbs().find(aj->verbId); ve != im.verbs().end()) {
                    // The atom's own verb: the atom is the src (kind 1 obj / 2 mob / 3 turf), and its arguments are
                    // prompted for as from the stat panel.
                    runResult(ve->second.name + " <- menu on \"" + aj->atom + "\"",
                              net::InputModel::atomVerb(ve->second, net::VerbSrc{aj->kind, aj->ref}, aj->atom, m.world().haveSelf, argAtoms));
                    continue;
                } else {
                    body = proto::enc::encodeVerb(aj->verbId, aj->kind, aj->ref, {});
                }
                bool ok = s.sendInput(proto::enc::kClientCmd, body);
                std::fprintf(stderr, "[input] atom verb %u %s kind=%u ref=%u: len=%zu %s\n", aj->verbId, aj->viaArgument ? "via-argument" : "own-section", aj->kind, aj->ref, body.size(), ok ? "sent" : "NOT SENT");
            } else if (auto* pj = std::get_if<PromptJob>(&j)) {
                // Exactly one reply per request. A cancel is sent only where stock DreamSeeker offers one.
                model::PromptReply rep = pj->cancel ? prompts.cancel(pj->id)
                                       : pj->alert  ? prompts.answerAlert(pj->id, pj->text)
                                                    : prompts.answerInput(pj->id, pj->text);
                if (rep.type == 0) {   // withdrawn by the server, or a cancel stock refuses (model::Prompt::cancellable)
                    std::fprintf(stderr, "[input] prompt %u %s - nothing sent\n", pj->id,
                                 prompts.withdrawn(pj->id, pj->alert) ? "withdrawn by the server" : "cancel refused, not cancellable");
                    continue;
                }
                bool ok = s.sendInput(rep.type, rep.body);
                std::fprintf(stderr, "[input] prompt %u %s: 0x%04x len=%zu %s\n", pj->id, pj->cancel ? "CANCEL" : "answer", rep.type, rep.body.size(), ok ? "sent" : "NOT SENT");
                // An in-list pick: the 0x0063 asks the server for the member, and the input() is answered with its reply.
                if (ok && rep.type == model::kInputResolveMsg) resolving.push_back({{}, "input() " + pj->text, true, pj->id});
            } else if (auto* sj = std::get_if<StatTabJob>(&j)) {
                model::StatTabSelect sel;
                if (model::selectStatTab(m.worldMut(), sj->index, sel)) {
                    bool ok = s.sendInput(model::kStatTabMsg, model::encodeStatTabBody(sel.index, sel.token, sel.name));
                    std::fprintf(stderr, "[input] stat tab %u \"%s\" token=%u: 0x006b %s\n", sel.index, sel.name.c_str(), sel.token, ok ? "sent" : "NOT SENT");
                } else {
                    std::fprintf(stderr, "[input] stat tab %d out of range\n", sj->index);
                }
            } else if (auto* rj = std::get_if<StatRowJob>(&j)) {
                const model::World& w = m.world();
                model::StatValue sv;
                proto::enc::AtomRef ref;
                if (rj->row >= 0 && size_t(rj->row) < w.stat.lines.size() && model::parseStatValue(w.stat.lines[size_t(rj->row)].valueJson, sv) && model::statRowRef(w, sv, ref)) {
                    model::StatRowClick c;
                    c.sub = rj->dbl ? proto::enc::kMouseDblClick : proto::enc::kMouseClick;
                    c.button = rj->button;
                    c.mods.ctrl = rj->mods & 1; c.mods.shift = rj->mods & 2; c.mods.alt = rj->mods & 4;   // StatPanelWidget::RowMods
                    c.control = rj->control;
                    c.icon = rj->icon; c.iconX = rj->iconX; c.iconY = rj->iconY;
                    // Gated by the world's 0x003b subscription, as a map click is.
                    bool wanted = !im.haveMouseSubs() || (rj->dbl ? im.hooks().dblClick : im.hooks().click);
                    bool ok = wanted && s.sendInput(proto::enc::kClientCmd, model::statRowClickBody(w, ref, c));
                    std::fprintf(stderr, "[input] stat row %d \"%s\" %s: over=%u/%u control=%s %s\n", rj->row, w.stat.lines[size_t(rj->row)].name.c_str(),
                                 rj->dbl ? "dblclick" : "click", ref.kind, ref.ref, c.control.c_str(), !wanted ? "not subscribed" : ok ? "sent" : "NOT SENT");
                } else {
                    std::fprintf(stderr, "[input] stat row %d is not clickable\n", rj->row);
                }
            } else if (auto* tj = std::get_if<TopicJob>(&j)) {
                bool ok = s.sendInput(proto::enc::kTopicMsg, proto::enc::encodeTopic(tj->href));
                std::fprintf(stderr, "[input] topic %s %s\n", tj->href.substr(0, 120).c_str(), ok ? "sent" : "NOT SENT");
            }
        }
    };

    auto nextTick = std::chrono::steady_clock::now();
    mapPace.next = nextTick;
    size_t refusedLogged = 0;
    uint64_t framesAtLastMap = ~0ull;
    std::set<uint32_t> refusedSeen;
    auto nextAudioLog = std::chrono::steady_clock::now();
    std::string lastAudioLine;
    // Each s2c 0x0039 answer completes the oldest resolve in flight.
    auto drainListReplies = [&] {
        uint8_t kind; uint32_t value;
        while (im.takeListReply(kind, value)) {
            if (resolving.empty()) { std::fprintf(stderr, "[input] s2c 0x0039 %02x:%u with no list resolve in flight - dropped\n", kind, value); continue; }
            Resolving rv = std::move(resolving.front());
            resolving.pop_front();
            std::fprintf(stderr, "[input] command \"%s\": 0x0039 answered %02x:%u\n", rv.line.c_str(), kind, value);
            if (rv.prompt && prompts.withdrawn(rv.promptId, false)) {
                std::fprintf(stderr, "[input] prompt %u withdrawn by the server - answer not sent\n", rv.promptId);
                continue;
            }
            if (rv.prompt) {
                model::PromptReply rep = model::PromptBook::resolvedInput(rv.promptId, kind, value);
                bool ok = s.sendInput(rep.type, rep.body);
                std::fprintf(stderr, "[input] prompt %u answer: 0x%04x len=%zu %s\n", rv.promptId, rep.type, rep.body.size(), ok ? "sent" : "NOT SENT");
                continue;
            }
            runResult(rv.line, net::InputModel::completeResolve(rv.r, kind, value, m.world().haveSelf, argAtoms));
        }
    };
    pumpPrompts = [&] {
        for (PromptFlow::Step& st : promptFlow.pump(std::chrono::steady_clock::now())) {
            switch (st.kind) {
            case PromptFlow::Step::Show:
                if (st.gaveUp) std::fprintf(stderr, "[prompt] input id=%u: no choices came - typed text only\n", st.id);
                emitPrompt(*st.prompt);
                break;
            case PromptFlow::Step::Send: {
                bool ok = s.sendInput(st.type, st.body);
                std::fprintf(stderr, "[prompt] id=%u: 0x%04x %s\n", st.id, st.type, ok ? "sent" : "NOT SENT");
                break;
            }
            case PromptFlow::Step::VerbArgGaveUp:
                std::fprintf(stderr, "[prompt] verb-argument id=%u: no choices came - typed text only\n", st.id);
                if (auto it = argHeld.find(st.id); it != argHeld.end()) { emit promptRequested(it->second); argHeld.erase(it); }
                break;
            default: break;
            }
        }
    };
    while (!stop_ && !reconnect_ && !s.stats().serverClosed && !s.stats().rebootRequested) {
        drainInbox();
        pumpPrompts();
        drainListReplies();
        for (size_t i = 0; i < rscUnresolved.size();) { if (declareRsc(rscUnresolved[i])) rscUnresolved.erase(rscUnresolved.begin() + long(i)); else i++; }
        if (!rscWanted.empty()) { s.resources().prioritize(rscWanted); rscWanted.clear(); }
        sounds.pump(nowMs());           // plays held for their resource bytes (SoundFeed::kPendingTtlMs)
        s.resources().prioritize(sounds.takeWanted());
        feed.pump(nowMs(), winReply);   // held winget queries: answered the moment the layout is measured, or empty at 3 s
        {   // wake for the next map frame, never later than 30 ms (the socket is serviced in between)
            auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(mapPace.next - std::chrono::steady_clock::now()).count();
            s.run(std::chrono::milliseconds(std::clamp<long long>(wait, 1, 30)));
        }
        feed.pump(nowMs(), winReply);
        auto now = std::chrono::steady_clock::now();
        if (now >= mapPace.next) {
            // The map runs at its own cadence (MapCadence), not the UI tick: a glide or animate() step is drawn at the
            // instant it is assembled, so the hand-off rate is the motion's frame rate.
            {
                int64_t wall = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                bool fresh = framesIngested.load() != framesAtLastMap;
                framesAtLastMap = framesIngested.load();
                mapPace.assembled(now, fresh || mapInMotion(m.world(), wall));
            }
            // Map: assemble on this thread (the model's only thread), execute on RenderThread.
            render::DrawList dl;
            render::SceneStats ss; ss.profilePreparation = profilePreparation;
            std::string aerr;
            int64_t clockNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (res.refusedRecipes().size() != refusedLogged) {
                // An undrawable icon makes its atom invisible: log each refused recipe once.
                // FUGUE_DUMP_REFUSED=<dir> also writes the refused bytes there.
                static const QByteArray dumpDir = qgetenv("FUGUE_DUMP_REFUSED");
                for (const auto& [rid, why] : res.refusedRecipes()) {
                    if (!refusedSeen.insert(rid).second) continue;
                    std::fprintf(stderr, "[icon] UNDRAWABLE res %08x: %s\n", rid, why.c_str());
                    if (!dumpDir.isEmpty())
                        if (const Bytes* b = res.bytesOf(rid))
                            if (FILE* f = std::fopen((dumpDir.toStdString() + "/" + QString::number(rid, 16).rightJustified(8, '0').toStdString()).c_str(), "wb")) {
                                std::fwrite(b->data(), 1, b->size(), f);
                                std::fclose(f);
                            }
                }
                refusedLogged = res.refusedRecipes().size();
            }
            // The default map's style is the maptext sheet (.maptext, .center, .black_outline), shared with MeasureText.
            const auto prepareStart = std::chrono::steady_clock::now();
            bool assembled = render::assembleMapControl(m, res, clockNs, dl, ss, &aerr, /*withHit=*/true, feed.live().defaultMapStyle(), &sceneCache);
            const double prepareMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - prepareStart).count();
            if (profilePreparation) tracePreparation({}, clockNs, prepareMs, ss, dl, assembled);
            const bool clientHover = im.clientHover() || im.clientMove();   // the map widgets post button-less motion only then (MapWidget::wantsHover)
            // The scene is this thread's own until it is submitted.
            if (assembled && dl.hit) std::const_pointer_cast<render::HitScene>(dl.hit)->clientHover = clientHover;
            s.resources().prioritize(res.takeWanted());   // what this frame could not draw loads first
            if (assembled) {
                if (!dlLogged) { dlLogged = true; log("first draw list " + std::to_string(dl.outW) + "x" + std::to_string(dl.outH) + " unmodeled=" + ss.summary()); }
                rt_->submit(std::move(dl), prepareMs);
            } else if (aerr != lastAssembleErr) {
                lastAssembleErr = aerr;
                log("assemble: " + aerr);
            }
            // The secondary map controls (a .dmf's other MAPs, and the ones a winset created: vg's camera console): one list
            // each, same clock.
            for (const std::string& id : feed.live().secondaryMaps()) {
                render::DrawList al;
                render::SceneStats as; as.profilePreparation = profilePreparation;
                std::string e2;
                const auto auxStart = std::chrono::steady_clock::now();
                const bool auxAssembled = render::assembleSecondaryMap(m, res, clockNs, id, al, as, &e2, feed.live().defaultMapStyle(), &sceneCache,
                                                                       /*withHit=*/true);
                const double auxMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - auxStart).count();
                if (profilePreparation) tracePreparation(id, clockNs, auxMs, as, al, auxAssembled);
                if (auxAssembled) {
                    if (al.hit) std::const_pointer_cast<render::HitScene>(al.hit)->clientHover = clientHover;
                    rt_->submitAux(id, std::move(al), auxMs);
                    lastAuxErr.erase(id);
                } else if (auto& last = lastAuxErr[id]; e2 != last) {   // said once per new error, as the main map's
                    last = e2;
                    log("assemble " + id + ": " + e2);
                }
            }
            s.resources().prioritize(res.takeWanted());
        }
        if (now < nextTick) continue;
        nextTick = now + std::chrono::milliseconds(kTickMs);
        const model::World& w = m.world();

        // Tell the GUI once when the skin (0x00af text or 0x00ef .dmf resource) has loaded.
        if (feed.counters().skinLoads != skinLoadsSeen) { skinLoadsSeen = feed.counters().skinLoads; emit skinLoaded(); }
        syncBrowseNames();
        if (QString n = QString::fromStdString(w.name); n != lastName) { lastName = n; emit worldName(n); }
        if (w.tickNanos != lastTickNs || w.frameNanos != lastFrameNs) {
            lastTickNs = w.tickNanos;
            lastFrameNs = w.frameNanos;
            emit frameTiming(lastTickNs, lastFrameNs);
        }
        // The main window icon is re-resolved when its value changes, and on every tick until its file arrives.
        if (skin::IconResolver::Out io; icon.update(feed.live().mainIcon(), io)) {
            log("window icon \"" + io.value + "\" -> " + (!io.bytes.empty() ? std::to_string(io.bytes.size()) + " bytes" : std::string(io.none ? "none" : "not here yet")));
            emit windowIcon(QString::fromStdString(io.name), QByteArray(io.bytes.data(), int(io.bytes.size())));
        }
        s.resources().prioritize(icon.takeWanted());

        flushChat();
        // Audio status is logged only when it changes.
        if (now >= nextAudioLog) {
            nextAudioLog = now + std::chrono::seconds(5);
            audio::Player::Stats as = player.stats();
            std::string line = "played=" + std::to_string(as.played) + " stops=" + std::to_string(as.stops) + " pending=" + std::to_string(sounds.pending()) +
                               " expired=" + std::to_string(sounds.expired()) + " live=" + std::to_string(as.live);
            for (const auto& [k, n] : as.byFormat) line += " " + k + "=" + std::to_string(n);
            for (const auto& [k, n] : as.unsupported) line += " UNDECODABLE-" + k + "=" + std::to_string(n);
            if (as.failed) line += " decode-failures=" + std::to_string(as.failed);
            if (line != lastAudioLine) {
                lastAudioLine = line;
                audio::note(line + " non-silent-blocks=" + std::to_string(as.peakBlocks) + " sink=" + player.sinkName());
            }
        }
        int64_t statClock = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (shell::StatView sv = buildStatView(m, res, statClock); !lastStatView || !(sv == *lastStatView)) { lastStatView = sv; emit statView(sv); }
        for (const auto& [name, b] : w.browsers) {
            auto it = lastBrowse.find(name);
            if (it == lastBrowse.end() || it->second != b.html) { lastBrowse[name] = b.html; emit browseHtml(QString::fromStdString(name), QString::fromStdString(b.params), QString::fromStdString(b.html)); }
        }
        for (auto it = lastBrowse.begin(); it != lastBrowse.end();) {
            if (!w.browsers.count(it->first) && !fileBrowse.count(it->first)) { emit browseClosed(QString::fromStdString(it->first)); it = lastBrowse.erase(it); }
            else ++it;
        }
        for (const skin::LiveSkin::CloneAction& a : live->cloneActions(cloneOpen)) {   // a winclone()d window: open while shown and holding a page
            if (a.open) emit browseHtml(QString::fromStdString(a.name), QString::fromStdString(a.params), QString::fromStdString(a.html));
            else emit browseClosed(QString::fromStdString(a.name));
        }

    }
    flushChat();   // a closing 0x0000's line arrives with the close
    const net::SessionStats& st = s.stats();
    out.accepted = st.sawSelfRef;
    out.reboot = st.rebootRequested;
    out.refused = m.world().sessionEnd.seen;
    out.why = out.refused ? m.world().sessionEnd.text : st.closeReason;
    noteSessionEnd(s.endSummary() + (out.refused ? "; the server said: " + out.why : ""));
    return out;
}

bool SessionWorker::spaceCompletion(const QString& line, QString& out) const {
    std::shared_ptr<const fugue::net::InputModel> snap;
    {
        std::lock_guard<std::mutex> g(verbsMu_);
        snap = verbSnap_;
    }
    std::string r;
    if (!snap || !snap->spaceCompletion(line.toStdString(), r)) return false;
    out = QString::fromStdString(r);
    return true;
}
