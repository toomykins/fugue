// DreamSeeker's command line as stock 516.1684 accepts it, so fugue-qt can stand in for it: a pager, a byond://
// handler, or `dreamseeker "byond://host:port" -pager` gets the same result from Fugue.
// Matches stock DreamSeeker 516.1684:
//   * A '-' argument is matched case-insensitively against -trace, -pager, -trusted, -safe, -ultrasafe, -isserver and
//     -is-server; -debug is case-sensitive. Any other '-' argument is dropped and is never the target. The self-updater
//     forms ({-install a b c}, {-updated}) act only in a standalone hub-game build; in stock DreamSeeker they are
//     ordinary unknown flags.
//   * A '+' argument swallows the next one. Only the exact `+connect` uses it, and only when another argument follows its value.
//     Its value is the target, with "byond://" prepended unless already present. A later +connect replaces the target.
//   * Anything else is the target if none is set yet (stored raw), else it is dropped.
//   * -trusted / -safe / -ultrasafe set one field: the last one wins.
// Fugue's own options (`--name`, `--name=value`, `--name value` for a value-taking name) are not DreamSeeker's. Invoked as
// DreamSeeker, an unknown `--word` is dropped as DS drops an unknown '-' argument; as fugue-qt it goes to Fugue's parser.
// Qt's own arguments are removed by QApplication before Fugue reads argv, but DreamSeeker counts them: they are passed in
// `hostTaken`, so DS's argc and indices stay DS's.
#pragma once
#include <set>
#include <string>
#include <vector>

namespace fugue::pager {

struct DsArgv {
    std::string target;                // raw for a bare argument, byond://-prefixed for +connect; "" = none
    int targetArg = -1;                // index in args of that bare argument or of its +connect
    bool trace = false;                // -trace
    bool pager = false;                // -pager
    int security = -1;                 // -trusted 0 / -safe 1 / -ultrasafe 2; -1 = not given; the last one wins
    bool isServer = false;             // -isserver / -is-server (sets the skin parameter IsServer)
    bool debug = false;                // -debug (case-sensitive)
    std::vector<std::string> dropped;  // what DreamSeeker would read and ignore, in order (reported, never acted on)
    std::vector<std::string> fugue;    // Fugue's own --options (and their values), in order, for its own parser
};

struct FugueOptions {
    std::set<std::string> value;  // names (without "--") of Fugue's options that take a value
    std::set<std::string> flag;   // names of Fugue's options that take none
    bool keepUnknown = true;      // an unknown --word goes to Fugue's parser (false = dropped, as DreamSeeker drops it)
};

// args = argv[1..] (argv[0] excluded) as DreamSeeker would see them. hostTaken[j] = args[j] was consumed by Qt before Fugue
// saw argv: it counts toward DS's argc and indices and is never a target, a flag or a +word's value. Shorter = not taken.
DsArgv parseDsArgv(const std::vector<std::string>& args, const FugueOptions& fugueOpts,
                   const std::vector<bool>& hostTaken = {});

// hostTaken for parseDsArgv, from the argv[1..] pointers before and after QApplication ran. Matched by POINTER IDENTITY, never
// by text: Qt compacts argv in place, so a kept argument keeps its char* and a removed one's pointer is gone. Equal strings are
// not interchangeable. Before {-style, +connect, +connect, h:1} with Qt taking `-style +connect` leaves {+connect, h:1}: a text
// match would treat argv[1]'s +connect as surviving, but argv[2]'s is the one left and its bound (i+2 < argc) fails.
std::vector<bool> takenByHost(const std::vector<const char*>& before, const std::vector<const char*>& after);

// True when argv[0]'s basename is dreamseeker or dreamseeker.exe (case-insensitive): Fugue was installed AS DreamSeeker.
bool invokedAsDreamSeeker(const std::string& argv0);

// The binary's mode switch (app/launcher.cpp main). args = argv[1..]. A leading `pager` / `client` / `version` subcommand
// picks the mode and is removed; `--version` anywhere picks "version"; otherwise a release build is the pager (a player starts it from a menu with no arguments)
// and a development build the client. Invoked as DreamSeeker the mode is ALWAYS the client and no subcommand is read: a bare
// `pager` is a target to DreamSeeker, and is one here too.
struct Mode {
    std::string mode;               // "pager" | "client" | "version"
    std::vector<std::string> rest;  // args with the subcommand removed
    bool asDreamSeeker = false;
};
Mode chooseMode(const std::string& argv0, const std::vector<std::string>& args, bool release);

// What a client run does with no target. Stock DreamSeeker 516.1684: no argument opens a lone
// "Open Location" box (OK with an address joins it, Escape exits); arguments with no target (`-trace`, `-updated`,
// `+connect h:p`) open no window and exit. Fugue's own --options with no target are not a DreamSeeker command line: usage
// line, exit 2.
enum class NoTarget { OpenLocation, QuietExit, Usage };
NoTarget noTargetAction(size_t dsArgc,
                        const DsArgv& d);  // dsArgc = DreamSeeker's argc (argv[0] counted, Qt's args too)

}  // namespace fugue::pager
