# Crash investigation

This document owns the procedure for turning a Diffractor crash report into an
attributed defect: what a report contains, where symbols come from, how to make a
minidump resolve, and how to tell the faulting frame apart from the cause. What the
code does belongs to [implementation.md](implementation.md) and the owning subsystem
document; this is only how to find out which one is at fault.

## 1. What a report contains

`app_frame::crash` (src/app.cpp) zips and posts:

- `Diffractor-<version>-<build>-<yyyymmdd>-<hhmmss>.dmp` — a user minidump.
- `diffractor.log` — the crashing session.
- `diffractor.previous.log` — the session before it.

The post is a `crash.zip` attachment plus form fields carrying `calc_app_info` — the
same block the About tooltip shows with debug info on — under the subject
`Diffractor CRASH report`. The **Support** command (`send_info`, src/app_commands.cpp)
posts to the same endpoint with the same app-info block, but as `logs.zip` under the
subject `Diffractor LOG` and with no dump. Read the subject first: a `LOG` report is a
user asking for help, not a fault.

The dump is the part most likely to be missing. `create_dump` needs `dbghelp` and a
writable temp file, and when either fails the report is still sent with the logs alone
— the log then says `no minidump was written - reporting logs only`. A report with no
`.dmp` is therefore a real crash, not a truncated upload. Both temp artifacts are
deleted once the attempt finishes, so nothing on the user's machine is left to ask for
afterwards.

