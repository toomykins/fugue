// A synchronous TCP game session: Handshake -> K_login -> Login -> frame pump.
// Windows frame-pump reads use event notification (nonblocking socket) plus a precise deadline timer.
//
// Threading: plain native sockets (POSIX / Winsock) and ONE thread. The thread that calls run() is the only reader and
// writer of the socket. Every reply (resource pulls, prompt answers, the Keepalive) is produced inline from the frame
// handler on that thread, so there is no write lock. The observer runs on the session thread and must only enqueue; a
// GUI runs run() on its own thread and hands decoded frames over. net/ stays Qt-free (platform/socket.h).
//
// Login sequence: Handshake, K_login, Login, then the replies handle() sends to server frames. One K_login cipher is
// used in both directions, and s2c frames carry no seq. C2S::Ready answers the first ResourceList; ConnSetting and
// WorldRefresh answer the first SelfRef, once.
#pragma once
#include "platform/sockettypes.h"
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "net/bytes.h"
#include "net/cauth.h"
#include "net/frame.h"
#include "net/handshake.h"
#include "net/respump.h"

namespace fugue::socket {
class ReadWait;
}

namespace fugue::net {

struct SessionConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 0;
    std::string stateDir;  // identity lives here (default ~/.fugue)
    // Resource cache dir (see net/rscstore.h). Empty = <stateDir>/rsc when stateDir was given explicitly, else
    // $HOME/.fugue/rsc.
    std::string rscDir;
    bool stateDirExplicit = false;  // set by Session: stateDir was non-empty on entry
    std::string wireLogPath;  // empty = none. One line per frame, plaintext bodies; Login bodies are never logged.
    bool humanPacing = true;  // small randomized delays between login frames (tens to hundreds of ms)
    int connectTimeoutMs = 10000;
    // Never send the Ready that answers the first ResourceList: the server serves resources but sends no world and runs
    // no mob/Login(). The cache warm-up pass sets it.
    bool holdEntry = false;
    // Account login: when non-empty, this pre-minted Login body (len(account)+43 bytes) replaces the guest credential.
    // net/ stays Qt-free, so the caller mints it (account/login.h). It is a bearer token: the wire log redacts every
    // Login and CertAnswer body.
    Bytes accountRecord;
    std::string accountLabel;  // the ckey: the log label and the cauth.txt entry name (net/cauth.h)
    // Hub login certificate, for an account login on a hub-registered server. Such a server answers the Login with a
    // CertChallenge and initialises the world only after a CertAnswer carrying a token the BYOND hub mints for this
    // session. net/ talks only to the game socket, so the caller supplies both halves (pager/hubcert.h wireHubCert):
    // hubCertAnswer runs the blocking hub round trip and returns the CertAnswer body (a live credential, never logged);
    // hubRekey maps K_login to the re-keyed K_login. The re-key applies when the server sends CertAck. c2s frames sent
    // in between would be read under the wrong key, so they are held until the ack.
    // Unset = the challenge is logged and not answered.
    // The Session passes the cauthKey and certStr from its per-connection cauth state (net/cauth.h): hubCertAnswer gets
    // the pair the hub must be asked about, hubRekey the certStr that request carried.
    std::function<std::optional<Bytes>(uint32_t cauthKey, const std::string& certStr, std::string* err)> hubCertAnswer;
    std::function<uint32_t(const std::string& certStr, uint32_t kOld)> hubRekey;
};

