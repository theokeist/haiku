# Glue Code & Anti-Progress Report

This report summarizes repository statistics, where application glue is implemented and injected by the build system, and "anti-progress" hotspots (stubs, TODOs, legacy placeholders) found in the tree. Use this as a triage starting point for improving system completeness and for checking runtime startup correctness.

---

## Quick Repository Statistics (fast scan)
- C++ source files (*.cpp): 5,940
- C source files (*.c): 2,357
- Header files (*.h): 7,974

Notes: counts come from a fast repository-wide search and include tools/3rdparty/tests.

---

## Glue-code: what, where, why

Why glue matters
- Glue code initializes the C/C++ runtime (constructors) and finalizes it (destructors), and provides the correct program entrypoint wiring for Haiku executables.
- Missing/incorrect glue can produce static-init bugs, improper shared-object behaviour, or runtime crashes.

Primary glue locations
- `src/system/glue/`
  - Top-level files: `crtbegin.c`, `crtend.c`, `haiku_version_glue.c`, `init_term_dyn.c`, `start_dyn.c`
  - Per-architecture glue in `src/system/glue/arch/` (one subfolder per supported architecture)

Where glue is injected in the build
- `build/jam/MainBuildRules` defines the `AddSharedObjectGlueCode` rule.
  - This rule sets `LINK_BEGIN_GLUE` and `LINK_END_GLUE` and adds the per-arch glue objects to the target.
  - When building for `haiku` the rule also sets `-nostdlib` and adjusts `NEEDLIBS` and `LINKFLAGS`.
- The `Application` Jam rule calls `AddSharedObjectGlueCode <target> : true` for executables, so every app built via the `Application` rule receives glue.

Key build variables to check when modifying or investigating glue
- `HAIKU_<TYPE>_BEGIN_GLUE_CODE_$(TARGET_PACKAGING_ARCH)` and `HAIKU_<TYPE>_END_GLUE_CODE_$(TARGET_PACKAGING_ARCH)` — per-arch glue symbols
- `NEEDLIBS`, `LINKFLAGS`, `TARGET_HAIKU_COMPATIBILITY_LIBS`
- Jam rules: `Application`, `AddSharedObjectGlueCode`, `LinkAgainst`, `AddResources`

Glue files to inspect first
- `src/system/glue/crtbegin.c`
- `src/system/glue/crtend.c`
- `src/system/glue/start_dyn.c`
- `src/system/glue/init_term_dyn.c`
- `src/system/glue/haiku_version_glue.c`
- `src/system/glue/arch/*` (per-arch implementations)

---

## Anti-Progress Hotspots (stubs, TODOS, legacy placeholders)

Why these are "anti-progress"
- Stubs and frequent TODOs allow builds/tests to pass while masking incomplete implementations and integration problems.
- They increase maintenance burden and make it harder to verify correctness on real hardware/real stacks.

Categories & concrete examples

1) TODO/FIXME markers (signals unfinished work)
- Many TODOs were found across tests and components. Representative examples:
  - `src/tools/vmdkimage/vmdkimage.cpp` — `// TODO: fixme!`
  - Graphics/tests: `src/tests/servers/app/newerClipping/*`, `src/tests/servers/app/newClipping/*` — numerous TODOs about redraw, locking, deadlocks
  - Painter test TODOs: `src/tests/servers/app/painter/Painter.cpp` — several `// TODO` lines noting untested or temporary implementations

2) Stubbed libraries and compatibility stubs
- `src/system/libroot/stubbed/` — builds a stubbed `libroot.so` (empty-symbol library) used during some builds/tests
- `src/system/libroot/stubbed/libroot_stubs.c.readme` — instructions and usage of stub generation
- `src/system/libroot/stubbed/libroot_stubs_legacy.c` — legacy syscall stubs (many empty `_kern_*` stubs)
- Linker script `.stub` sections: `src/system/ldscripts/*` include `.stub` in text region layout

3) `localestub` usage in Jamfiles (widespread)
- Many Jamfiles compile tests and apps against `localestub` (test stubs) instead of full libraries. Examples: `src/preferences/*/Jamfile`, `src/tests/*/Jamfile`, `src/kits/*/Jamfile`.
- Relying on `localestub` obscures real linking/runtime issues until later integration steps.

4) Stub drivers / placeholder drivers
- Tests and some server code reference `stub` or `vesa` fallback drivers (e.g., `AccelerantHWInterface.cpp`), and boot/arch directories have `// Stub` comments for unimplemented sections.

5) Legacy artifacts
- Presence of `libroot_stubs_legacy.c` and many `// empty stub for R5 compatibility` lines indicate compatibility shims that should be audited and possibly cleaned up.

---

## Short impact analysis
- Glue-related bugs can manifest as startup failures, missing static constructors, or incorrect destructor ordering. Any change touching `src/system/glue/*` or the `AddSharedObjectGlueCode` Jam rule should be validated across architectures.
- Stubs/localestub usage reduces integration confidence: triaging and replacing prominent stubs with real implementations should be prioritized to avoid regressions that only surface late.
- TODO-dense areas in graphics and tests suggest potential deadlocks, missing redraw semantics, and untested paths — these are good candidates for focused engineering sprints.

---

