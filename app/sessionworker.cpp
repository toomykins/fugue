#include "app/sessionworker.h"
#include "net/textmacro.h"
#include "app/mapframeclock.h"
#include "shell/turfmap.h"
#include "shell/entitymap.h"
#include "shell/mapmenu.h"
#include "shell/namedmap.h"
#include "shell/statview.h"
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
#include "app/choicefetches.h"
#include "app/defaultskin.h"
#include "audio/player.h"
#include "audio/note.h"
#include "model/browsing.h"
#include "model/prompt.h"
#include "model/resync.h"
#include "model/appgc.h"
#include "model/statpanel.h"
#include "model/verbmenu.h"
#include "model/world.h"
#include "net/input.h"
#include "net/reconnect.h"
#include "pager/target.h"
#include "proto/enc_c2s_0002_input.h"
#include "proto/msg.h"
#include "shell/chaticons.h"
#include "shell/ttmetrics.h"
#include <QBuffer>
#include <QUrlQuery>
#include "shell/recipefiler.h"
#include "skin/skinfeed.h"
#include "skin/winicon.h"

using namespace fugue;

namespace {
constexpr int kTickMs = 90;  // the UI tick: chat, stat panel, browse windows

}  // namespace

void SessionWorker::post(Job j) {
    std::lock_guard<std::mutex> g(inMu_);
    if (inbox_.size() < 4096)
        inbox_.push_back(std::move(j));  // bounded, so a stalled session cannot grow it without limit
}
void SessionWorker::postCommand(const QString& line, bool promptNull) { post(CmdJob{line.toStdString(), promptNull}); }
void SessionWorker::postStockMove(int dir) { post(MoveJob{dir}); }
void SessionWorker::postMapPointer(const shell::MapPointer& pointer) {
    std::lock_guard<std::mutex> g(inMu_);
    // Coalesce only consecutive motion on the same rendered identity. Never
    // coalesce across a target transition, leave, click or other input job.
    if (pointer.kind == shell::MapPointer::Hover && !inbox_.empty())
        if (auto* last = std::get_if<MapPointerJob>(&inbox_.back());
            last && last->pointer.kind == shell::MapPointer::Hover && shell::sameMapTarget(last->pointer, pointer)) {
            last->pointer = pointer;
            return;
        }
    // The widget hands over every pick and every leave; a leave after a leave says nothing new.
    if (pointer.kind == shell::MapPointer::Leave && !inbox_.empty())
        if (auto* last = std::get_if<MapPointerJob>(&inbox_.back()); last && last->pointer.kind == shell::MapPointer::Leave)
            return;
    if (inbox_.size() >= 4096) {
        // A stalled connection must not retain an old server hover. Collapse
        // excess hover history to a reset followed by the latest observation.
        std::erase_if(inbox_, [](const Job& j) {
            const auto* p = std::get_if<MapPointerJob>(&j);
            return p && (p->pointer.kind == shell::MapPointer::Hover || p->pointer.kind == shell::MapPointer::Leave);
        });
        auto leave = pointer;
        leave.kind = shell::MapPointer::Leave;
        inbox_.push_back(MapPointerJob{leave});
    }
    if (inbox_.size() < 4098) inbox_.push_back(MapPointerJob{pointer});
}
void SessionWorker::postStatHover(int row, const std::string& control, int mods, quint64 generation, bool icon,
                                  int iconX, int iconY) {
    shell::StatHover h{row, control, generation, icon, iconX, iconY, {bool(mods & 1), bool(mods & 2), bool(mods & 4)}};
    std::lock_guard<std::mutex> g(inMu_);
    // Steps within one row say nothing new until the session has taken them: keep the last.
    if (!inbox_.empty())
        if (auto* last = std::get_if<StatHoverJob>(&inbox_.back());
            last && !last->leave && last->hover.row == row && last->hover.generation == generation &&
            last->hover.control == control) {
            last->hover = std::move(h);
            return;
        }
    if (inbox_.size() < 4096) inbox_.push_back(StatHoverJob{false, std::move(h)});
}
void SessionWorker::postStatHoverLeft() { post(StatHoverJob{true, {}}); }
void SessionWorker::postMapMenuPick(quint64 token, int index) { post(MapPickJob{token, index}); }
void SessionWorker::postTopic(const QString& href) { post(TopicJob{href.toStdString()}); }
void SessionWorker::postPromptAnswer(quint32 id, bool alert, const QString& text) {
    post(PromptJob{id, alert, false, text.toStdString()});
}
void SessionWorker::postPromptCancel(quint32 id, bool alert) { post(PromptJob{id, alert, true, {}}); }
void SessionWorker::postArgAnswer(quint32 id, const QString& text) { post(ArgJob{id, false, text.toStdString()}); }
void SessionWorker::postArgCancel(quint32 id) { post(ArgJob{id, true, {}}); }
void SessionWorker::postArgNone(quint32 id) { post(ArgJob{id, false, {}, true}); }
void SessionWorker::postStatTab(int index) { post(StatTabJob{index}); }
void SessionWorker::postStatRow(int row, const std::string& control, const std::string& button, int mods, bool dbl,
                                quint64 generation, bool icon, int iconX, int iconY) {
    post(StatRowJob{row, control, button, mods, dbl, generation, icon, iconX, iconY});
}