struct SessionStats {
    std::map<uint16_t, uint64_t> s2c;  // frames received by type
    std::map<uint16_t, uint64_t> c2s;  // frames sent by type
    std::vector<uint16_t> s2cOrder;    // first-seen order of s2c types (the init-burst sequence)
    uint64_t badChecksum = 0;
    uint64_t framingErrors = 0;
    uint64_t promptsAnswered = 0;
    bool serverClosed = false;
    std::string closeReason;
    bool rebootRequested = false;  // Reboot arrived: the world is rebooting and the client must log in again
    bool connected = false;        // the TCP connection was made
    bool sawSelfRef = false;       // SelfRef arrived: the server made us a /client and ran world-init
    bool sawWorldInit = false;     // MouseSubs arrived (part of the world-init burst)
    bool accountLogin = false;     // the Login carried an account record, not the guest credential
    uint64_t certChallenges = 0;   // CertChallenge frames seen (a hub-registered server sends one after Login)
    int certChallengeFlag = -1;    // first body byte of the first CertChallenge (-1 = none)
    uint64_t certAnswers = 0;      // CertAnswer frames sent (each a fresh hub-minted token)
    bool rekeyed = false;          // CertAck arrived and the session cipher was re-keyed
    uint32_t selfRefValue = 0;
    uint32_t computerId = 0;
};

