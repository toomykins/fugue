# Unattached browser-pane startup

This handwritten world verifies that a BROWSER whose own `is-visible` is true
executes its page before its parent pane is attached to any CHILD control.
The page's ready Topic causes the server to attach it. A second Topic proves the
same document/token survived attachment and the startup script ran exactly once.

`dreamseeker-observations.txt` records stock DreamDaemon / DreamSeeker 516.1688 on
Windows, guest (2026-10-25). To reproduce, copy the sources to a disposable build
directory, compile `world.dme`, host on an unused port, and connect a separate
stock client using `-new -guest`. Wait for `BROWSER_PANE_PROBE_DONE` in the server
log. Stop only owned processes.

Regression: `DockTest::anUnattachedPaneRunsBeforeAttachment` in
`shell/tests/browse_test.cpp`. No external game assets/code are required.
Local evidence: `build/browser-pane-probe/`; stock owner:
`build/probes/20261025T044540Z-f300d321/`.
