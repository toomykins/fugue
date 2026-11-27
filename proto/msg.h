// Wire message types, named by what each message is. The hex value of a message type is written here and nowhere else
// in client code; decoders also carry it in their file name (proto/dec_s2c_<type>_<name>.cpp).
//
// The two directions have independent type spaces: the same number can mean different things each way, and some
// messages (Alert, Input, Winget, ...) use one number for a request and its answer. Field layouts are in the decoders
// and encoders, and in docs/protocol/MESSAGE-CATALOG-516.md (s2c) and docs/protocol/GAME-CLIENT-SERVER-516.md.
//
// Each direction is an unscoped enum inside a namespace: S2C::SelfRef reads like an enum class member, but converts
// to the uint16_t type field every frame carries, so it compares, switches and passes like the number it names.
#pragma once
#include <cstdint>

namespace fugue {

// Server to client.
namespace S2C {
enum Type : uint16_t {
    SessionEnd = 0x0000,        // [reason u16][required version u32]: the server ends the session
    Handshake = 0x0001,         // the handshake reply (nonce, flags)
    WorldState = 0x0007,        // per-tick eye, view and object-array update for the client's mob
    InView = 0x0008,            // the ten object arrays for objs in view
    MobBind = 0x0009,           // [ref][appid]: binds a mob record's appearance
    ContentsDelta = 0x000a,     // usr.contents entering / leaving, objs and mobs
    ResourceList = 0x000e,      // the resource availability list ([id u32][kind u8]...)
    Verb = 0x0011,              // one verb the client may run
    MapRefreshFull = 0x0012,    // map-window appearance grid, always a full refresh
    MapRefresh = 0x0014,        // map-window appearance grid, a full refresh only on a z change
    Keepalive = 0x0016,         // empty: the echo of the client's Keepalive
    CertChallenge = 0x001c,     // a hub-registered server's login-certificate challenge
    Reboot = 0x001e,            // empty: the world reboots, the client logs in again
    AreaGrid = 0x0020,          // per-cell area indices and the area appearances
    ObjBind = 0x0024,           // [ref][appid]: binds an obj record's appearance
    Appearance = 0x0026,        // one appearance, by appid
    Text = 0x0027,              // plain text output to the default control
    ImageCreate = 0x0033,       // an /image enters client.images
    ImageDelete = 0x0034,       // /images leave client.images
    ImageModify = 0x0035,       // an /image in client.images changes
    CertAck = 0x0036,           // the server accepts the CertAnswer; the session cipher is re-keyed
    Timing = 0x0037,            // [tick ms u32][client frame ms u32]
    ClientAppearance = 0x0038,  // the client's own appearance (mouse pointer icons)
    ResolveReply = 0x0039,      // [kind u8][value u32][u32]: the value a ListResolve / InputResolve names
    MouseSubs = 0x003b,         // which mouse procs the world defines, and other client flags
    Bounds = 0x003e,            // world maxx/maxy and the view size
    ExpansionReply = 0x003f,    // [count u16][cstring]*count: the choices a choices request asked for
    MapGrid = 0x0061,           // the viewport turf grid: resets the map window
    Input = 0x0062,             // an input() prompt
    StatPanel = 0x006a,         // stat panel tabs and rows
    FlickState = 0x006c,        // flick(): the icon state to play on an atom
    Sound = 0x006d,             // sound(): play, stop or update a sound
    MobGone = 0x006e,           // [id u32]...: mob records the server no longer has
    ObjGone = 0x006f,           // [id u32]...: obj records the server no longer has
    ClientDir = 0x0070,         // [dir u8]: client.dir
    SelfRef = 0x0071,           // [ref u32]: the client's own mob; the first one means "logged in"
    WorldName = 0x0072,         // the world's name (cstring first)
    Hud = 0x0076,               // client.screen membership
    Link = 0x0079,              // link(url)
    Browse = 0x0084,            // browse(html, params)
    Probe = 0x008f,             // a liveness probe: answered with ProbeEcho carrying the same body
    ProbeEcho = 0x0090,         // the echo of the client's Probe
    Alert = 0x0092,             // an alert() prompt
    PromptWithdraw = 0x009d,    // the server withdraws an alert() / input() it is no longer waiting on
    Resource = 0x009f,          // resource transfer: offer and data
    Extended = 0x00a0,          // framing escape for bodies over 0xffff bytes (net/frame.h)
    Output = 0x00a7,            // output(value, control)
    Winset = 0x00a8,            // winset(control, params)
    Winget = 0x00a9,            // winget(control, prop): answered with C2S::Winget
    SkinText = 0x00af,          // the world's .dmf interface as text
    WinClone = 0x00b1,          // winclone(template, name): a new window from a skin template
    WinShow = 0x00b2,           // winshow(window, flag)
    HostMessage = 0x00b6,       // a message from the host, shown as a `*** ... ***` chat line
    FlickTarget = 0x00c7,       // sent before a flick: the atom it plays on, and its appearance
    VisContents = 0x00d8,       // a turf's (or obj's, mob's) vis_contents, and a turf's appearance
    SkinResource = 0x00ef,      // the world's .dmf interface as a resource id
    Animate = 0x00f0,           // animate()
    VisTurfs = 0x00f1,          // the remote turfs a vis_contents names
    MeasureText = 0x00fc,       // a text layout request: answered with C2S::MeasureText
    BrowseRsc = 0x00fe,         // browse_rsc(file, name): a resource registered under a file name
};
}  // namespace S2C

// Client to server.
namespace C2S {
enum Type : uint16_t {
    Handshake = 0x0001,         // the opening handshake (net/handshake.h)
    Command = 0x0002,           // the input command: verbs, mouse, movement (proto/enc_c2s_0002_input.h)
    ResyncMob = 0x000b,         // [id u32]: asks the server to re-send a mob record (model/resync.h)
    AppearancePoll = 0x000c,    // asks the server to re-send an appearance (model/appgc.h)
    ConnSetting = 0x000d,       // [u16 127], sent once after the first SelfRef; stored by the server
    Keepalive = 0x0016,         // empty: sent every 1000 WorldState frames
    WorldRefresh = 0x0019,      // empty: asks for the whole world, sent once after the first SelfRef
    Login = 0x001a,             // the login credential (guest or account record)
    CertAnswer = 0x0036,        // the hub-minted answer to S2C::CertChallenge
    ListResolve = 0x0039,       // asks which value an `in <list>` argument's text names
    ResourceDeclare = 0x003c,   // declares the resource ordinals the client wants
    ResyncObj = 0x003d,         // [id u32]: asks the server to re-send an obj record (model/resync.h)
    VerbExpansion = 0x003f,     // asks for the choices of a verb argument
    Input = 0x0062,             // the answer to an input() prompt
    InputResolve = 0x0063,      // asks which value an input() in-list pick names
    InputExpansion = 0x0064,    // asks for the choices of an input() in list
    StatTab = 0x006b,           // selects a stat panel tab
    AppearancesFreed = 0x0073,  // appearance ids the client has dropped (model/appgc.h)
    Topic = 0x0081,             // a byond:// Topic link
    Probe = 0x008f,             // a liveness probe: the server echoes it as S2C::ProbeEcho
    ProbeEcho = 0x0090,         // the echo of the server's Probe
    Alert = 0x0092,             // the answer to an alert() prompt
    Resource = 0x009f,          // resource transfer: pull
    Extended = S2C::Extended,   // the framing escape is the same in both directions
    Winget = 0x00a9,            // the answer to S2C::Winget
    Ready = 0x00b7,             // empty: the client is ready for the world; answers the first ResourceList
    MeasureText = 0x00fc,       // the answer to S2C::MeasureText
};
}  // namespace C2S

}  // namespace fugue
