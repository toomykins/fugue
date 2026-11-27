# Contributing to Fugue

Thanks for helping. Fugue is a native client for BYOND games; the goal is to behave like stock DreamSeeker, so the
most valuable contributions are fixes that make a real game look or act the way it does in DreamSeeker, with a test
that pins it.

## Build and test

Prerequisites, options and the test command are in [docs/BUILDING.md](docs/BUILDING.md). A development build (no
`-DFUGUE_RELEASE=ON`) keeps the test hooks. For work that does not touch the window, `-DFUGUE_HEADLESS_ONLY=ON` builds
only the Qt-light modules and their tests.

QtTest suites share the `fugue_tests` executable, with CTest selecting a suite for each test process. Core, account and
pager test sources are glob-discovered; shell tests are registered in `shell/CMakeLists.txt`. Use the matching
`FUGUE_TEST_MAIN`, `FUGUE_TEST_GUILESS_MAIN` or `FUGUE_TEST_APPLESS_MAIN` entry from `tests/testmain.h`;
custom startup functions use `FUGUE_TEST_ENTRY` in place of `main`. A scenario can have
separate CTest entries for portable logic, GPU replay and native window integration, sharing its inputs and expectations.
Add only the profiles the scenario needs; see
[test profiles and commands](docs/BUILDING.md#test-profiles). Keep individual expectations and reproduction details
with the test source or fixture README.

Automated tests must not need external network services or a BYOND installation. Portable tests keep their input files
inside the repository; explicit capability profiles may require their declared display, device or installed font files.
Tests that replay a recorded session use data under `tests/data/`; a recording should come from a small purpose-written
world rather than a real game, so the expected values are understood and the data is ours to publish. Stock observations establish parity;
explicit Fugue invariants establish requirements such as bounded work and correct lifetimes.

Test an optimised build (`RelWithDebInfo`, the default of `tools/test.ps1` and of a single-config tree); `Debug` runs
the same checks several times slower. `ctest -L fast -j 8` is the inner loop. Please run the portable suite and the
affected capability profiles before sending a change. Report unavailable profiles and
skipped cases separately; they do not establish verification.

## Scenario lifecycle

Every test should make its **setup, inputs and checkpoints → observations → explicit expectation** easy to find.
Keep this in ordinary test code and the fixture's README; no scenario framework or common file format is required.
A checkpoint can be a completed function call, a complete static phase, a recorded instant, or an observed submission.
State which one the claim needs.

1. **Observe.** Reproduce stock-parity behavior in a small world. Record the stock version, setup, actions, timing
   or completion condition, and relevant display conditions. A server acknowledgement alone does not prove that the
   client presented the new state. Mark unknown conditions as unknown; replay clocks are not capture timestamps.
2. **Retain.** Keep the inputs and independent expectations consumed by the test, with their provenance. Expectations
   may be values, receipts, pixels, or a documented transcription. Identify transcriptions and missing original
   evidence explicitly. Never regenerate a stock expectation from Fugue's output to make a test pass.
3. **Replay.** Start from known state, deliver inputs in order, and sample production code at the declared checkpoints.
   Compare observations with the retained expectations. Validate required inputs so missing data cannot remove checks
   silently. Distinguish stock parity, implementation equivalence, and engineering invariants in the test's claim.
4. **Check native integration.** Add checks when the claim crosses a window, presentation, input, or device boundary.
   Reuse the scenario's expectations, wait for the relevant observable completion, and register the required CTest
   profile explicitly. A skipped native check is missing verification.

Pure engineering invariants use the same test shape with an explicit requirement as their expectation; they need no
stock capture. Add only the execution profiles the claim requires. Document what an existing fixture cannot prove
before extending its claims, especially when arrival timing, stock output, or native evidence was not retained.

## Adding code

* Modules are directories whose `CMakeLists.txt` calls `fugue_module(<target>)`, which globs the directory's
  `*.cpp` into an object library (some modules add tools or options after it). New source files in an existing module
  need no build-list edit. A new module directory needs its own `CMakeLists.txt` and one `add_subdirectory` line in the
  top-level `CMakeLists.txt`.
* A protocol message decoder is one file, `proto/dec_<s2c|c2s>_<type hex4>_<name>.cpp`, that registers itself with
  `FUGUE_REGISTER_DECODER`. Parse with the bounds-checked `proto::Reader`; on a malformed body throw instead of
  guessing. The rule is correct framing or "unknown", never a guessed width.
* Includes are rooted at the repository root: `#include "net/cipher.h"`.
* C++20. `net/`, `proto/`, `model/`, `skin/`, `account/` stay free of Qt GUI types; Qt Widgets and WebEngine live in
  `shell/`, `pager/ui/` and `app/`.

## Code style

* Follow the formatting of the file you are editing: four-space indent, braces on the same line, short lines (the
  code base wraps near 120 columns).
* Comments explain why, not what, and stay on the code: no history, no account of who found a problem or how it was
  tested. When a behaviour exists because stock BYOND does it, state that fact in a line or two (and the stock
  version). If a rule only approximates stock, say so in the comment.
* A change that alters rendering or protocol behaviour should come with a test, and where possible with the stock
  behaviour it was checked against. If you cannot check against stock BYOND, say so in the commit message.
* Do not commit BYOND binaries, game assets, credentials, or output copied out of a decompiler. Describe facts and
  structure in your own words. Third-party code or data needs a licence note in `THIRD-PARTY-NOTICES.md`.

## Commit messages and pull requests

Describe what changed and why in the first line, then what you tested and how. Keep unrelated changes in separate
pull requests.

## How changes reach `main`

Fugue is developed in a separate tree and published here. A merged pull request is carried into that tree with your
authorship and message intact, so the next published update keeps your change and its credit. Base pull requests on
the current `main`; history on `main` is never rewritten. Pull requests from new contributors need a maintainer's
approval before CI runs, and are reviewed for security before anything in them is built.

Releases are tags (`vX.Y.Z`): patch versions for fixes, minor versions for new features. An issue fixed on `main` is
labelled `fixed-in-main` and closed once the fix is confirmed in a real game.