Reports are only sent when `setting.send_crash_dumps` is set, and only one thread
claims the handler (`df::handling_crash`), so a report is never a duplicate of a
second fault inside the first. The crashed-file skip list is written before any of
this and independently of it. Some faults produce no report at all — see
[implementation.md](implementation.md#crash-loop-protection) for what the handler
cannot reach and what covers those cases instead.

## 2. Read the logs before the dump

The log is cheaper than the dump and often decides the answer on its own. Take:

- **Line 1-2** — version, build, OS, bit-ness, `debug`/`release`, core count.
- **Startup block** — settings backend, GPU adapter and vendor/device IDs, whether
  the D3D11 device was created, `p010`/`nv12` support, image budgets.
- **Timeline** — every line is milliseconds since start. Compare the timestamp of the
  last log line with `Process Uptime` in the dump: a long gap means the fault has no
  logged neighbour and the log is only context.
- **The tail** — `*** CRASH ***`, plus `Last file type opened` and `Rendering
  function` (from `df::rendering_func`, maintained by `df::scope_rendering_func`) when
  they were set.
- **`crash_files_db` lines** — file types added to the skip list, and on the next run
  the count of files skipped because they crashed a previous run of the same release line.
- **The previous log** — if it shows the same anomaly, the crash is reproducible on
  that machine and the anomaly is a lead rather than a coincidence.

Anything a `__try`/`__except` or `catch` swallowed appears here and nowhere else. A
swallowed fault minutes before the crash is a suspect, not a footnote.

## 3. Symbols

`dd.ps1` publishes symbols with `Add-ToSymbolStore`, which runs
`tools\symstore add /r /f <file> /s c:\code\symbols /t Diffractor /compress`. Two
consequences:

- The store is `c:\code\symbols`, indexed by `<name>\<key>\<name>.ex_` / `.pd_`.
- Entries are **cab-compressed**, so they must be `expand`ed before a debugger can use
  them as an image.

Both the 32- and 64-bit desktop binaries and the Store `diffractor.exe` are published.

The console debugger is the WinDbg store app, not the Windows Kits install:

```powershell
$cdb = Join-Path (Get-AppxPackage *WinDbg*).InstallLocation 'amd64\cdb.exe'
```

## 4. The minidump trap

A user minidump does not contain the executable's PE header. `dbghelp` therefore
cannot read the debug directory, cannot find the PDB, and reports
`Image header paged out` / `(no symbols)`. A symbol path alone — `-y srv*...` — is not
enough, and pointing `-i` at the symbol server does not help either.

The image has to be on disk, uncompressed, and matched:

```powershell
# 1. module range: end - start gives the size, e.g. 0x31443000 - 0x2e3a0000 = 0x30A3000
& $cdb -z <dump> -c "lm m diffractor*; q"

# 2. store keys are <TimeDateStamp><size>, so filter by that size suffix
Get-ChildItem c:\code\symbols\diffractor64.exe | Where-Object Name -like '*30a3000'

# 3. expand the image and its PDB side by side
expand c:\code\symbols\diffractor64.exe\<key>\diffractor64.ex_ tmp\bin\diffractor64.exe
expand c:\code\symbols\diffractor64.pdb\<guid+age>\diffractor64.pd_ tmp\bin\diffractor64.pdb

# 4. -i supplies the image, -y the symbols
& $cdb -z <dump> `
  -y "c:\code\diffractor\tmp\bin;srv*c:\code\symbols*https://msdl.microsoft.com/download/symbols" `
  -i "c:\code\diffractor\tmp\bin" `
  -c ".lines -e; .reload /f; .ecxr; kv; q"
```

**Several store entries share one size suffix** — one per build that day. Pick by the
leading `TimeDateStamp`, or try each: the wrong image says `image header does not
match memory image header` and then `mismatched pdb`. The matching PDB key is the one
whose folder timestamp is minutes after the image's; `lmvm diffractor64` prints the
image `Timestamp:` once the right one loads, which confirms the pair.

Run cdb from a `.ps1` invoked as `pwsh -NoProfile -File`. An inline
`pwsh -Command "...$var..."` loses its variables to the outer shell.

## 5. Local debug builds

A dump from `exe\diffractor64-d.exe` usually has no stored symbols, and the local PDB
has been relinked since. Force the mismatched PDB on:

```
.reload /f /i diffractor64-d.exe=<base>,<size>
```

The result is approximate — treat function names as a strong hint, line numbers as
weaker — but it has been accurate enough to name the failing function and file. Verify
the answer against current source before acting on it.

## 6. Reading the fault

In order, and stopping as soon as the answer is unambiguous:

- `.ecxr` — switch to the stored exception context. Everything else is meaningless
  without it. Read the faulting instruction and the register it dereferenced; `rax=0`
  with `mov rax,[rax]` is a null virtual call, `0xcccccccc` is uninitialised debug
  memory, a plausible-looking-but-wrong pointer is a lifetime or corruption bug.
- `kv` — the stack. Frames below an unresolved module are unreliable, because x64
  unwinding needs that module's unwind info.
- `!analyze -v` — the bucket, `Failure.Exception.Code`, and `Timeline.Process.Start.DeltaSec`.
- `dps <rsp> <stack-top>` — a raw scan of the stack region. When `kv` dies at a
  system DLL this recovers the app frames above it, which is usually what identifies
  the feature involved.
- `~*k` — the other threads, to see what work was in flight.
- `?? setting.<field>` — globals read straight out of the dump. The fastest way to
  confirm which configuration produced the crash.

## 7. Attribution

**The faulting frame is not always the culprit.** Two cases recur:

- **Heap corruption** (`!analyze` bucket `HEAP_CORRUPTION`, original exception
  `0xC0000374`). The fault is in `RtlpFreeHeapInternal` on some innocent free, often
  inside a system DLL. The corrupter ran earlier and left no frame. Go back to the log
  and look for a swallowed fault, a third-party or driver call, or an operation that
  writes a buffer — and check whether the previous session shows the same thing.
- **Deliberate test crashes.** A three-line log (`main` version, OS, `*** CRASH ***`)
  with a process uptime of a few seconds is a `/test:` console run, not a user
  session. Check the stack for `run_console_tests` and check
  [v-next.md](v-next.md) before treating it as a defect — some negative cases are
  verified by observing `0xC0000005`.

An attributed crash needs three things that agree: the faulting instruction, a
mechanism in our code that produces it, and something in the log or the dump's globals
that says this configuration took that path. Two out of three is a hypothesis.

## 8. Afterwards

- Expanded images and PDBs are hundreds of megabytes; delete them from `tmp\` when
  done. They are reproducible from the store at any time.
- If the root cause is a setting, a driver, or a fallback that did not fire, record the
  invariant in the owning document so the next reader does not re-derive it.

## Where this lives

| Crash-handling subject | Source |
|---|---|
| Exception filter, dump writing, report contents | [app_toolbar.cpp](../src/app_toolbar.cpp) |
| The open-file list a fault records, and its bounds | [util_crash_files_db.h](../src/util_crash_files_db.h) |
| The graphics crash guard and hardware-acceleration fallback | [platform_win_settings.cpp](../src/platform_win_settings.cpp) |
| The degraded start after two unsettled launches | [app.cpp](../src/app.cpp), [app_settings.cpp](../src/app_settings.cpp) |
| Diagnostic log and session counters | [util_log.h](../src/util_log.h), [util.h](../src/util.h) |
| Symbol store tooling | `tools/symstore.exe`, `tools/symsrv.dll` |

The guard's own behavior is tested — the crash-guard recovery session and DXGI device loss are in
[test_platform_win.cpp](../src/test_platform_win.cpp), the only test file permitted system headers.
What is *not* tested is the faulting path itself, so treat a change to the exception filter as
unverified by the suite and say so.