## Recommended next actions (pick 1–3)
1. Generate a ranked TODO list (by file) showing number of TODO/FIXME occurrences; export as `todo_summary.csv`. This helps triage hotspots.
2. Produce a Jamfile map listing all occurrences of `localestub` and the targets that depend on it; use this to plan incremental stub replacements.
3. Audit per-architecture glue: list the exact glue object files per `TARGET_PACKAGING_ARCH` by parsing `build/jam/MainBuildRules` and the `src/system/glue/arch/` directories; verify presence for primary targets.
4. Replace high-impact stub(s) used by core system (e.g., key `libroot` stubs) with real implementations and run component build+tests.

If you want, I can run (1) now and produce the CSV and a small markdown triage with top-20 files by TODO count.

---

## Useful quick commands
Run these locally to reproduce similar quick scans (from repo root):

```bash
# Count files by extension
find . -type f -name "*.cpp" | wc -l
find . -type f -name "*.c" | wc -l
find . -type f -name "*.h" | wc -l

# Find glue files
ls -la src/system/glue

# Find TODO/FIXME occurrences and top files
grep -RIn "TODO\|FIXME\|XXX" src | awk -F: '{print $1}' | sort | uniq -c | sort -rn | head -n 50

# Find Jamfiles using 'localestub'
grep -RIn "localestub" src | sed -E 's/:.*//g' | sort | uniq
```

---

---

## Ranked TODO/FIXME Hotspots (Top 20 Files)

| Rank | File | Count |
|------|------|-------|
| 1 | `src/kits/debugger/dwarf/DebugInfoEntries.h` | 42 |
| 2 | `src/kits/media/MediaRoster.cpp` | 32 |
| 3 | `src/kits/interface/View.cpp` | 28 |
| 4 | `src/kits/tracker/PoseView.cpp` | 26 |
| 5 | `src/kits/interface/Window.cpp` | 25 |
| 6 | `src/system/kernel/vm/vm.cpp` | 20 |
| 7 | `src/kits/interface/Menu.cpp` | 20 |
| 8 | `src/system/kernel/fs/vfs.cpp` | 15 |
| 9 | `src/kits/debugger/controllers/TeamDebugger.cpp` | 13 |
| 10 | `src/tests/servers/app/painter/Painter.cpp` | 12 |
| 11 | `src/system/kernel/vm/vm_page.cpp` | 12 |
| 12 | `src/tests/servers/app/newerClipping/WindowLayer.cpp` | 10 |
| 13 | `src/system/kernel/team.cpp` | 10 |
| 14 | `src/system/kernel/cache/block_cache.cpp` | 10 |
| 15 | `src/kits/network/libnetservices/GopherRequest.cpp` | 10 |
| 16 | `src/kits/debugger/debug_info/DwarfTypes.cpp` | 10 |
| 17 | `src/kits/app/Application.cpp` | 10 |
| 18 | `src/tests/servers/app/newerClipping/drawing/AccelerantHWInterface.cpp` | 9 |
| 19 | `src/system/kernel/vm/VMAnonymousCache.cpp` | 9 |
| 20 | `src/system/kernel/cache/file_cache.cpp` | 9 |

**Key observations:**
- Debugger DWARF handling has the highest TODO density (42), indicating incomplete debug symbol support
- Media and interface kits have significant unfinished work (32, 28, 26, 25 TODOs respectively)
- Kernel VM, filesystem, and caching layers have scattered TODOs (15-20 each) — potential stability/performance concerns
- Graphics tests (painter, window layer) show incomplete test coverage and unvalidated rendering paths

---

## Jamfiles Using `localestub` (Partial List)

Over **50+ Jamfiles** across the tree use `localestub` instead of full libraries. Representative categories:

| Category | Examples |
|----------|----------|
| **Disk systems** | `bfs/`, `btrfs/`, `fat/`, `intel/`, `ntfs/` |
| **Mail daemon** | `inbound_filters/`, `inbound_protocols/pop3/`, `outbound_protocols/smtp/` |
| **Media add-ons** | `mixer/`, `multi_audio/` |
| **Network settings** | `dialup/`, `dnsclient/`, `ftpd/`, `hostname/`, `ipv4/`, `ipv6/`, `sshd/`, `telnetd/`, `vpn/` |
| **Screen savers** | `butterfly/`, `debugnow/`, `flurry/`, `glife/`, `gravity/` |
| **Translators** | `gif/`, `jpeg/`, `png/`, `bmp/`, and 20+ others |
| **Preferences & apps** | `appearance/`, `locale/`, `mail/`, `network/`, `tracker/`, and many more |

**Impact:** Tests and add-ons compiled against `localestub` stub libraries won't catch real linking or runtime initialization issues until integration/release testing. This increases the cost of late-stage bug discovery.

---

## Summary

**High-priority areas for focus:**
1. **Debugger DWARF (42 TODOs)** — Symbol parsing is incomplete; impacts debugging on all architectures
2. **Media & Interface kits (28–32 TODOs)** — UI responsiveness and audio handling may have unfinished features
3. **Kernel VM & FS (15–20 TODOs)** — Memory management and filesystem operations may be suboptimal or incomplete
4. **Stub reduction** — Replace `localestub` deps in high-impact add-ons (disk systems, mail daemon, network) with real libraries to increase integration confidence

**Glue verification:** No major issues detected in the glue-code build injection pathway; however, each architecture's per-arch glue objects should be verified after any changes to `AddSharedObjectGlueCode` or `src/system/glue/`.

---

If you want the full list of 50+ Jamfiles using `localestub`, or detailed per-architecture glue object verification, run the quick commands listed above or provide the next action.