void SessionWorker::run() {
    std::string dir = cfg_.stateDir.empty() ? fugue::net::defaultStateDir() : cfg_.stateDir;
    if (!dir.empty()) {
        // Status also goes to files, since a desktop-menu launch has no terminal (audio/note.h).
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string nerr;
        if (!audio::setNoteFile(dir + "/audio.log", &nerr))
            std::fprintf(stderr, "[session] audio status file: %s\n", nerr.c_str());
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
        if (live_)
            for (const auto& name : live_->clearRuntimeBrowsers()) {
                openPopups_.erase(name);
                emit browseClosed(QString::fromStdString(name));
            }
        e.afterLink = viaLink;
        viaLink = !linkTarget_.empty();
        if (stop_) return;
        net::ReconnectStep step = net::reconnectStep(e, first, reconnect_);
        if (viaLink) {  // at most one a second, so worlds linking to each other (or to themselves) cannot spin the client
            reconnect_ = false;
            const auto age =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                    .count();
            step.delayMs = int(std::max<int64_t>(0, 1000 - age));
            const size_t colon = linkTarget_.rfind(':');
            const std::string host = linkTarget_.substr(0, colon);
            const uint16_t port = uint16_t(std::atoi(linkTarget_.c_str() + colon + 1));
            linkTarget_.clear();
            if (host != cfg_.host || port != cfg_.port) {
                cfg_.host = host;
                cfg_.port = port;
                std::lock_guard<std::mutex> g(verbsMu_);
                verbSnap_.reset();  // the old world's verbs, until the new world's Verb entries
                emit worldAddress(QString::fromStdString(host), port);
            }
        }
        sameConnection = step.sameConnection;
        if (!sameConnection) s->close();
        for (const QString& l : QString::fromStdString(step.message).split('\n', Qt::SkipEmptyParts))
            emit localOutput(l);
        int ms = step.delayMs;
        if (ms < 0) {  // only .reconnect starts another
            while (!stop_ && !reconnect_) QThread::msleep(50);
        } else {
            for (auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
                 !stop_ && !reconnect_ && std::chrono::steady_clock::now() < end;)
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
    ++mapSession_;
    ++mapMenuToken_;
    struct MapChoice {
        model::HitTarget hit;
        uint32_t verb;
        bool argument;
        std::string name;
        uint16_t key = 0;
        bool haveKey = false;
        uint64_t lifetime = 0;
        std::optional<shell::MapPointer> attachment;
    };
    std::vector<MapChoice> mapChoices;
    int menuZ = 0;
    emit mapMenuReady(shell::MapMenu{});  // close any popup from the old world
    emit mapViewSize(0, 0);
    emit hoverName({});
    if (mapMetrics_) mapMetrics_->reset();
    if (mapMailbox_ && mapMailbox_->publish(QImage(), QRect(), 0, 0)) emit mapFrameAvailable();
    shell::TurfMap turfMap;
    shell::EntityMap entityMap;
    struct NamedScene {
        explicit NamedScene(const QString& name) : scene(name) {}
        shell::NamedMap scene;
        shell::NamedMap::Frame last;
    };
    std::map<uint64_t, std::unique_ptr<NamedScene>> namedScenes;
    if (mapRoutes_)
        for (const auto& route : mapRoutes_->snapshot())
            if (route.mailbox->publish({}, {}, 0, 0)) emit mapFrameAvailable();
    shell::MapVisibility mapVisibility;
    const auto traceSelector = qgetenv("FUGUE_ATOM_TRACE");
    auto atomTrace = traceSelector.isEmpty()
                         ? std::optional<shell::AtomTrace>{}
                         : shell::AtomTrace::parse(traceSelector, qgetenv("FUGUE_ATOM_TRACE_INFO_ROW"));
    if (!traceSelector.isEmpty() && !atomTrace)
        std::fprintf(
            stderr,
            "[atom-trace] invalid selector; expected obj:<decimal> or mob:<decimal>, optional INFO row 0..65535\n");
    if (atomTrace) atomTrace->inspectPixels = qgetenv("FUGUE_ATOM_TRACE_PIXELS") == "1";
    shell::AtomTraceOutput atomTraceOutput;
    uint64_t diagnosticMapPublication = 0;
    int diagnosticPublishedSprites = -1;  // no mailbox publication observed yet
    entityMap.setDiagnostics(atomTrace ? &*atomTrace : nullptr);
    shell::TurfMap::Frame lastMapFrame;
    shell::MapSprites lastSprites;
    int lastViewW = 0, lastViewH = 0;
    auto log = [](const std::string& m) { std::fprintf(stderr, "[session] %s\n", m.c_str()); };
    {
        std::lock_guard<std::mutex> g(inMu_);
        inbox_.clear();
    }
    s.setManualPrompts(true);  // the GUI's PromptHost answers alert()/input(); never auto-cancel them
    PromptFlow promptFlow;     // pending dialogs and the choices request in flight (app/choicefetches.h)
    model::PromptBook& prompts = promptFlow.book;
    // Missing-verb-argument dialogs held for the GUI, by a client-local id. An entry leaves on the dialog's one outcome
    // and dies with this session.
    std::map<quint32, net::MissingArg> pendingArgs;
    std::map<quint32, net::SrcChoice> pendingSrcs;  // `<Verb> word` src dialogs, same id space and lifetime
    quint32 nextArgId = 1;
    std::map<quint32, shell::PromptRequest>
        argHeld;  // verb-argument dialogs held for their ExpansionReply (by pendingArgs id)
    // Verb-argument dialogs whose listbox the ExpansionReply filled: the listbox shows the choices as
    // model::parseExpansionReply decodes them, and a pick goes back in the ListResolve as net::listPickText.
    std::set<quint32> argServerLists;
    // List resolves in flight (C2S::ListResolve sent, S2C::ResolveReply awaited), in SEND order: the answer names no
    // request, so the oldest owns it. Stock DreamSeeker sends the verb when the answer arrives and not before
    // (net::InputModel::completeResolve). An input() in list's pick resolves on the same S2C::ResolveReply
    // (model::Prompt::resolvesThroughServer), so it queues here too.
    struct Resolving {
        net::ListResolve r;
        std::string line;
        bool prompt = false;
        quint32 promptId = 0;
    };
    std::deque<Resolving> resolving;
    model::Session m;
    // Atom-table resync poll (C2S::ResyncMob / ResyncObj, model/resync.h): after every ingested frame, and from the
    // pump hook.
    model::ResyncPoller resync;
    model::AppearanceGc appearanceGc;  // frees idle appearances and reports them in C2S::AppearancesFreed
    auto pumpResync = [&] {
        int64_t now =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        for (const auto& f : resync.poll(m.worldMut(), now)) s.sendInput(f.type, f.body);
        for (const auto& f : appearanceGc.step(m)) s.sendInput(f.type, f.body);
    };
    std::function<void()> pumpPrompts;  // PromptFlow::pump: once the frames read are handled, and after each answer
    s.setPumpHook([&] {
        pumpResync();
        if (pumpPrompts) pumpPrompts();
    });
    net::InputModel im;  // verbs + the mouse subscription mask (Verb / MouseSubs), session thread only
    shell::MapHover hover;
    std::string statusName;  // the last hoverName sent
    auto steadyNs = [] {
        return int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    auto sendHover = [&] {
        const auto now = steadyNs();
        for (auto body = hover.due(now); !body.empty(); body = hover.due(now))  // reports due together, in order
            s.sendInput(C2S::Command, proto::enc::mouseWireBody(std::move(body), s.wideVerbIds()));
    };
    // One hover pick (a map's or the stat panel's) into the session's one record, gated by the MouseSubs bits and the
    // appearance flags of what it names and of what the last report named.
    auto updateHover = [&](const shell::MapPointer& p, bool current) {
        const auto& w = m.world();
        auto flagsOf = [&](const shell::MapPointer& named) -> uint32_t {
            std::optional<uint32_t> appid = named.info ? std::optional<uint32_t>(named.infoAppid) : std::nullopt;
            if (!named.info)
                if (const auto target = shell::mapPointerTarget(named, w); target) appid = target->appid;
            if (!appid) return 0;
            const auto a = w.appearances.find(*appid);
            return a == w.appearances.end() ? 0 : a->second.fields.appearanceFlags;
        };
        shell::MapHover::Gate gate{im.clientHover(), im.clientMove()};
        if (current && p.kind == shell::MapPointer::Hover) gate.flags = flagsOf(p);
        if (const auto* last = hover.last()) gate.sentFlags = flagsOf(*last);
        hover.update(p, current, gate, steadyNs());
        sendHover();
    };
    bool rightPressSent = false;  // the right press in progress went to the wire (shell::mapPopupAction)
    // The live skin: skin and winset traffic goes in, and wingets are answered from it. Shared with the GUI.
    std::shared_ptr<skin::LiveSkin> live = live_ ? live_ : std::make_shared<skin::LiveSkin>();
    skin::SkinFeed feed(live);
    feed.setDefaultSkin(app::kDefaultSkin);  // a world that sends no skin gets this one
    feed.setCommandSink([this](const std::string& c) { emit worldCommand(QString::fromStdString(c)); });
#ifdef FUGUE_RELEASE
    feed.setClientVersion(kVersionRelease);  // winget "fugue-version"
#endif
    // winget "url" is the address the client dialed, as "IP:port" (stock DreamSeeker 516 does the same).
    live->setClientEnv("url", cfg_.host + ":" + std::to_string(cfg_.port));
    m.setTextTarget([live](const std::string& c) { return live->printsOutput(c); });
    auto nowMs = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
    auto winReply = [&](uint32_t reqId, const std::string& v) {
        s.replyWinget(reqId, v);
        std::fprintf(stderr, "[skin] winget req %u -> \"%s\"\n", reqId, v.substr(0, 120).c_str());
    };
    std::map<std::string, std::string> lastBrowse;  // window -> the html the GUI was last sent
    std::map<std::string, uint64_t> cloneOpen;      // winclone()d window -> the page seq the GUI was last sent
    // Windows opened by a browse_file offer (never seen as Resource), and the popups still open when the last session
    // ended: stock DreamSeeker keeps them open across a reconnect, a reboot and a link().
    std::set<std::string> fileBrowse = openPopups_;
    std::vector<model::RscRecord> rscUnresolved;  // BrowseRsc records whose ordinal is not yet in the ResourceList
    auto syncBrowseNames = [&] {
        std::set<std::string> names = fileBrowse;
        for (const auto& [n, b] : m.world().browsers) names.insert(n);
        openPopups_ = names;
        feed.setBrowseNames(std::move(names));
    };
    // A browse_rsc file that a page is about to load jumps the declaration queue: stock DreamSeeker 516 answers a
    // BrowseRsc for an undownloaded kind-0 file with a ResourceDeclare for that ordinal, ahead of anything else queued.
    // Collected per frame and handed over in one prioritize() call.
    std::vector<uint32_t> rscWanted;
    auto declareRsc = [&](const model::RscRecord& r) {
        const auto& list = m.world().resources;
        if (r.ordinal >= list.size()) return false;
        uint32_t id = list[r.ordinal].first;
        if (rsc_) rsc_->declare(r.name, id);  // BrowseRsc's u32 is an ORDINAL into the ResourceList, not an id
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
        std::fprintf(stderr, "[prompt] %s id=%u title=\"%s\" %s%s -> GUI\n", r.alert ? "alert" : "input", p.id,
                     p.title.c_str(), r.alert ? "" : p.typeName().c_str(), p.inList ? " in list" : "");
        emit promptRequested(r);
    };
    // S2C::Sound bypasses the model: the feed resolves its ordinal against the ResourceList as it stands after
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
    s.setObserver([&](uint16_t type, ByteSpan body) {
        if (type == S2C::Resource) return;  // resource-pump frames are not model input
        if (rec_) rec_->s2c(type, body);
        // The handshake precedes this observer, including on a reboot/relogin.
        // Set both consumers before the first verb/appearance frame can arrive.
        im.setWideVerbIds(s.wideVerbIds());
        m.setWideVerbIds(s.wideVerbIds());
        if (type == S2C::Alert || type == S2C::Input || type == S2C::ExpansionReply || type == S2C::PromptWithdraw ||
            type == S2C::ProbeEcho) {
            // alert()/input() requests, PromptWithdraw, the ExpansionReply to a choices request and the ProbeEcho
            // behind it. Dialogs open and requests go out from pumpPrompts.
            PromptFlow::Step st = promptFlow.onFrame(type, body);
            if (st.replaced) emit promptWithdrawn(st.id, st.alert);
            switch (st.kind) {
                case PromptFlow::Step::Withdraw:
                    std::fprintf(stderr, "[prompt] %s id=%u withdrawn by the server (PromptWithdraw)\n",
                                 st.alert ? "alert" : "input", st.id);
                    emit promptWithdrawn(st.id, st.alert);
                    break;
                case PromptFlow::Step::Dropped:
                    if (st.abandoned)
                        std::fprintf(stderr,
                                     "[prompt] late ExpansionReply for timed-out verb-argument id=%u - dropped\n",
                                     st.id);
                    else
                        std::fprintf(stderr, "[prompt] ExpansionReply for no in-list prompt of ours - dropped\n");
                    break;
                case PromptFlow::Step::VerbArgChoices:
                    if (auto it = argHeld.find(st.id); it != argHeld.end()) {
                        shell::PromptRequest r = std::move(it->second);
                        argHeld.erase(it);
                        std::vector<std::string> ch;
                        if (model::parseExpansionReply(body, ch))
                            for (const std::string& c : ch) r.choices << QString::fromStdString(c);
                        argServerLists.insert(r.id);
                        std::string raw;
                        for (const std::string& c : ch) raw += (raw.empty() ? "" : "|") + c;
                        std::fprintf(stderr, "[prompt] verb-argument id=%u: %d choices from ExpansionReply (%s)\n",
                                     r.id, int(r.choices.size()), raw.substr(0, 200).c_str());
                        emit promptRequested(r);
                    }
                    break;
                default: break;
            }
        }
        im.onFrame(type, body);
        if (type == S2C::Verb) {
            auto snap = std::make_shared<const net::InputModel>(im);
            std::lock_guard<std::mutex> g(verbsMu_);
            verbSnap_ = std::move(snap);
        }
        // Timestamp model updates with the current wall clock.
        m.setIngestClockNs(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        m.ingest(type, body);
        if (type == S2C::Timing) s.wake();  // recompute the map deadline immediately on a client.fps change
        pumpResync();
        if (rec_) {
            const model::World& w = m.world();
            auto self = w.haveSelf ? w.atoms.find(w.selfRef | model::kRegistryA) : w.atoms.end();
            rec_->setEye(w.haveEye ? w.eyeX : 0, w.haveEye ? w.eyeY : 0,
                         self != w.atoms.end() && self->second.hasPos ? self->second.z : w.grid.z);
        }
        feed.onFrame(type, body);  // SkinText / SkinResource / Winset
        if (type == S2C::Sound) sounds.onFrame(body, nowMs());
        if (type == S2C::Browse) {
            // Every browse is delivered, even an unchanged body: stock DreamSeeker treats an unchanged body as no change
            // and does not reopen a window the player closed.
            model::BrowseFrame bf;
            if (model::parseBrowseFrame(body, bf)) {
                std::string name = model::parseBrowseParams(bf.params).window;
                if (bf.close) {
                    fileBrowse.erase(name);
                    lastBrowse.erase(name);
                    emit browseClosed(QString::fromStdString(name));
                } else {
                    lastBrowse[name] = bf.html;
                    emit browseHtml(QString::fromStdString(name), QString::fromStdString(bf.params),
                                    QString::fromStdString(bf.html), live->propertyRevision());
                }
            }
            syncBrowseNames();
        } else if (type == S2C::Output) {
            model::OutputFrame of;
            model::PageCall pc;
            if (model::parseOutputFrame(body, of) && model::parsePageCall(of.control, of.value, pc)) {
                QStringList args;
                for (const std::string& a : pc.args) args << QString::fromStdString(a);
                emit pageCall(QString::fromStdString(pc.target), QString::fromStdString(pc.fn), args);
            }
        } else if (type == S2C::Link) {
            model::LinkFrame lf;
            if (model::parseLinkFrame(body, lf)) {
                if (lf.kind == model::LinkFrame::Topic) {
                    bool ok = s.sendInput(C2S::Topic, proto::enc::encodeTopic(lf.url));
                    std::fprintf(stderr, "[link] topic %s %s\n", lf.url.substr(0, 120).c_str(),
                                 ok ? "sent" : "NOT SENT");
                } else if (lf.kind == model::LinkFrame::External) {
                    std::fprintf(stderr, "[link] open %s\n", lf.url.substr(0, 200).c_str());
                    emit externalLink(QString::fromStdString(lf.url));
                } else if (lf.kind == model::LinkFrame::World) {
                    // Stock DreamSeeker joins the linked world in the same window, whichever world it is (pager/target.h
                    // refuses a hub name or a missing port). An identical skin keeps every control's state across it.
                    std::string why;
                    std::optional<std::string> t = pager::parseTarget(lf.url, &why);
                    std::fprintf(stderr, "[link] world %s: %s\n", lf.url.substr(0, 200).c_str(),
                                 t ? ("joining " + *t + " in this window").c_str() : why.c_str());
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
        } else if (type == S2C::BrowseRsc) {
            for (auto& r : model::parseBrowseRscFrame(body))
                if (!declareRsc(r)) rscUnresolved.push_back(r);
            s.resources().prioritize(rscWanted);
            rscWanted.clear();
        }
        framesIngested++;
    });
    // Main window icon (skin/winicon.h): this world's files by name, and its ResourceList by ordinal.
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
    if (rec_) {
        rec_->newSession();
        s.setSendTap([this](uint16_t type, ByteSpan body) { rec_->c2s(type, body); });
    }
    s.setResourceObserver([&](uint32_t id, const std::string& name, ByteSpan data) {
        if (rec_) rec_->resource(id, name, data);
        icon.noteResource(id, name);
        m.noteResource(id, data, name);
        // A font file among the world's resources is loaded for its maptext, as stock does (resource type 14 there).
        // Any font file that arrives counts, a page's own included: taking only ResourceList's type-14 entries did not
        // load tgstation's lobby face 'Grand9K Pixel' (why was not looked into).
        if (data.size() > 4 && (!std::memcmp(data.data(), "\0\1\0\0", 4) || !std::memcmp(data.data(), "OTTO", 4) ||
                                !std::memcmp(data.data(), "true", 4) || !std::memcmp(data.data(), "ttcf", 4)) &&
            shell::addResourceFont(QByteArray(reinterpret_cast<const char*>(data.data()), qsizetype(data.size()))))
            std::fprintf(stderr, "[maptext] font from resource %08x %s\n", id, name.c_str());
        if (rsc_ && !browseRecipes.onResource(id, name, data))
            rsc_->put(id, name, std::string(reinterpret_cast<const char*>(data.data()), data.size()));
        // browse(file(...)) arrives as a resource plus a directive in the offer's params (not as a Browse): open it as
        // a browse window, or file it under the alias a page will ask for.
        std::string opts;
        if (model::browseFileOptions(s.resources().offerParams(id), opts)) {
            model::BrowseParams bp = model::parseBrowseParams(opts);
            if (!bp.window.empty()) {
                fileBrowse.insert(bp.window);
                syncBrowseNames();
                std::string html(reinterpret_cast<const char*>(data.data()), data.size());
                lastBrowse[bp.window] = html;
                log("browse_file \"" + name + "\" -> window " + bp.window + " (" + std::to_string(data.size()) +
                    " bytes)");
                emit browseHtml(QString::fromStdString(bp.window), QString::fromStdString(opts),
                                QString::fromStdString(html), live->propertyRevision());
            } else if (std::string alias = model::browseFileAlias(opts); !alias.empty() && rsc_) {
                rsc_->alias(alias, id);
                log("browse_file \"" + name + "\" filed as " + alias);
            }
        } else if (std::string ctl; model::outputFileControl(s.resources().offerParams(id), ctl)) {
            log("output_file \"" + name + "\" -> control " + ctl + " (" + std::to_string(data.size()) + " bytes)");
            emit outputFile(QString::fromStdString(ctl), QString::fromStdString(std::string(
                                                             reinterpret_cast<const char*>(data.data()), data.size())));
        }
        feed.onResource(id, name, data);  // the skin may arrive as a .dmf resource named by SkinResource
    });
    shell::MapTextCache textMeasure;
    s.setTextMeasure(
        [&](const std::string& text, const std::string& style, uint16_t width) -> std::pair<uint16_t, uint16_t> {
            // Every request is answered: a refusal resumes the world's proc with null.
            const auto settings = live->defaultMapTextSettings();
            bool valid = true;
            const double points = settings.size.empty() ? 0 : QString::fromStdString(settings.size).toDouble(&valid);
            textMeasure.setStyle(
                {QString::fromStdString(settings.style), QString::fromStdString(settings.family), valid ? points : -1});
            const auto size =
                textMeasure.measure(QString::fromStdString(net::plainText(text)), QString::fromStdString(style), width);
            if (size.width() < 0) return {uint16_t(0xffff), uint16_t(0xffff)};
            return {uint16_t(size.width()), uint16_t(size.height())};
        });
    // Winget: the skin holds a query until it has a measured layout.
    s.setWingetHandler([&](ByteSpan body) {
        skin::WingetQuery q;
        if (!skin::parseWingetBody(body, q)) return;  // malformed: nothing is sent either
        feed.submitWinget(q, nowMs(), winReply);
    });
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
    uint64_t mapTextSkinGeneration = UINT64_MAX;
    uint64_t chatSeen = 0;
    shell::StatThumbnails statThumbnails;
    std::optional<shell::StatView> lastStatView;  // none yet: a session's first view replaces the last session's panel
    QString lastName;
    int64_t lastTickNs = 0, lastFrameNs = 0;
    // A line with `\icon` tags waits (in order) for their files; after kChatIconWaitMs the tags are dropped.
    QStringList chatHeld;
    std::optional<std::chrono::steady_clock::time_point> chatHeldSince;
    constexpr int kChatIconWaitMs = 10000;
    shell::IconCache chatIcons;  // the icons chat lines name, DDMI recipes evaluated
    // Icons a page asked for by resource reference (BrowseRscStore::wantIcon): each waits up to kChatIconWaitMs for
    // its file, like a chat icon, and is then answered with nothing.
    std::deque<std::pair<std::string, std::chrono::steady_clock::time_point>> pageIcons;
    shell::IconCache pageIconCache;
    auto servePageIcons = [&] {
        if (!rsc_) return;
        const auto nowT = std::chrono::steady_clock::now();
        for (std::string& key : rsc_->takeWantedIcons()) pageIcons.emplace_back(std::move(key), nowT);
        if (pageIcons.empty()) return;
        const model::World& w = m.world();
        std::vector<uint32_t> wanted;
        pageIconCache.begin(w.resources, [&](uint32_t id, QByteArray& bytes) {
            Bytes data;
            if (!s.resources().readStored(id, data)) {
                wanted.push_back(id);
                return false;
            }
            bytes = QByteArray(reinterpret_cast<const char*>(data.data()), qsizetype(data.size()));
            return true;
        });
        for (auto it = pageIcons.begin(); it != pageIcons.end();) {
            const std::string& key = it->first;
            const bool expired = nowT - it->second > std::chrono::milliseconds(kChatIconWaitMs);
            const auto mark = key.find('?');
            const skin::IconRef ref = skin::parseIconRef(key.substr(0, mark));
            const QUrlQuery query(QString::fromStdString(mark == std::string::npos ? std::string() : key.substr(mark + 1)));
            std::string png;
            bool answered = ref.kind != skin::IconRef::Ordinal;
            if (!answered && ref.ordinal < w.resources.size()) {
                using Status = shell::IconCache::Status;
                const auto found = pageIconCache.lookup(ref.ordinal);
                if (found.ready) {
                    const shell::DmiIcon& icon = pageIconCache.icon(found.id);
                    const QString name = query.queryItemValue("state", QUrl::FullyDecoded);
                    const QString moves = query.queryItemValue("movement").toLower();
                    const shell::DmiIcon::State* state = nullptr;
                    if (moves == "true" || moves == "1")
                        if (const auto moving = icon.moving.find(name); moving != icon.moving.end()) state = &moving->second;
                    if (!state) state = icon.state(name);
                    const int dir = query.hasQueryItem("dir") ? query.queryItemValue("dir").toInt() : 2;
                    const int frame = std::max(1, query.queryItemValue("frame").toInt());
                    const QImage image = state && !icon.sheet.isNull() ? icon.frame(icon.index(*state, dir ? dir : 2, frame - 1))
                                                                        : QImage();
                    if (!image.isNull()) {
                        QByteArray bytes;
                        QBuffer buffer(&bytes);
                        buffer.open(QIODevice::WriteOnly);
                        image.save(&buffer, "PNG");
                        png = bytes.toStdString();
                    }
                    answered = true;
                } else if (found.status != Status::Missing && found.status != Status::Deferred &&
                           found.status != Status::DependencyUnavailable)
                    answered = true;  // not a picture
            }
            if (answered || expired) {
                rsc_->putIcon(key, std::move(png));
                it = pageIcons.erase(it);
            } else
                ++it;
        }
        if (!wanted.empty()) s.resources().prioritize(wanted);
    };
    auto flushChat = [&] {
        const model::World& w = m.world();
        if (w.chatTotal != chatSeen) {
            size_t take = size_t(std::min<uint64_t>(w.chatTotal - chatSeen, w.chatHtml.size()));
            for (size_t i = w.chatHtml.size() - take; i < w.chatHtml.size(); i++)
                chatHeld << QString::fromStdString(w.chatHtml[i]);
            chatSeen = w.chatTotal;
        }
        if (chatHeld.isEmpty()) return;
        auto nowT = std::chrono::steady_clock::now();
        if (!chatHeldSince) chatHeldSince = nowT;
        bool expired = nowT - *chatHeldSince > std::chrono::milliseconds(kChatIconWaitMs);
        // The icon cache evaluates a DDMI recipe (a runtime /icon, as stock mints it) against its operand icons.
        std::vector<uint32_t> wanted;
        chatIcons.begin(w.resources, [&](uint32_t id, QByteArray& bytes) {
            Bytes data;
            if (!s.resources().readStored(id, data)) {
                wanted.push_back(id);
                return false;
            }
            bytes = QByteArray(reinterpret_cast<const char*>(data.data()), qsizetype(data.size()));
            return true;
        });
        auto lookup = [&](uint32_t ord) {
            shell::ChatIconLookup l;
            if (ord >= w.resources.size()) return l;
            using Status = shell::IconCache::Status;
            const auto found = chatIcons.lookup(ord);
            const bool waiting = found.status == Status::Missing || found.status == Status::Deferred ||
                                 found.status == Status::DependencyUnavailable;
            if (found.ready) {
                l.status = shell::ChatIconLookup::Ready;
                l.icon = &chatIcons.icon(found.id);
            } else if (waiting && !expired) {
                l.status = shell::ChatIconLookup::Pending;
            }
            return l;
        };
        QStringList ready;
        while (!chatHeld.isEmpty()) {
            bool pending = false;
            QString html = shell::expandChatIcons(chatHeld.first(), lookup, &pending);
            if (pending) break;
            ready << html;
            chatHeld.removeFirst();
            chatHeldSince = nowT;
        }
        if (chatHeld.isEmpty()) chatHeldSince.reset();
        if (!wanted.empty()) s.resources().prioritize(wanted);
        if (!ready.isEmpty()) emit chatLines(ready);
    };
    // ---- input: everything below runs on this thread, the socket's only writer ----
    // A verb's candidate srcs (model::verbSrcCandidates): client, own mob, the movables it can see, usr.contents,
    // client.screen, turfs in view, the own area. With one candidate the verb is sent; with several, the player is asked
    // (srcChoice), as stock DreamSeeker does.
    auto srcFor = [&](uint32_t id) {
        std::vector<net::SrcCandidate> out;
        const model::World& w = m.world();
        for (const model::VerbSrcCandidate& c : model::verbSrcCandidates(w, id, true)) {
            bool own = c.src.kind == proto::enc::kVerbSrcClient ||
                       (c.src.kind == proto::enc::kVerbSrcMob && w.haveSelf && c.src.ref == w.selfRef);
            out.push_back(
                {{c.src.kind, c.src.ref}, c.name, !own, std::nullopt, c.rank.dist, c.rank.cell, c.rank.carried});
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
            out.push_back(
                {{c.src.kind, c.src.ref}, c.name, !own, std::nullopt, c.rank.dist, c.rank.cell, c.rank.carried});
        }
        return out;
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
            std::fprintf(stderr, "[input] command \"%s\": %zu srcs carry it - asking \"%s\"\n", line.c_str(),
                         r.srcChoice->candidates.size(), r.srcChoice->title.c_str());
            emit promptRequested(pr);
            return;
        }
        if (r.resolve) {
            // An `in <list>` argument: the server names its value first, and the Command waits for that answer.
            bool ok = s.sendInput(C2S::ListResolve, net::encodeVerbExpansionRequest(r.resolve->at, r.resolve->text));
            std::fprintf(stderr, "[input] command \"%s\": argument %zu \"%s\" -> ListResolve %s\n", line.c_str(),
                         r.resolve->at.index + 1, r.resolve->text.c_str(), ok ? "sent" : "NOT SENT");
            if (ok) resolving.push_back({*r.resolve, line});
            return;
        }
        if (r.missing) {
            if (!pendingArgs.empty() || !pendingSrcs.empty()) {
                // One dialog at a time, so a held or auto-repeating macro key cannot stack them.
                std::fprintf(stderr, "[input] command \"%s\": an argument dialog is already open - ignored\n",
                             line.c_str());
                return;
            }
            net::ArgPrompt ap = net::argPrompt(*r.missing);
            shell::PromptRequest pr;
            pr.alert = false;
            pr.verbArg = true;
            pr.id = nextArgId++;
            pr.title = QString::fromStdString(ap.title);
            pr.message = QString::fromStdString(ap.message);
            pr.typeMask = ap.kind == net::ArgPrompt::Message ? model::kInputMessage
                          : ap.kind == net::ArgPrompt::Num   ? model::kInputNum
                                                             : model::kInputText;
            pr.inList = ap.kind == net::ArgPrompt::List;
            pr.allowNull = ap.allowNull;
            for (const std::string& c : ap.choices) pr.choices << QString::fromStdString(c);
            pendingArgs[pr.id] = *r.missing;
            std::fprintf(stderr, "[input] command \"%s\": argument %zu missing - prompting \"%s\"\n", line.c_str(),
                         r.missing->index + 1, ap.title.c_str());
            if (pr.inList && !r.missing->atoms) {
                // The listbox is filled by a VerbExpansion round trip addressed by the argument's ordinal. The dialog
                // is held until it answers (or ChoiceFetches::kWait passes), so it opens complete, as an input() in
                // list does.
                std::fprintf(stderr, "[input] verb expansion ordinal %u src %02x:%u: VerbExpansion queued\n",
                             ap.ordinal, r.missing->src.kind, r.missing->src.ref);
                argHeld[pr.id] = pr;
                promptFlow.askVerbArg(pr.id, C2S::VerbExpansion, net::encodeVerbExpansionRequest(*r.missing),
                                      std::chrono::steady_clock::now());
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
            std::fprintf(stderr, "[input] command \"%s\": 0x%04x len=%zu %s\n", line.c_str(), f.type, f.body.size(),
                         ok ? "sent" : "NOT SENT");
        }
    };
    auto runCommand = [&](const std::string& line, bool promptNull) {
        runResult(line, im.verbCommand(line, m.world().haveSelf, srcFor, argAtoms, promptNull));
    };
    auto drainInbox = [&] {
        std::deque<Job> jobs;
        {
            std::lock_guard<std::mutex> g(inMu_);
            jobs.swap(inbox_);
        }
        for (Job& j : jobs) {
            if (auto* mv = std::get_if<MoveJob>(&j)) {
                // Under client.dir the keys move by the screen: `.north` goes to whatever is drawn at the top (stock
                // 516.1689, a scratch probe world: EAST sends east, south, west, north for up, right, down, left).
                const int turn = m.world().clientDir;
                mv->dir = shell::MapTurn(turn == 4 ? 8 : turn == 8 ? 4 : turn).icon(mv->dir);
                net::FrameOut f = net::stockMoveFrame(mv->dir);
                bool ok = s.sendInput(f.type, f.body);
                std::fprintf(stderr, "[input] move dir=%d: 0x%04x len=%zu %s (sub 0: stock move command)\n", mv->dir,
                             f.type, f.body.size(), ok ? "sent" : "NOT SENT");
            } else if (auto* cj = std::get_if<CmdJob>(&j)) {
                runCommand(cj->line, cj->promptNull);
            } else if (auto* aj = std::get_if<ArgJob>(&j)) {
                if (auto sc = pendingSrcs.find(aj->id); sc != pendingSrcs.end()) {
                    net::SrcChoice choice = std::move(sc->second);
                    pendingSrcs.erase(sc);
                    if (aj->cancel) {
                        std::fprintf(stderr, "[input] \"%s\" cancelled - nothing sent\n", choice.title.c_str());
                        continue;
                    }
                    // The picked name is the src. Names are unique in the list.
                    std::vector<net::SrcCandidate> picked;
                    for (const net::SrcCandidate& c : choice.candidates)
                        if (c.name == aj->text) {
                            picked.push_back(c);
                            break;
                        }
                    if (picked.empty()) {
                        std::fprintf(stderr, "[input] \"%s\": \"%s\" is not in the list - nothing sent\n",
                                     choice.title.c_str(), aj->text.c_str());
                        continue;
                    }
                    runResult(choice.line + " <- src \"" + aj->text + "\"",
                              im.verbCommand(
                                  choice.line, m.world().haveSelf, [&](uint32_t) { return picked; }, argAtoms,
                                  choice.promptNull));
                    continue;
                }
                auto it = pendingArgs.find(aj->id);
                if (it == pendingArgs.end()) continue;
                net::MissingArg miss = std::move(it->second);
                pendingArgs.erase(it);
                bool serverList = argServerLists.erase(aj->id) > 0;
                if (aj->cancel) {
                    std::fprintf(stderr, "[input] %s argument %zu cancelled - nothing sent\n", miss.verb.name.c_str(),
                                 miss.index + 1);
                    continue;
                }
                if (aj->none) {
                    runResult(miss.line + " <- None",
                              net::InputModel::completeNull(miss, m.world().haveSelf, argAtoms));
                    continue;
                }
                // The value is the argument, verbatim: it is never re-parsed as a typed line, whose quote rules would
                // eat a leading ' or ". A pick from an ExpansionReply listbox goes back as net::listPickText.
                std::string value = serverList ? net::listPickText(aj->text) : aj->text;
                runResult(miss.line + " <- \"" + aj->text + "\"",
                          net::InputModel::completeArg(miss, value, m.world().haveSelf, argAtoms));
            } else if (auto* pj = std::get_if<PromptJob>(&j)) {
                // Exactly one reply per request. A cancel is sent only where stock DreamSeeker offers one.
                model::PromptReply rep = pj->cancel  ? prompts.cancel(pj->id)
                                         : pj->alert ? prompts.answerAlert(pj->id, pj->text)
                                                     : prompts.answerInput(pj->id, pj->text);
                if (rep.type == 0) {  // withdrawn by the server, or a cancel stock refuses (model::Prompt::cancellable)
                    std::fprintf(stderr, "[input] prompt %u %s - nothing sent\n", pj->id,
                                 prompts.withdrawn(pj->id, pj->alert) ? "withdrawn by the server"
                                                                      : "cancel refused, not cancellable");
                    continue;
                }
                bool ok = s.sendInput(rep.type, rep.body);
                std::fprintf(stderr, "[input] prompt %u %s: 0x%04x len=%zu %s\n", pj->id,
                             pj->cancel ? "CANCEL" : "answer", rep.type, rep.body.size(), ok ? "sent" : "NOT SENT");
                // An in-list pick: the InputResolve asks the server for the member, and the input() is answered with
                // its reply.
                if (ok && rep.type == C2S::InputResolve) resolving.push_back({{}, "input() " + pj->text, true, pj->id});
            } else if (auto* sj = std::get_if<StatTabJob>(&j)) {
                model::StatTabSelect sel;
                if (model::selectStatTab(m.worldMut(), sj->index, sel)) {
                    bool ok = s.sendInput(C2S::StatTab, model::encodeStatTabBody(sel.index, sel.token, sel.name));
                    std::fprintf(stderr, "[input] stat tab %u \"%s\" token=%u: StatTab %s\n", sel.index,
                                 sel.name.c_str(), sel.token, ok ? "sent" : "NOT SENT");
                } else {
                    std::fprintf(stderr, "[input] stat tab %d out of range\n", sj->index);
                }
            } else if (auto* hj = std::get_if<StatHoverJob>(&j)) {
                // The stat panel's pointer goes through the same record as the maps' (shell::MapHover).
                std::optional<shell::MapPointer> p;
                if (hj->leave) {
                    p.emplace();
                    p->kind = shell::MapPointer::Leave;
                    p->session = mapSession_;
                } else if (lastStatView) {
                    p = shell::statHoverPointer(m.world(), *lastStatView, hj->hover, mapSession_);
                }
                if (!p) continue;  // picked on a view the panel no longer shows
                updateHover(*p, true);
            } else if (auto* pointerJob = std::get_if<MapPointerJob>(&j)) {
                const auto& p = pointerJob->pointer;
                const auto& w = m.world();
                if (p.session != mapSession_) continue;
                const auto& c = p.click;
                const bool current = shell::mapPointerCurrent(p, w, mapSession_);
                if (p.kind == shell::MapPointer::Hover || p.kind == shell::MapPointer::Leave) {
                    if (p.fromDefaultMap)
                        if (auto name = current ? shell::mapHoverName(p, w) : std::string(); name != statusName) {
                            statusName = std::move(name);
                            emit hoverName(QString::fromStdString(statusName));
                        }
                    updateHover(p, current);
                    continue;
                }
                const auto popup = shell::mapPopupAction(p, im.showPopupMenus(), rightPressSent);
                // A release clears the hover record, unless the verb-menu path takes it (stock 516.1689,
                // tests/data/hover-world).
                if ((p.kind == shell::MapPointer::Up || p.kind == shell::MapPointer::Released) &&
                    shell::mapReleaseForgetsHover(popup))
                    hover.released();
                if (p.kind == shell::MapPointer::Released) continue;
                static const bool traceInput = std::getenv("FUGUE_INPUT_TRACE") != nullptr;
                if (traceInput) {
                    uint32_t appid = UINT32_MAX;
                    if (const auto named = shell::mapPointerTarget(p, w);
                        named && named->kind != shell::MapPointerTarget::Turf && named->appid)
                        appid = *named->appid;
                    if (auto a = w.appearances.find(appid); a != w.appearances.end())
                        std::fprintf(stderr, "[map-input] appid=%u name=%s appearanceFlags=%08x\n", appid,
                                     model::plainAtomName(a->second.fields.name).c_str(),
                                     a->second.fields.appearanceFlags);
                    const auto& src = p.source ? p.source->click.over : proto::enc::nullRef();
                    std::fprintf(stderr,
                                 "[map-input] event=%d current=%d session=%llu z=%d over=%u:%u@%u,%u hud=%d "
                                 "identity=%llu source=%u:%u params=%s flags=%08x\n",
                                 int(p.kind), current, static_cast<unsigned long long>(p.session), p.z, c.over.kind,
                                 c.over.ref, c.over.x, c.over.y, p.screen,
                                 static_cast<unsigned long long>(p.targetIdentity), src.kind, src.ref,
                                 proto::enc::clickParams(c).c_str(), im.mouseFlags());
                }
                if (p.kind == shell::MapPointer::Down && p.click.button == "right")
                    rightPressSent = popup == shell::MapPopupAction::None;
                // A transparent/iconless rectangle can belong to the menu stack
                // even when there is no ordinary alpha hit. Validate each entry.
                if (!current && !(popup == shell::MapPopupAction::Open && p.insideScene)) continue;
                if (popup == shell::MapPopupAction::Open) {
                    if (p.session != mapSession_ || p.z != w.viewZ || (p.surface && p.surface->expired())) continue;
                    mapChoices.clear();
                    shell::MapMenu menu;
                    menu.token = ++mapMenuToken_;
                    menu.global = p.global;
                    menuZ = p.z;
                    // The recorder's marker: where the click landed, what was stacked there, what the menu offers.
                    std::string stacked, offered;
                    {
                        int group = 0;
                        for (const auto& entry : shell::mapMenuStack(p, w, mapSession_)) {
                            const auto& rendered = entry.rendered;
                            const auto& ref = rendered.click.over;
                            const auto& hit = entry.hit;
                            const auto& name = entry.name;
                            if (rec_)
                                stacked += (stacked.empty() ? "" : ",") + std::string("{\"name\":") +
                                           model::Recorder::quote(name) + ",\"kind\":" + std::to_string(ref.kind) +
                                           ",\"ref\":" + std::to_string(ref.ref) + ",\"x\":" + std::to_string(entry.x) +
                                           ",\"y\":" + std::to_string(entry.y) +
                                           ",\"screen\":" + (rendered.screen ? "true" : "false") + "}";
                            if (name.empty() || entry.verbs.empty()) continue;
                            // The picture is taken now, from the scene the click was made on: stock's does not
                            // change while the menu is open.
                            const QImage icon = shell::mapMenuIcon(rendered);
                            std::string verbs;
                            for (const auto& verb : entry.verbs) {
                                if (mapChoices.size() >= 1024) break;
                                menu.items.push_back({group, QString::fromStdString(name),
                                                      QString::fromStdString(model::menuVerbLabel(verb.verb)), icon});
                                if (rec_) verbs += (verbs.empty() ? "" : ",") + model::Recorder::quote(verb.verb.name);
                                MapChoice choice{hit, verb.verb.id, verb.viaArgument, name};
                                choice.attachment = rendered;
                                if (hit.target.kind != model::kTargetTurf) {
                                    auto atom =
                                        w.atoms.find(hit.target.ref |
                                                     (hit.target.kind == model::kTargetMob ? model::kRegistryA : 0));
                                    if (atom != w.atoms.end()) {
                                        choice.key = atom->second.key;
                                        choice.haveKey = atom->second.haveKey;
                                        choice.lifetime = atom->second.renderIdentity;
                                    }
                                }
                                mapChoices.push_back(std::move(choice));
                            }
                            if (rec_ && !verbs.empty())
                                offered += (offered.empty() ? "" : ",") + std::string("{\"atom\":") +
                                           model::Recorder::quote(name) + ",\"verbs\":[" + verbs + "]}";
                            ++group;
                        }
                    }
                    if (rec_)
                        rec_->marker("menu", "\"px\":" + std::to_string(p.scenePixel.x()) +
                                                 ",\"py\":" + std::to_string(p.scenePixel.y()) + ",\"tile\":[" +
                                                 std::to_string(int(c.loc.x) + 1) + "," +
                                                 std::to_string(int(c.loc.y) + 1) + "],\"hits\":[" + stacked +
                                                 "],\"menu\":[" + offered + "]");
                    emit mapMenuReady(menu);
                    continue;
                }
                auto hooks = im.haveMouseSubs() ? im.hooks() : proto::enc::allMouseHooks();
                auto event = p;
                if (event.kind == shell::MapPointer::DoubleClick && !hooks.dblClick && hooks.click)
                    event.kind = shell::MapPointer::Click;
                // Popup right input is consumed, including Down/Up: independent
                // stock receipts contain only a selected menu verb, not mouse hooks.
                const bool wanted = popup == shell::MapPopupAction::None &&
                                    shell::mapPointerWanted(event.kind, hooks, im.clientWheel());
                const bool sent =
                    wanted &&
                    s.sendInput(C2S::Command, proto::enc::mouseWireBody(shell::mapPointerBody(event), s.wideVerbIds()));
                if (traceInput)
                    std::fprintf(stderr, "[map-input] event=%d wanted=%d sent=%d\n", int(event.kind), wanted, sent);
            } else if (auto* pick = std::get_if<MapPickJob>(&j)) {
                if (pick->token != mapMenuToken_ || pick->index < 0 || size_t(pick->index) >= mapChoices.size())
                    continue;
                auto choice = mapChoices[size_t(pick->index)];
                mapChoices.clear();  // one invocation per popup
                const auto& w = m.world();
                if (w.viewZ != menuZ ||
                    (choice.attachment && !shell::mapPointerCurrent(*choice.attachment, w, mapSession_)))
                    continue;
                auto& target = choice.hit.target;
                // The same resolution the menu was built with, against the world as it is now.
                const auto named =
                    choice.attachment ? shell::mapPointerTarget(*choice.attachment, w) : std::nullopt;
                if (!named || !named->appid) continue;
                choice.hit.appid = *named->appid;
                if (named->kind == shell::MapPointerTarget::Turf) {
                    if (named->cell[2] != uint16_t(menuZ)) continue;
                } else if (named->kind == shell::MapPointerTarget::Atom && !choice.attachment->visHit) {
                    // A vis_contents member is pinned by the membership validation above, a nullspace child too; its
                    // menu location remains the rendered holder's cell. An atom on the map follows its own position.
                    auto atom = w.atoms.find(named->atom);
                    if (atom == w.atoms.end() || !atom->second.hasPos || atom->second.z != menuZ ||
                        w.resync.freed.count(atom->first) || atom->second.renderIdentity != choice.lifetime ||
                        (choice.haveKey && (!atom->second.haveKey || atom->second.key != choice.key)))
                        continue;
                    target.x = atom->second.x;
                    target.y = atom->second.y;
                    choice.hit.appid = atom->second.appid;
                }
                auto offered = model::atomMenu(w, choice.hit.appid, target);
                if (std::none_of(offered.begin(), offered.end(), [&](const auto& v) {
                        return v.verb.id == choice.verb && v.viaArgument == choice.argument;
                    }))
                    continue;
                auto verb = im.verbs().find(choice.verb);
                if (verb == im.verbs().end()) continue;
                uint32_t ref = target.kind == model::kTargetTurf
                                   ? uint32_t(target.x - 1) | (uint32_t(target.y - 1) << 16)
                                   : target.ref;
                if (choice.argument) {
                    model::VerbSrc src;
                    if (!model::ownVerbSrc(w, choice.verb, src)) continue;
                    const uint32_t dmRef =
                        target.kind == model::kTargetTurf
                            ? 0x01000000u | (uint32_t(target.x - 1) + uint32_t(target.y - 1) * uint32_t(w.bounds.maxX) +
                                             uint32_t(menuZ) * uint32_t(w.bounds.maxX) * uint32_t(w.bounds.maxY))
                            : model::dmRefOf(w, target);
                    runResult(choice.name, net::InputModel::atomArgument(verb->second, {src.kind, src.ref}, target.kind,
                                                                         ref, net::atomArgumentText(choice.name, dmRef),
                                                                         w.haveSelf, choice.name, argAtoms));
                } else {
                    runResult(choice.name, net::InputModel::atomVerb(verb->second, {target.kind, ref}, choice.name,
                                                                     w.haveSelf, argAtoms));
                }
            } else if (auto* rj = std::get_if<StatRowJob>(&j)) {
                const model::World& w = m.world();
                proto::enc::AtomRef ref;
                if (lastStatView && shell::statRowCurrent(w, *lastStatView, rj->row, rj->generation) &&
                    model::statRowRef(w, model::statValueOf(w.stat.lines[size_t(rj->row)].value), ref)) {
                    model::StatRowClick c;
                    // A click on a row clears the hover record, as one on a map does (stock 516.1689,
                    // tests/data/hover-world panel pclick: the next step is an over report, MouseMove at the server).
                    hover.released();
                    c.sub = rj->dbl ? proto::enc::kMouseDblClick : proto::enc::kMouseClick;
                    c.button = rj->button;
                    c.mods.ctrl = rj->mods & 1;
                    c.mods.shift = rj->mods & 2;
                    c.mods.alt = rj->mods & 4;  // StatPanelWidget::RowMods
                    c.control = rj->control;
                    c.icon = rj->icon;
                    c.iconX = rj->iconX;
                    c.iconY = rj->iconY;
                    // Gated by the world's MouseSubs, as a map click is.
                    bool wanted = !im.haveMouseSubs() || (rj->dbl ? im.hooks().dblClick : im.hooks().click);
                    bool ok =
                        wanted && s.sendInput(C2S::Command, proto::enc::mouseWireBody(
                                                                model::statRowClickBody(w, ref, c), s.wideVerbIds()));
                    std::fprintf(stderr, "[input] stat row %d \"%s\" %s: over=%u/%u control=%s %s\n", rj->row,
                                 w.stat.lines[size_t(rj->row)].name.c_str(), rj->dbl ? "dblclick" : "click", ref.kind,
                                 ref.ref, c.control.c_str(),
                                 !wanted ? "not subscribed"
                                 : ok    ? "sent"
                                         : "NOT SENT");
                } else {
                    std::fprintf(stderr, "[input] stat row %d is not clickable\n", rj->row);
                }
            } else if (auto* tj = std::get_if<TopicJob>(&j)) {
                bool ok = s.sendInput(C2S::Topic, proto::enc::encodeTopic(tj->href));
                std::fprintf(stderr, "[input] topic %s %s\n", tj->href.substr(0, 120).c_str(),
                             ok ? "sent" : "NOT SENT");
            }
        }
    };

    auto nextTick = std::chrono::steady_clock::now();
    fugue::MapFrameClock mapClock(nextTick);
    const bool traceFrames = std::getenv("FUGUE_FRAME_TRACE") != nullptr;
    auto traceAt = nextTick;
    uint64_t traceRuns = 0, traceMaps = 0, traceMissed = 0;
    double traceWait = 0, traceActual = 0, traceCompose = 0, traceCompare = 0, traceMaxOver = 0;
    double traceTerrain = 0, traceEntities = 0;
    shell::EntityMap::Stats traceEntityStats;
    auto nextAudioLog = std::chrono::steady_clock::now();
    std::string lastAudioLine;
    // Each S2C::ResolveReply answer completes the oldest resolve in flight.
    auto drainListReplies = [&] {
        uint8_t kind;
        uint32_t value;
        while (im.takeListReply(kind, value)) {
            if (resolving.empty()) {
                std::fprintf(stderr, "[input] ResolveReply %02x:%u with no list resolve in flight - dropped\n", kind,
                             value);
                continue;
            }
            Resolving rv = std::move(resolving.front());
            resolving.pop_front();
            std::fprintf(stderr, "[input] command \"%s\": ResolveReply %02x:%u\n", rv.line.c_str(), kind, value);
            if (rv.prompt && prompts.withdrawn(rv.promptId, false)) {
                std::fprintf(stderr, "[input] prompt %u withdrawn by the server - answer not sent\n", rv.promptId);
                continue;
            }
            if (rv.prompt) {
                model::PromptReply rep = model::PromptBook::resolvedInput(rv.promptId, kind, value);
                bool ok = s.sendInput(rep.type, rep.body);
                std::fprintf(stderr, "[input] prompt %u answer: 0x%04x len=%zu %s\n", rv.promptId, rep.type,
                             rep.body.size(), ok ? "sent" : "NOT SENT");
                continue;
            }
            runResult(rv.line, net::InputModel::completeResolve(rv.r, kind, value, m.world().haveSelf, argAtoms));
        }
    };
    pumpPrompts = [&] {
        for (PromptFlow::Step& st : promptFlow.pump(std::chrono::steady_clock::now())) {
            switch (st.kind) {
                case PromptFlow::Step::Show:
                    if (st.gaveUp)
                        std::fprintf(stderr, "[prompt] input id=%u: no choices came - typed text only\n", st.id);
                    emitPrompt(*st.prompt);
                    break;
                case PromptFlow::Step::Send: {
                    bool ok = s.sendInput(st.type, st.body);
                    std::fprintf(stderr, "[prompt] id=%u: 0x%04x %s\n", st.id, st.type, ok ? "sent" : "NOT SENT");
                    break;
                }
                case PromptFlow::Step::VerbArgGaveUp:
                    std::fprintf(stderr, "[prompt] verb-argument id=%u: no choices came - typed text only\n", st.id);
                    if (auto it = argHeld.find(st.id); it != argHeld.end()) {
                        emit promptRequested(it->second);
                        argHeld.erase(it);
                    }
                    break;
                default: break;
            }
        }
    };
    while (!stop_ && !reconnect_ && !s.stats().serverClosed && !s.stats().rebootRequested) {
        drainInbox();
        pumpPrompts();
        drainListReplies();
        for (size_t i = 0; i < rscUnresolved.size();) {
            if (declareRsc(rscUnresolved[i]))
                rscUnresolved.erase(rscUnresolved.begin() + long(i));
            else
                i++;
        }
        if (!rscWanted.empty()) {
            s.resources().prioritize(rscWanted);
            rscWanted.clear();
        }
        sounds.pump(nowMs());  // plays held for their resource bytes (SoundFeed::kPendingTtlMs)
        s.resources().prioritize(sounds.takeWanted());
        feed.pump(nowMs(),
                  winReply);  // held winget queries: answered the moment the layout is measured, or empty at 3 s
        auto now = std::chrono::steady_clock::now();
        mapClock.setTiming(m.world().tickNanos, m.world().frameNanos, now);
        sendHover();
        auto wait = mapClock.wait(now, nextTick);
        if (const auto at = hover.dueAt()) {  // a waiting hover report goes out on its own time
            const auto left = std::chrono::ceil<std::chrono::milliseconds>(std::chrono::nanoseconds(*at - steadyNs()));
            wait = std::clamp(left, std::chrono::milliseconds(0), wait);
        }
        s.run(wait);
        if (traceFrames) {
            double actual = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count();
            ++traceRuns;
            traceWait += wait.count();
            traceActual += actual;
            traceMaxOver = std::max(traceMaxOver, actual - wait.count());
        }
        feed.pump(nowMs(), winReply);
        now = std::chrono::steady_clock::now();
        const model::World& w = m.world();
        // Timing and map bounds must not wait for the slower chat/stat tick.
        mapClock.setTiming(w.tickNanos, w.frameNanos, now);
        if (mapMetrics_) mapMetrics_->setPeriod(mapClock.periodNs());
        if (w.tickNanos != lastTickNs || w.frameNanos != lastFrameNs) {
            lastTickNs = w.tickNanos;
            lastFrameNs = w.frameNanos;
            emit frameTiming(lastTickNs, lastFrameNs);
        }
        if (w.bounds.have && (w.bounds.shownW() != lastViewW || w.bounds.shownH() != lastViewH)) {
            lastViewW = w.bounds.shownW();
            lastViewH = w.bounds.shownH();
            emit mapViewSize(lastViewW, lastViewH);
        }

        if (mapClock.due(now)) {
            // The default scene uses the default MAP's live stylesheet/font.
            // Named scenes are prepared independently below, on this worker too.
            const auto skinGeneration = live->generation();
            if (mapTextSkinGeneration != skinGeneration) {
                const auto settings = live->defaultMapTextSettings();
                bool validSize = true;
                const double points =
                    settings.size.empty() ? 0 : QString::fromStdString(settings.size).toDouble(&validSize);
                entityMap.setMapTextStyle({QString::fromStdString(settings.style),
                                           QString::fromStdString(settings.family), validSize ? points : -1});
                mapTextSkinGeneration = skinGeneration;
            }
            const int64_t compositionStart = shell::MapMetrics::nowNs();
            const int64_t renderNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();
            const auto readMapResource = [&](uint32_t id, QByteArray& bytes) {
                Bytes data;
                if (!s.resources().readStored(id, data)) {
                    rscWanted.push_back(id);
                    return false;
                }
                bytes = QByteArray(reinterpret_cast<const char*>(data.data()), qsizetype(data.size()));
                return true;
            };
            auto map = turfMap.prepare(w, readMapResource, renderNs, false, entityMap.hudMargins());
            // Admission domain is the tiles the presented source rectangle actually shows,
            // which a glide can extend one tile beyond the eye-centred nominal view.
            map.geometry.visibility = mapVisibility.prepare(
                w, shell::MapVisibility::displayedCells(map.geometry.region, map.geometry.tile,
                                                      shell::MapTurn(map.geometry.dir).rect(map.source)));
            const int64_t terrainEnd = traceFrames ? shell::MapMetrics::nowNs() : 0;
            shell::EntityMap::Stats entityStats;
            auto sprites =
                entityMap.prepare(w, map, readMapResource, renderNs, traceFrames ? &entityStats : nullptr, true);
            const int64_t compositionEnd = shell::MapMetrics::nowNs();
            const double compositionMs = double(compositionEnd - compositionStart) / 1e6;
            if (traceFrames) {
                traceTerrain += double(terrainEnd - compositionStart) / 1e6;
                traceEntities += double(compositionEnd - terrainEnd) / 1e6;
                traceEntityStats.candidates += entityStats.candidates;
                traceEntityStats.visits += entityStats.visits;
                traceEntityStats.appearanceDecodes += entityStats.appearanceDecodes;
                traceEntityStats.frameDecodes += entityStats.frameDecodes;
                traceEntityStats.resourceReads += entityStats.resourceReads;
                traceEntityStats.traverseMs += entityStats.traverseMs;
                traceEntityStats.sortMs += entityStats.sortMs;
                traceEntityStats.pruneMs += entityStats.pruneMs;
                traceEntityStats.assembleMs += entityStats.assembleMs;
                traceEntityStats.templateBuilds += entityStats.templateBuilds;
                traceEntityStats.instanceUpdates += entityStats.instanceUpdates;
                traceEntityStats.culled += entityStats.culled;
                traceEntityStats.sortedSprites += entityStats.sortedSprites;
                traceEntityStats.sceneReuses += entityStats.sceneReuses;
            }
            const int64_t compareStart = shell::MapMetrics::nowNs();
            map.geometry.session = mapSession_;
            if (map.image.cacheKey() != lastMapFrame.image.cacheKey() || map.source != lastMapFrame.source ||
                map.geometry != lastMapFrame.geometry || sprites != lastSprites) {
                lastMapFrame = map;
                lastSprites = sprites;
                if (mapMailbox_) {
                    if (mapMailbox_->publish(map.image, map.source, compositionStart, shell::MapMetrics::nowNs(),
                                             map.geometry, sprites))
                        emit mapFrameAvailable();
                    if (atomTrace) {
                        ++diagnosticMapPublication;
                        diagnosticPublishedSprites = 0;
                        for (const auto& sprite : sprites)
                            if (sprite.atom == atomTrace->key && sprite.targetIdentity == atomTrace->map.lifetime &&
                                (sprite.kind == shell::MapSprite::Entity || sprite.kind == shell::MapSprite::Master))
                                ++diagnosticPublishedSprites;
                    }
                }
            }
            // Each subscription owns independent caches, master/target selection,
            // immutable publication and GUI composition. Never retain the mailbox:
            // its last GUI owner disappearing retires the worker scene next tick.
            std::set<uint64_t> activeMaps;
            if (mapRoutes_)
                for (const auto& route : mapRoutes_->snapshot()) {
                    activeMaps.insert(route.id);
                    auto& state = namedScenes[route.id];
                    if (!state) state = std::make_unique<NamedScene>(route.name);
                    const auto namedStart = shell::MapMetrics::nowNs();
                    const auto frame = state->scene.prepare(w, readMapResource, renderNs, mapSession_);
                    const auto namedEnd = shell::MapMetrics::nowNs();
                    route.mailbox->metrics()->setPeriod(mapClock.periodNs());
                    route.mailbox->metrics()->composed(double(namedEnd - namedStart) / 1e6, 0);
                    const auto& old = state->last;
                    if (frame.terrain.image.cacheKey() != old.terrain.image.cacheKey() ||
                        frame.terrain.source != old.terrain.source || frame.terrain.geometry != old.terrain.geometry ||
                        frame.sprites != old.sprites) {
                        state->last = frame;
                        if (route.mailbox->publish(frame.terrain.image, frame.terrain.source, namedStart, namedEnd,
                                                   frame.terrain.geometry, frame.sprites))
                            emit mapFrameAvailable();
                    }
                }
            std::erase_if(namedScenes, [&](const auto& entry) { return !activeMaps.count(entry.first); });
            now = std::chrono::steady_clock::now();
            const uint64_t missed = mapClock.completed(now);
            if (mapMetrics_) mapMetrics_->composed(compositionMs, missed);
            if (traceFrames) {
                ++traceMaps;
                traceMissed += missed;
                traceCompose += compositionMs;
                traceCompare += double(shell::MapMetrics::nowNs() - compareStart) / 1e6;
            }
        }
        if (traceFrames && now - traceAt >= std::chrono::seconds(1)) {
            double seconds = std::chrono::duration<double>(now - traceAt).count();
            // map/s counts frames composed here; shown/s the map frames that reached the screen: the window had
            // composed them and handed them on for display (QRhiWidget::frameSubmitted, counted by the map widget).
            const double shown = mapMetrics_ ? mapMetrics_->snapshot().submittedPerSecond : 0;
            std::fprintf(stderr,
                         "[frame-trace] target=%.1f map/s=%.1f shown/s=%.1f missed/s=%.1f compose=%.3fms compare=%.3fms "
                         "waits=%llu requested=%.3fms actual=%.3fms max-over=%.3fms\n",
                         1e9 / mapClock.periodNs(), traceMaps / seconds, shown, traceMissed / seconds,
                         traceMaps ? traceCompose / traceMaps : 0, traceMaps ? traceCompare / traceMaps : 0,
                         static_cast<unsigned long long>(traceRuns), traceRuns ? traceWait / traceRuns : 0,
                         traceRuns ? traceActual / traceRuns : 0, traceMaxOver);
            std::fprintf(stderr,
                         "[scene-trace] view=%dx%d atoms=%zu appearances=%zu sprites=%lld terrain=%.3fms "
                         "entities=%.3fms cache=%dx%d\n",
                         w.bounds.viewW, w.bounds.viewH, w.atoms.size(), w.appearances.size(),
                         static_cast<long long>(lastSprites.size()), traceMaps ? traceTerrain / traceMaps : 0,
                         traceMaps ? traceEntities / traceMaps : 0, lastMapFrame.image.width(),
                         lastMapFrame.image.height());
            const double attempts = double(std::max<uint64_t>(1, traceMaps));
            std::fprintf(stderr,
                         "[entity-trace] candidates=%.0f visits=%.0f culled=%.0f templates=%.1f instances=%.1f "
                         "sorted=%.0f reuse/s=%.1f traverse=%.3fms sort=%.3fms assemble=%.3fms prune=%.3fms "
                         "appearance-decodes/s=%.1f frame-decodes/s=%.1f resource-reads/s=%.1f\n",
                         traceEntityStats.candidates / attempts, traceEntityStats.visits / attempts,
                         traceEntityStats.culled / attempts, traceEntityStats.templateBuilds / attempts,
                         traceEntityStats.instanceUpdates / attempts, traceEntityStats.sortedSprites / attempts,
                         traceEntityStats.sceneReuses / seconds, traceEntityStats.traverseMs / attempts,
                         traceEntityStats.sortMs / attempts, traceEntityStats.assembleMs / attempts,
                         traceEntityStats.pruneMs / attempts, traceEntityStats.appearanceDecodes / seconds,
                         traceEntityStats.frameDecodes / seconds, traceEntityStats.resourceReads / seconds);
            traceEntityStats = {};
            traceAt = now;
            traceRuns = traceMaps = traceMissed = 0;
            traceWait = traceActual = traceCompose = traceCompare = traceMaxOver = 0;
            traceTerrain = traceEntities = 0;
        }

        if (now < nextTick) continue;
        nextTick = now + std::chrono::milliseconds(kTickMs);
        // Tell the GUI once when the skin (SkinText, or the .dmf resource SkinResource names) has loaded.
        if (feed.counters().skinLoads != skinLoadsSeen) {
            skinLoadsSeen = feed.counters().skinLoads;
            emit skinLoaded();
        }
        syncBrowseNames();
        if (QString n = QString::fromStdString(w.name); n != lastName) {
            lastName = n;
            emit worldName(n);
        }
        // The main window icon is re-resolved when its value changes, and on every tick until its file arrives.
        if (skin::IconResolver::Out io; icon.update(feed.live().mainIcon(), io)) {
            log("window icon \"" + io.value + "\" -> " +
                (!io.bytes.empty() ? std::to_string(io.bytes.size()) + " bytes"
                                   : std::string(io.none ? "none" : "not here yet")));
            emit windowIcon(QString::fromStdString(io.name), QByteArray(io.bytes.data(), int(io.bytes.size())));
        }
        s.resources().prioritize(icon.takeWanted());

        flushChat();
        servePageIcons();
        // Audio status is logged only when it changes.
        if (now >= nextAudioLog) {
            nextAudioLog = now + std::chrono::seconds(5);
            audio::Player::Stats as = player.stats();
            std::string line = "played=" + std::to_string(as.played) + " stops=" + std::to_string(as.stops) +
                               " pending=" + std::to_string(sounds.pending()) +
                               " expired=" + std::to_string(sounds.expired()) + " live=" + std::to_string(as.live);
            for (const auto& [k, n] : as.byFormat) line += " " + k + "=" + std::to_string(n);
            for (const auto& [k, n] : as.unsupported) line += " UNDECODABLE-" + k + "=" + std::to_string(n);
            if (as.failed) line += " decode-failures=" + std::to_string(as.failed);
            if (line != lastAudioLine) {
                lastAudioLine = line;
                audio::note(line + " non-silent-blocks=" + std::to_string(as.peakBlocks) +
                            " sink=" + player.sinkName());
            }
        }
        if (rsc_)
            browseRecipes.pump(
                w.resources,
                [&](uint32_t id, QByteArray& bytes) {
                    Bytes data;
                    if (!s.resources().readStored(id, data)) {
                        rscWanted.push_back(id);
                        return false;
                    }
                    bytes = QByteArray(reinterpret_cast<const char*>(data.data()), qsizetype(data.size()));
                    return true;
                },
                *rsc_);
        statThumbnails.begin(
            m.world(),
            [&](uint32_t id, QByteArray& bytes) {
                Bytes data;
                if (!s.resources().readStored(id, data)) {
                    rscWanted.push_back(id);
                    return false;
                }
                bytes = QByteArray(reinterpret_cast<const char*>(data.data()), qsizetype(data.size()));
                return true;
            },
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        if (shell::StatView sv = shell::buildStatView(m.world(), statThumbnails, atomTrace ? &*atomTrace : nullptr);
            !lastStatView || !(sv == *lastStatView)) {
            sv.generation = ++statGeneration_;
            lastStatView = sv;
            emit statView(sv);
        }
        if (atomTrace) {
            const auto record = atomTraceOutput.sample(
                *atomTrace, mapSession_, diagnosticMapPublication, diagnosticPublishedSprites,
                lastStatView ? lastStatView->generation : 0,
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(),
                mapMailbox_ ? &lastSprites : nullptr,
                lastStatView && atomTrace->infoRow && *atomTrace->infoRow < unsigned(lastStatView->rows.size())
                    ? &lastStatView->rows[int(*atomTrace->infoRow)].thumbnail
                    : nullptr);
            if (!record.isEmpty()) {
                std::fputs("[atom-trace] ", stderr);
                std::fwrite(record.constData(), 1, size_t(record.size()), stderr);
                std::fflush(stderr);
            }
        }
        for (const auto& [name, b] : w.browsers) {
            auto it = lastBrowse.find(name);
            if (it == lastBrowse.end() || it->second != b.html) {
                lastBrowse[name] = b.html;
                emit browseHtml(QString::fromStdString(name), QString::fromStdString(b.params),
                                QString::fromStdString(b.html), live->propertyRevision());
            }
        }
        for (auto it = lastBrowse.begin(); it != lastBrowse.end();) {
            if (!w.browsers.count(it->first) && !fileBrowse.count(it->first)) {
                emit browseClosed(QString::fromStdString(it->first));
                it = lastBrowse.erase(it);
            } else
                ++it;
        }
        for (const skin::LiveSkin::CloneAction& a :
             live->cloneActions(cloneOpen)) {  // a winclone()d window: open while shown and holding a page
            if (a.open)
                emit browseHtml(QString::fromStdString(a.name), QString::fromStdString(a.params),
                                QString::fromStdString(a.html), live->propertyRevision());
            else
                emit browseClosed(QString::fromStdString(a.name));
        }
    }
    flushChat();  // a closing SessionEnd's line arrives with the close
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
