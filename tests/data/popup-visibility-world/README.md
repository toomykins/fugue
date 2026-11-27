# Popup visibility / last-writer fixture

Handwritten world, recorded with stock DreamDaemon and DreamSeeker **516.1688**
on Windows (2026-10-25). `dreamseeker-observations.json` contains all 15 phases:
DM `winget` visibility/title, page ping count, on-close count, and titles of visible
native popup windows belonging to that exact owned DreamSeeker process.

Copy sources into a disposable build directory, compile `world.dme`, start its
DMB on an unused port, then connect a separate `-new -guest` stock client. The
world logs `PHASE {json}` records and finishes with `POPUP_PROBE_DONE`. Observe
owned native windows at each phase, excluding the main window (`Popup visibility
main`). No clicks or account access are required. Stop only owned processes.

Verified: server hide/show, hidden page execution, page-side hide/show, unrelated
world changes, identical repeated world writes, re-browse visibility reset, page
vs world title writes, and programmatic close without invoking on-close.
The C++ regressions live in `shell/tests/browse_test.cpp`; they also test immutable
property-write stamps and a GUI-delayed browse followed by a newer server hide.

Original local artifacts: `build/popup-visibility-probe/` (`stock-observations.json`,
`fugue-before-observations.json`, subsequent `fugue-*-observations.json`). Native
observations recorded process ownership by PID, creation time and executable,
not process names. The stock observation process was recorded in
`build/probes/20261025T043522Z-1440111b/`.