class Session {
public:
    // Session-thread MeasureText (0x00fc) layout; raw text/style and wrapping width.
    // Return the 0xffff pair for unsupported input. Without a handler, valid queries
    // receive that sentinel rather than parking the server procedure.
    using TextMeasure = std::function<std::pair<uint16_t, uint16_t>(const std::string&, const std::string&, uint16_t)>;
    void setTextMeasure(TextMeasure measure) { textMeasure_ = std::move(measure); }
    using Observer = std::function<void(uint16_t type, ByteSpan plainBody)>;
    using Log = std::function<void(const std::string&)>;
    explicit Session(SessionConfig cfg, Log log = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Connects, handshakes, logs in (as a guest, or with accountRecord) and sends the post-login frames. Returns "" or an error.
    std::string connectAndLogin();
    // After stats().rebootRequested: handshakes and logs in again on the same connection, as the stock client does when
    // the world reboots. stats() start over. Returns "" or an error.
    std::string relogin();
    // Pump frames for `duration` (or until the server closes). Returns when done; the socket stays open.
    void run(std::chrono::milliseconds duration);
    // run()'s poll() timeout for `left` of its budget: whole ms rounded up, at most 100, never negative. poll() treats
    // a negative timeout as "block until the socket has data", which would park the session thread past its budget.
    // A large transfer can overrun the budget, so the clamp matters.
    static int pollTimeoutMs(std::chrono::microseconds left);
    void close();

    // Called on the session thread for every decrypted s2c frame (including resource frames) before the built-in
    // handling. Must not block.
    void setObserver(Observer o) { observer_ = std::move(o); }
    // Called for every c2s frame handed to sendInput (plaintext body, before encryption). Session thread.
    void setSendTap(Observer o) { sendTap_ = std::move(o); }
    // Called on the session thread once per file the resource pump has verified and stored (or found already held),
    // right after the Resource frame that completed it. `data` is the verified file. Must not block long.
    using ResourceObserver = std::function<void(uint32_t id, const std::string& name, ByteSpan data)>;
    void setResourceObserver(ResourceObserver o) { resObserver_ = std::move(o); }
    // winget(control, prop) queries (S2C::Winget). With no handler the session answers the empty string inline. With a
    // handler the session answers nothing itself: the handler runs on the session thread with the raw query body and
    // must arrange exactly one replyWinget() per query, immediately or later on the same thread. The DM proc stays
    // parked until then. This lets a skin hold winexists("browseroutput") until its layout is measured (skin/skinfeed.h).
    using WingetHandler = std::function<void(ByteSpan body)>;
    void setWingetHandler(WingetHandler h) { wingetHandler_ = std::move(h); }
    // [value\0][reqid u32 LE], each backslash in the value doubled: the server unescapes it. Session thread only.
    bool replyWinget(uint32_t reqId, const std::string& value);
    // alert() (S2C::Alert) / input() (S2C::Input). By default the session auto-answers them (alert "", input null), so
    // a headless run never parks a DM proc. With manual prompts on, the session answers nothing: the observer sees the
    // request, and the owner replies through sendInput() on the session thread with model/prompt.h's encoders. An owner
    // that never answers parks the proc.
    void setManualPrompts(bool on) { manualPrompts_ = on; }
    bool manualPrompts() const { return manualPrompts_; }
    const SessionStats& stats() const { return st_; }
    // One line on how the session ended: its length, the close reason, frame counts and the last frames each way.
    std::string endSummary() const;
    const ResourceStats& resourceStats() const { return res_->stats(); }
    ResourcePump& resources() { return *res_; }
    uint32_t sessionLoginKey() const { return key_; }
    bool wideVerbIds() const { return wideVerbIds_; }  // negotiated before any observed game frame
    // Send one c2s frame (plaintext body; the K_login cipher + seq are applied). Session thread only, like run().
    // Used by the input path; counts in stats().c2s and appears in the wire log.
    bool sendInput(uint16_t type, ByteSpan plain) { return sendFrame(type, plain); }
    // Called on the session thread on every pass of run()'s loop (at most ~100 ms apart while the socket is idle),
    // after the buffered frames are handled. Owner-side timers such as the resync sweep (model/resync.h) run here.
    // It may call sendInput().
    void setPumpHook(std::function<void()> h) { pumpHook_ = std::move(h); }
    // Makes the run() in progress return once the frames already read are handled, instead of at the end of its
    // budget. Session thread only: an observer or the pump hook may call it.
    void wake() { wake_ = true; }

private:
    bool sendFrame(uint16_t type, ByteSpan plain);
    bool writeAll(const Bytes& b);
    void handle(uint16_t type, ByteSpan body);
    void processFrame(const Frame& f);  // decrypt + stats + wire log + handle(): shared by run() and the login peek
    void wire(const char* dir, uint16_t type, ByteSpan body);
    void logf(const std::string& s) {
        if (log_) log_(s);
    }
    bool readFrameBlocking(Frame& out, int timeoutMs);
    std::string readHandshakeReply(const Handshake& hs, HandshakeReply& hr);  // sets key_
    std::string login(const HandshakeReply& hr);
    void pace(int lo, int hi);
    void answerCertChallenge(uint8_t flag);

    std::unique_ptr<CauthCache> cauth_;  // this connection's cauth key and certStr
    bool wideVerbIds_ = false;
    bool rekeyPending_ = false;  // our CertAnswer is out, CertAck has not arrived
    std::vector<std::pair<uint16_t, Bytes>>
        deferredPost_;  // c2s frames held (sendFrame) until that ack re-keys the cipher

    SessionConfig cfg_;
    Log log_;
    Observer observer_, sendTap_;
    ResourceObserver resObserver_;
    WingetHandler wingetHandler_;
    TextMeasure textMeasure_;
    bool manualPrompts_ = false;
    std::set<uint32_t> resReported_;
    uint64_t resSeen_ = 0;
    void reportNewResources();
    void resetResources();
    socket::Handle fd_ = -1;
    FrameReader reader_;
    uint32_t key_ = 0;
    uint16_t seq_ = 1;
    SessionStats st_;
    std::unique_ptr<ResourcePump> res_;
    void* wireFile_ = nullptr;  // FILE*
    std::chrono::steady_clock::time_point t0_;
    struct Recent {
        bool c2s;
        uint16_t type;
        size_t len;
        std::chrono::steady_clock::time_point at;
    };
    std::vector<Recent> recent_;
    size_t recentNext_ = 0;
    void noteFrame(bool c2s, uint16_t type, size_t len);
    std::function<void()> pumpHook_;
    bool wake_ = false;
    std::unique_ptr<socket::ReadWait> readWait_;
    bool prodDue_ = false;      // the first ResourceList was handled: send Ready once the buffered frames are drained
    bool sentProd_ = false;     // the Ready that answers the first ResourceList went out
    bool sentRefresh_ = false;  // the ConnSetting + WorldRefresh that answer the first SelfRef went out
    // Keepalive state (handle(), case WorldState): the first follows the 25th WorldState, then one every 1000.
    static constexpr uint16_t kKeepaliveFrames = 1000, kKeepaliveFirstCount = 975;
    uint16_t pingCount_ = kKeepaliveFirstCount;
    bool pingOutstanding_ = false;
};

}  // namespace fugue::net
