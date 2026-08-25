# HISTORY.md — Objective Chronological Archive

## 2026-08-25 — Agent Session: Namespace slice 4 — legacy virtual paths REMOVED (complete migration)

### Context
Directive: resolve aliasing completely — actually move resources to their canonical `0/` homes rather than mapping old spellings onto new ones. Decision recorded in STATE: legacy Unix-style paths are removed entirely; Plan0 has exactly one namespace.

### Changes

#### `src/fs/vfile.c` + `include/vfile.h`
- Registry re-homed: `0/system/processes|time|version|timer|stat|interrupts|mounts|heap`, `0/system/hostname` (rw), `0/system/kernel/{name,version,arch}`, `0/system/runtime/uptime`, `0/system/self/{pid,name,status,fdinfo}` + `0/system/self/fd` dir with dynamic `/N` read-through, `0/hardware/memory/{ram,info}`, `0/hardware/cpu`, `0/hardware/cpu-id`, `0/hardware/platform`, `0/hardware/pci`, `0/dev/{null,zero,console}` (rw).
- Directory listings rebuilt for the new tree (`ls /` now shows `0`; 0/, 0/system, kernel/runtime/self, hardware/memory listings).
- **Hostname deduplicated**: the old twin registrations (`/proc/hostname` + `/sys/kernel/hostname`) collapse into the single canonical `0/system/hostname`; wrapper generators deleted.
- VFS mount changed from `/proc`,`/sys`,`/dev` to a single `/0` plumbing prefix (NULL-ops → vfile adapter routing unchanged).
- Dynamic fd prefix updated in `vfile_exists`/`vfile_read` and in `vfs.c`'s size check.

#### `src/fs/ns.c` + `include/ns.h`
- New kinds: NS_SYSTEM_DIR/NS_SYSTEM_NODE, NS_DEV_DIR/NS_DEV_SHIM, NS_HWINFO.
- Classification: entire `0/system/**` subtree is content-addressable (leaf validation delegated to the backend); `0/dev/*` likewise; hardware info leaves (`platform`, `pci`, `cpu-id`, `memory/info`) classified.
- Translation extended: system/dev/hw-info/memory/cpu kinds map to internal `/0<canonical>`; partition/user remain volume-relative. Fixed two translation bugs found by QEMU probing: content kinds produced `/00...` (canonical already starts with the domain digit), and an int/size_t sign-compare.

#### Consumers
- Host tests: `test_vfile.c` fully migrated to `/0`-internal spellings + semantic updates (single namespace mount, new listing expectations, hostname dedup, O_CREAT-under-/0 rejection); `test_ns.c` legacy-passthrough assertions now use synthetic `/legacy/...` spellings; translation suite gained system/dev/memory cases. Net 167 tests (two duplicate hostname tests removed).
- Drive scripts (14): all legacy path arguments migrated (`drive_vcat`, `drive_vfile*`, `drive_writable`, `drive_fdinfo`, `drive_heap`, `drive_stdio`, `drive_redir`, `drive_cmds`, `drive_mounts`, `drive_shell_process`, `test_process_shell`, `drive_parts`, `drive_ns`); drive_ns gained an explicit legacy-rejection check.
- AGENTS.md pitfall #9 rewritten for the namespace model.

### Debugging note
Migration was driven by throwaway scripted rewrites; two scripted-corruption bugs (`/00/dev/...` double-prefix from an over-broad doc map, and slash-consuming replacements inside test literals) were caught by the host suite and fixed, after which clean builds were treated as the only source of truth.

### Verification
- Kernel build clean (0 compiler warnings).
- Host suite: **167/167**.
- QEMU/WHPX: `drive_vcat.py` **72/72** (uptime/version/processes/kernel-arch through fd layer under new paths, grep+wc on `0/system/runtime/uptime`, dev shims), `drive_parts.py` **13/13**, `drive_ns.py` **6/6** (incl. `ns /proc/uptime` → invalid-path-syntax rejection on real hardware).

### Results
- There is exactly ONE namespace: every resource lives at its `0/...` address; Unix-style paths fail by design ("File not found" / "invalid path syntax").
- Remaining addressing work: directory listing for namespace nodes via ns (structural dirs currently EKIND for file ops), `0/user/` binding introspection, then non-addressing backlog (AHCI, debug infrastructure).

## 2026-08-25 — Agent Session: Namespace slice 3 — shell speaks the namespace

### Context
Next addressing-level slice: shell path-taking commands accept Plan0 namespace paths (`0/...`) with resolution through the resolver, legacy `/`-rooted syntax preserved bit-for-bit. Also surfaced and fixed a real semantic gap: storage device classes were resolving even when no hardware of that class existed.

### Changes

#### `src/fs/ns.c` + `include/ns.h`
- **Partition depth fix**: partition resources resolve at ANY depth below `partitions/<n>` (e.g. `.../partitions/1/docs/x.txt`) instead of unknown at depth 7 — an off-by-one that had hidden volume-content addressing.
- **`ns_to_fs_path()`**: shell-argument translation. Non-namespace args (no `<digit>/` prefix) pass through; `<device>/partitions/<n>/rest` → `/rest` only when n is the mounted volume (`volume_base_lba` comparison); `0/user/rest` → `/rest` (logical user root v1 binds the active volume); typed errors NS_FS_EPARSE / EUNKNOWN / EVOLUME / EKIND. Output never longer than input → safe in-place replacement.

#### Topology honesty (device classes)
- **`disk.c`/`disk.h`**: caches ATA IDENTIFY word 217 (nominal media rotation rate; 0x0001 = solid state) → `disk_is_ssd()`.
- **`part.c`/`part.h`**: `part_storage_device()` returns "hdd"/"ssd" for the single detected device, 0 without hardware.
- **`ns.c`**: storage branches classify ONLY the owning class; the absent class answers unknown. Previously `0/hss/...` silently resolved to the same partitions as `0/hsh/...` — a topology lie this session removed.

#### `src/ui/shell.c`
- One pre-pass at the top of `dispatch()` translates arg1/arg2 via `ns_to_fs_path` (except the `ns` command, which consumes raw namespace paths). Every path-taking command (cat/read/write/edit/ls/go to/mkdir/delete/copy/move/find/grep/wc/info/chmod/vcat) now accepts namespace paths; handlers untouched; legacy behavior identical. Help text documents the syntax.

#### Tests
- `tests/unit/test_ns.c` rewritten around truthful fixtures (11 tests): grammar, alias mechanics (incl. boundary safety), alias≡canonical descriptor equivalence, classification WITH topology-honesty assertions (absent ssd class unknown; no hardware → no storage classes at all), volume-tree resolution below partitions/N, translation suite (passthrough/partition/user/errors incl. absent-device vs absent-partition distinction).
- `tests/mocks/mock_kernel.c`: `mock_disk_set_ssd()` + `disk_is_ssd()` mock.

#### Tools
- `drive_parts.py`: namespace file-op checks — write via `0/hsh/partitions/1/nsfile.txt`, read via `0/user/nsfile.txt`, legacy `/nsfile.txt` reads the same resource.
- `drive_ns.py`: boots with the GPT image; hdd describe+alias checks; NEW negative check `ns 0/hss` → "Unknown resource" (topology honesty on real hardware).

### Debugging note
A wild-goose chase during test bring-up traced to stale incremental builds masking edits (clean builds were authoritative); the actual defect was the partition depth off-by-one above. Clean-build discipline reaffirmed.

### Verification
- Kernel build clean (0 compiler warnings).
- Host suite: **169/169**.
- QEMU/WHPX: `drive_parts.py` **13/13** (namespace write + user-alias read + legacy equivalence + all image-inspection proofs), `drive_ns.py` **6/6**, no-disk regression `drive_vcat.py` **72/72**.
- Intermittent WHPX boot stalls observed again (different stages, clean on retry) — environment-level, documented in STATE.

### Results
- Shell syntax revised: every path-taking command now addresses resources through the unified namespace while remaining 100% backward compatible.
- Remaining addressing work: legacy `/proc`,`/sys`,`/dev` aliasing through the resolver; vfile canonical re-registration under `0/system/*`, `0/dev/*`; directory listing for namespace nodes (`ls 0/hardware/storage`).

## 2026-08-25 — Agent Session: Namespace slice 2 — filesystem binds a partition volume

### Context
Next slice per docs/NAMESPACE.md §16/§23 and STATE known-issue #5: make `0/hardware/storage/<dev>/partitions/<n>` a REAL fs volume — the filesystem must live inside the partition (offset from its start, size clamped to it) instead of formatting over whole-disk LBA 0, which previously destroyed the protective MBR on GPT disks.

### First attempt (corrected)
Initially placed the binding (`fs_use_partition` etc.) directly in fs.c and compiled fs.c into the host suite — which collided at link time with the existing functional fs_* mock in mock_kernel.c. Restructured: the binding lives in its own module; fs.c only consumes it.

### Changes

#### `src/fs/volume.c` + `include/volume.h` (NEW)
- `volume_use_partition(n)` validates n against the live dense partition list and stores base LBA + sector count; `volume_use_whole_disk()` restores legacy (base 0, unbounded); getters `volume_base_lba()`/`volume_sectors()`.

#### `src/fs/fs.c`
- Boot-sector write (format) and boot-sector read (mount) target the volume base instead of absolute LBA 0.
- `read_cluster`/`write_cluster` offset every cluster by the volume base.
- `fs_format` clamps requested disk_size to the bound volume — an fs can never span past its partition into neighboring volumes.

#### `src/kernel/kernel.c`
- Boot policy: when `part_is_ready()`, bind `volume_use_partition(1)` before filesystem init ("FS volume: storage partitions/1"). Without disk/GPT the legacy in-memory path applies unchanged.

#### Tests
- `tests/unit/test_fs_volume.c` (NEW, 2 tests): selection/validation (1-based, out-of-range rejection, per-partition bases) and legacy whole-disk reset — real volume.c + part.c over mocks.
- `tests/mocks/mock_kernel.c`: disk mock now tracks the min/max LBA range touched since reset (`mock_disk_io_range`) for volume-containment assertions.

#### Tools
- `tools/drive_parts.py`: new checks — boot log "FS volume: storage partitions/1", and **post-run host-side image inspection**: protective MBR intact (0xEE entry + 0x55AA), GPT header intact, FS magic "PLAN" at partition 1 start (LBA 2048), partition 2 NOT formatted. QEMU stderr captured to a log.

### Verification
- Kernel build clean (0 compiler warnings).
- Host suite: **165/165 pass**.
- QEMU/WHPX `drive_parts.py`: **10/10** — boot parse, volume binding line, parts listing, probe reads, wc regression + all four image-inspection proofs (the definitive demonstration that format went INSIDE the partition).
- No-disk regression: `drive_vcat.py` 72/72 (legacy in-memory path untouched).

### Results
- Known issue #5 resolved: the storage stack is now genuinely partitioned end-to-end (disk → GPT → partition-relative I/O → fs INSIDE the volume), matching the namespace model where volumes are addressed as `<device>/partitions/<n>`.
- Environment note: intermittent WHPX ATA-attach flake still observed occasionally (clean on retry; stderr shows no host-side drive error) — documented in STATE alongside the serial keystroke-drop issue.

## 2026-08-25 — Agent Session: Plan0 namespace architecture (spec + resolver slice)

### Context
Design directive: Plan0 gets ONE unified resource namespace with numeric domain roots (`0/` = local machine; 1–9 reserved for future domains incl. network), self-explaining canonical paths, optional short aliases resolving to the same resource, and a table-driven resolver — explicitly NOT a renamed Unix clone, and NOT hundreds of scattered string compares. Directive sequence: audit → analyze → specify → test → implement minimally.

### Audit findings (pre-spec)
- VFS `find_ops`: longest-prefix string match; `/proc`,`/sys`,`/dev` mounts carry NULL ops and are implicitly routed to `vfile_vfs_ops` — dispatch by convention.
- vfile: flat exact-string registry (~30 Unix-style resources) with hand-written per-dir emitters; dynamic files matched by string prefix.
- path.c `resolve_path`: prepends `/`; no grammar or resource-root concept (16 shell callers).
- Storage stack (disk→GPT→part-relative I/O→whole-disk fs.c) had NO path representation anywhere. No aliases, no numeric roots, no introspection.

### Key decisions (full rationale in docs/NAMESPACE.md §6, §7; recorded in STATE.md)
1. **Filesystems always mount on `<device>/partitions/<n>`** — even single-partition devices. Device vs. partition are different resource kinds (raw block device vs. fs-capable range); uniform addressing eliminates all "is it partitioned?" special cases and nondeterministic mount fallbacks.
2. **Logical user storage at `0/user/`**, separate from hardware paths, so relocating storage never changes application paths (`0/hss/user/` style stays a secondary device-scoped view).
3. **Aliases are pure renames expanded BEFORE classification** (`0/hss` → `0/hardware/storage/ssd`) — one implementation, two spellings; backends never see alias strings.

### Changes
- **`docs/NAMESPACE.md` (NEW)** — authoritative specification: roots, hardware/system/dev/user subtrees, partition convention (+ analysis), grammar (bare digit root valid; dots inside components allowed, all-dot rejected; 256 B / depth 16), resolution pipeline, application access model, discoverability requirements, legacy-path migration mapping (`/proc`→`0/system`, `/sys/kernel`→`0/system/kernel`, `/dev/*`→`0/dev/*`, `/` root→partition volume once fs-on-partition lands), non-goals, slice plan.
- **`include/ns.h` + `src/fs/ns.c` (NEW)** — the resolver: normalize → alias expansion (component-boundary safe, bounded passes) → split → declarative node walk producing `ns_resource_t {kind, domain, part_index, device, canonical}`. Distinct errors: -1 parse violation vs -2 unknown resource. `ns_describe()` introspection reports canonical form, alias links in BOTH directions, kind, and live backend facts from the existing disk/GPT/partition stack (read-only; e.g. real partition count, per-partition start/size/label). Wired into kernel Makefile. Zero changes to vfs/vfile/fs behavior.
- **`src/ui/shell.c`** — new `ns <path>` command (help entry added): prints the description through the terminal; invalid syntax vs unknown resource error distinctly.
- **`tests/unit/test_ns.c` (NEW, 7 tests)** over the existing disk/gpt/pmm mocks: grammar accept/reject (incl. `9/main/index.html` parses, `/proc/...` rejected, `..` traversal banned, bare `0` valid); alias expansion incl. boundary safety (`hsss` not hijacked by `hss`); full-descriptor alias≡canonical equivalence; classification of every kind; structured errors; backend-unavailable reporting (with explicit mock-state reset — suite-order isolation bug found and fixed during development).
- **`tests/Makefile` / `tests/test_runner.c`** — ns.c into REAL_SRCS, "Namespace Resolver" suite registered.
- **`tools/drive_ns.py` (NEW)** — serial driver gating on live `ns` behavior.

### Verification
- Kernel build clean (0 compiler warnings).
- Host suite: **163/163 pass** (156 previous + 7 namespace tests).
- QEMU/WHPX end-to-end: `drive_ns.py` **5/5** — `ns 0/hss` shows canonical+kind+alias+backend; canonical path reveals its alias; memory ram reports totals; unknown resource errors cleanly; `/proc/uptime` rejected as syntax error. One transient WHPX boot failure observed (no shell prompt); clean on retry — consistent with previously documented environment flakiness, unrelated to namespace code (no disk involved).

### Results
- The namespace is now a real, tested module with an authoritative spec — foundational layer for mounting fs on partition volumes under `0/hardware/storage/<dev>/partitions/<n>` next.
- Next slices (per spec §16/§23): bind `fs_vfs_ops` to a chosen partition volume; legacy-path compatibility aliasing; `ls`/`go to` namespace awareness; `0/user/` binding policy.

## 2026-08-25 — Agent Session: Partitioned disk I/O (part.c) + ATA IDENTIFY reliability fix

### Context
STATE's near-term backlog: "Add part.c to disk I/O path for partitioned access". Investigation found `part_init()` was never even called from kernel_main, `part_get_info` was stubbed to zeros, and `part_read/write_sectors` ignored the partition number entirely (TODO comments). The GPT layer (partition_table.c) already parsed header + entries correctly.

### Changes

#### `src/drivers/part.c` (+ `include/part.h`)
- **`part_translate()`** — partition-relative → absolute LBA translation with bounds checks: sectors must be 1..256 (LBA28 driver cap), the range must fit inside the GPT entry (`start_lba`/`end_lba` inclusive), absolute LBA capped at 0x0FFFFFFF. Every rejection returns -1 without touching the disk.
- **`part_read_sectors`/`part_write_sectors`** now honor `partition_num` via the translation.
- **`part_get_info`** filled from the real GPT entry: start LBA, size (inclusive end), type classification against well-known GUIDs (EFI system/Linux swap/Linux fs/FAT32), UTF-16LE label converted to ASCII.
- **`part_list_partitions`** dense used-only listing (unused table slots dropped; original slot index preserved in `partition_number`).
- **`part_is_ready()`** guard flag set only after a successful `gpt_init`; every public entry point checks it.
- **`part_init()`** skips a redundant `disk_init` when the disk is already up, treats "no GPT" as non-fatal ("No GPT partition table found"), and reports the USED partition count instead of the raw table size.
- `part.h`: `part_is_ready()` declared.

#### `src/kernel/kernel.c`
- `part_init()` called after successful `disk_init()` at boot.

#### `src/ui/shell.c`
- New `parts` command (`show partitions`, `parts` alias): no-disk/no-GPT guards, lists # / start LBA / size KB / label for each used entry, then probe-reads sector 0 of EVERY listed partition through `part_read_sectors` so the translation is exercised against real hardware in the listing itself (`read pN sector 0: ok|FAILED`).

#### `src/drivers/disk.c`
- **ATA IDENTIFY race fix**: disk_init read STATUS exactly once after issuing IDENTIFY; under WHPX this intermittently observed a not-yet-started command (~50% of boots with a disk attached reported "ATA disk init FAILED"). Now polls up to 100k spins: 0x00 = floating bus (no drive), BSY must clear, then ERR/DF checked. Boot-with-disk went from ~50% failure to 4/4 clean runs.

#### Tests
- `tests/mocks/mock_kernel.c`: un-prefixed `disk_*`/`gpt_*` mocks (init/ready/read/write/get_info/get_size; gpt_init/gpt_list_partitions/gpt_get_partition) with test setters `mock_disk_set_ready`, `mock_disk_io_reset`, `mock_disk_last_io`, `mock_gpt_setup` (MOCK_GPT_MAX_PARTS=16).
- `tests/unit/test_part.c` (NEW, 8 tests): real part.c compiled over the mocks — init without GPT stays unready; read translates rel-LBA 10 → abs 2058; write lands on partition start 2048; all bounds rejections perform ZERO disk calls; requires-ready; get_info fields incl. label conversion and GUID classification; dense listing drops gap slots.
- `tests/Makefile`: part.c added to REAL_SRCS, test_part.c to unit sources; `tests/test_runner.c`: "Partition Manager" suite registered.

#### Tools
- `tools/make_gpt_image.py` (NEW): builds a 16 MiB raw image with protective MBR, GPT header @LBA1 (CRCs zeroed — driver has GPT_VALIDATE_CRC=0), entry array @LBA2, two used partitions: "plan0-test" (Linux-fs GUID, LBA 2048-4095) and "data-two" (FAT32 GUID, LBA 4096-6143).
- `tools/drive_parts.py` (NEW): boots `-kernel bin/kernel0.bin -drive file=tools/gpt_test.img,if=ide,format=raw,index=0` under WHPX, echo-verifies keystrokes, checks the boot-time GPT parse ("Found 2 partitions"), the `parts` listing (starts/sizes/labels), both probe reads, and a `wc` shell regression. Image regenerated per run (fs_format writes its boot sector over LBA 0 during boot).

### Verification
- Kernel build clean (0 compiler warnings; the 2 known pre-existing linker notes).
- Host suite: **156/156 pass** (148 previous + 8 partition tests).
- QEMU/WHPX end-to-end: `drive_parts.py` **5/5 checks × 4 consecutive runs** — boot-time GPT parse, parts listing (2048/1024/plan0-test + 4096/data-two), partition-relative probe reads ok on p0+p1, wc regression green.

### Results
- Partitioned access is real: relative LBAs translate through the GPT entry with hard bounds; observable via `parts` including live probe reads through the new I/O path.
- Disk attach is now reliable under WHPX (IDENTIFY poll).
- Known follow-up recorded in STATE: fs.c still formats/mounts whole-disk (not behind a partition); serial drive scripts remain intermittently lossy host-side (documented).

## 2026-08-25 — Agent Session: Land previously-uncommitted infrastructure + `wc` command

### Context
The working tree carried 39 modified and 21 untracked files from earlier sessions that had been recorded in HISTORY/STATE but never committed (the granular per-feature commits only staged their own files, stranding everything else). Among them: build-critical untracked sources, so `git clone` + `make all` could not have worked. This session characterized every diff, verified the unified tree, and committed atomically. One new feature (`wc`) was found finished but unrecorded in the tree.

### Changes (commit order)

1. **`8e29be5` Track nl_parser/path/version sources required by the build** — `include/nl_parser.h`, `include/path.h`, `include/version.h`, `src/fs/path.c` were untracked while committed sources included them; a clean checkout could not build.
2. **`7d6f76f` Track failed assertions via longjmp; make stddef.h hosted-safe** — ASSERT used to `return`, so the runner counted failures as passes; it now `longjmp`s to the runner which marks the test failed. stddef.h uses `#include_next <stddef.h>` when hosted. Also fixed `cpu_context_t` size expectation (144 → 160) and printf/cast mismatches in the integration suite.
3. **`528297a` Zero BSS and boot page tables; harden context switch and IRQ stubs** — BSS was never zeroed (multiboot `bss_end_addr = 0`; raw-loaded ELF debug sections overwrote it) so every static started as garbage; boot page tables are now zeroed before population. `context_switch` hardcodes CS=0x08/SS=0x10 (reading CS from the stack was garbage — 64-bit CALL pushes only RIP). The IRQ stub read the IRQ number from the wrong stack offset (the dummy error code, always 0). `.note.GNU-stack` added to asm files missing it.
4. **`b63433f` Fix TSS descriptor, mirror panics to serial, wire process entry args** — GDT TSS descriptor now stores base[63:48] (previously truncated → non-canonical base → `ltr` #GP). `panic()` mirrors the full register dump to COM1. `irq_handler` feeds `/proc/interrupts` via `vfile_irq_tick`. `process_create` passes entry/arg via rdi/rsi for `process_wrapper`. `switch_timer_tick` sends EOI before the context-switch iretq and forces a schedule when nothing is current.
5. **`39f0c96` Canonicalize pmm_* as the only PMM API; never free kernel-resident pages** — the `memory_*` wrapper layer removed; callers use `pmm_*` directly. `pmm_free_range` now refuses to free pages below the kernel-resident bitmap region, so boot-time memory-map freeing can never hand out pages overlapping the running kernel.
6. **`6da6b55` Route pic/timer/serial I/O ports through io.h** — removed three private `outb`/`inb` copies.
7. **`fab90b9` Driver cleanup** — `disk_is_ready()`, `input_init` returns whether any keyboard backend attached, `part.c` unused-parameter casts, `usb_host.c` drops two unused descriptor-buffer declarations.
8. **`d329d24` Share k_strstr/k_trim from kstring; drop dead file intents and duplicate UI info commands** — canonical `k_strstr`/`k_trim` exported from kstring; the private copies in `intent_dispatcher.c`/`ui_command.c` removed. The 500-series file intents (schema-only, no handlers) and `cmd_list_processes`/`cmd_show_system_info` (duplicates of shell `ps`/`system`) removed.
9. **`cff9651` Add wc command for line/word/byte counts** — `wc <file>` (Unix-style counts, virtual files first, chunked real-file reads, 4 KiB cap); two new drive_vcat end-to-end checks.
10. **`1ef60ef` Add QEMU/serial debug and shell-drive tooling** — 16 previously-untracked scripts (monitor-state dumpers, serial shell drivers, environment probes).
11. **`d3e1c88` Add AGENTS.md operating guide** — the repo's own AGENTS.md was untracked.

### Verification
- Kernel build: clean (0 compiler warnings; only the two known pre-existing linker notes).
- Host suite: **148/148 pass** from a clean build.
- QEMU/WHPX end-to-end: `drive_vcat.py` — full battery green on clean runs (70/70 with the two new `wc` checks). Environment note: the serial drive scripts intermittently lose individual keystrokes under WHPX (e.g. `delete` arrives as `deee`); failing checks always re-pass on rerun, and each failed tail shows the garbled echo — kernel output is correct, the input driver on the host side drops bytes. Any check failure whose tail shows a correctly-typed command should be treated as a real regression.

### Results
- Repository now builds from a clean checkout for the first time since `path.c`/`nl_parser.h`/`version.h` were introduced.
- All recorded work is now actually in git history; tree clean except agent bookkeeping (`.omo/`, `agent/`, `.freebuff/`).
- STATE.md gained four decisions: pmm_*-only allocator API, shell-owns-file-ops (not intent schemas), shared kstring helpers, and the repo-hygiene landing.

## 2026-08-24 — Agent Session: `cat` streams files beyond 255 bytes

### Context
`read`/`cat` read at most 255 bytes in one shot and printed that — larger files were silently truncated mid-content with no indication.

### Changes
- **`cmd_read_file` streams the whole file**: a chunked loop reads 255 bytes at a time and prints each chunk, so `cat` on a multi-hundred-byte file shows everything. Empty-file and not-found handling unchanged.

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 148/148 pass.
- QEMU/WHPX end-to-end: `drive_vcat.py` 70/70 — 5 new checks (edit builds a ~480-byte file across 20 lines; `cat` shows both first and last lines, proving no 255-byte cut).

## 2026-08-24 — Agent Session: `grep` + NL-parser hijack fix

### Context
No way to search file contents. While adding one, a real bug surfaced: the natural-language parser claimed ANY phrase containing a verb token anywhere, so `grep Uptime /proc/uptime` was hijacked into the `uptime` command (the mid-command "Uptime" token matched the verb table).

### Changes
- **`grep <pattern> <file>` command**: reads the whole file — virtual (`/proc/...`, `vfile_read`) or real (`fs_open` + chunked reads) — splits it into lines and prints `N: line` for every match (documented 4 KiB cap). Dispatch + help entry added.
- **NL-parser hijack fix** (nl_parser.c): a verb can only claim a phrase if it sits at the start, preceded at most by filler words (`the`, `please`, `me`, ...). Once a non-filler precedes a token, the scan stops and the parser declines, so plain commands containing verb-looking words (uptime, list, read, ...) fall through to the token parser.

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 151/151 assertions — 3 new nl_parser regressions (`grep Uptime /proc/uptime` declined, `grep beta file` declined, `please show me the files` still claimed).
- QEMU/WHPX end-to-end: `drive_vcat.py` 65/65 — 6 new checks (edit builds a 3-line file; `grep beta` finds lines 1+3; `grep zzz` no matches; `grep Uptime /proc/uptime` matches a virtual file).

## 2026-08-24 — Agent Session: read-only markers in `ls`

### Context
`info <file>` reported the read-only flag, but a plain `ls` listing gave no hint which files are protected — you had to probe each file individually.

### Changes
- **`ls` marks read-only files with `[RO]`**: `list_callback` now resolves each entry's full path (via a `list_cur_dir` static set by `cmd_list`, same pattern as the recursive find/copy walkers) and calls `fs_is_readonly`; files display `[RO]  N bytes  name`, others `       N bytes  name` (aligned), dirs unchanged as `[DIR]`. Virtual entries (`/proc`, `/sys`, `/dev`) resolve through the same path and simply show no marker.

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 148/148 pass.
- QEMU/WHPX end-to-end: `drive_vcat.py` 56/56 — 2 new checks (`ls` shows `[RO]` after `chmod -w`, drops it after `chmod +w`).

## 2026-08-24 — Agent Session: shell command history recall (arrow keys)

### Context
The shell stored command history (`history_add` + `history` command) but it was unreachable — the PS/2 driver ignored 0xE0-prefixed extended scancodes entirely, so the up/down arrow keys did nothing. Users had to retype every command.

### Changes
- **PS/2 driver handles extended (0xE0) keys**: `ext_pending` tracks the 0xE0 prefix; Up (0x48) is delivered to the shell as `\x01` and Down (0x50) as `\x02`; other extended keys (Home/End/Del/PageUp/PageDown) and their releases are ignored instead of being misread as normal keys.
- **Shell input loop recalls history**: up/down walk the ring buffer with a full line redraw (erase + repaint prompt, pad shorter entries with spaces); Enter runs the recalled command.
- **`history_add` consecutive-dedup**: re-running a recalled command no longer pushes a duplicate onto the stack (which made up-up land on the duplicate instead of the older command). Empty lines were already skipped.

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 148/148 pass.
- QEMU/WHPX end-to-end: `drive_history.py` 8/8 — up recalls the last command, Enter re-runs it (vcat prints content), up-up walks to the `write`, down returns to `vcat` and re-runs; `drive_vcat.py` regression 53/53.

## 2026-08-24 — Agent Session: recursive directory copy

### Context
`copy` only worked on plain files — `copy <dir> <dst>` failed with "Source file not found" because `fs_open` expects a file. Directories had no way to be duplicated.

### Changes
- **`copy <dir> <dst>` mirrors a directory tree** in shell.c: `copy_dir_walk`/`copy_dir_callback` recurse with `fs_readdir`, recreate subdirectories via `fs_mkdir`, and copy every file byte-for-byte (with `fs_truncate` so a shorter file never leaves stale tail bytes).
- Unix `cp -r` semantics: if dst is an existing directory, the tree lands inside it under the source's basename; otherwise dst becomes the copy root.
- Self-copy refusal: copying a directory into itself (or any descendant path) errors out before walking, preventing infinite descent; a depth-8 guard bounds cycles.
- Path helpers `path_basename`/`path_join`/`path_has_prefix` added (shell has no k_strrchr/k_memcmp).

### Verification
- Kernel builds clean (0 warnings; only the two known pre-existing linker warnings).
- Host suite: 148/148 pass.
- QEMU/WHPX end-to-end: `drive_vcat.py` 53/53 — 4 new checks (mkdir tree → `copy srcdir dstdir` → vcat both mirrored files → self-copy refused).

## 2026-08-24 — Agent Session: recursive `find` + index-root corruption fix

### Context
`find` only counted flat root matches (the intent-layer search literally documented "Real implementation would walk subdirectories recursively"), so nested files were invisible. While debugging, a deeper bug surfaced: after MFT slot churn (create/delete cycles), `ls` on a directory returned `(empty)` even though writes into it succeeded.

### Changes
- **Recursive `find` in the shell** (`cmd_find` rewritten): walks the real filesystem tree with `fs_readdir`, prints the full path of every matching entry, descends into subdirectories with a depth guard against cycles, and reports the match count. `find_walk`/`find_callback` replace the old intent-layer count-only search.
- **Index-root corruption fix in `add_dir_entry`** (fs.c): when a directory's index was empty, the walk loop never executed and the insertion point read `last->length` from uninitialized/stale memory in a reused MFT slot, so the new entry landed at a wrong offset and the index became corrupt. The walk is now bounded by `index_attr->length`, `insert` tracks the true insertion point (first-entry position for an empty index), and the `last->length` validity check gates entry traversal.
- QEMU driver: vcat find checks tightened to examine only the region after "Searching for" (they previously passed spuriously by matching the earlier `write` echo); `drive_find_debug.py` added to reproduce the churn→empty-index scenario.

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 148/148 pass.
- QEMU/WHPX end-to-end: `drive_vcat.py` 46/46 — new checks: recursive find finds `/findtest/deep-note.txt` (1 match), delete+recreate flows, `ls` lists `[DIR] findtest`; `drive_find_debug.py` confirms `find note` finds `/ft/deep-notetxt` after create/delete churn that previously corrupted the index.

## 2026-08-24 — Agent Session: fs_stat + enriched `info` command

### Context
`fs_stat` was declared in fs.h but never implemented, and the shell's `info` command opened the file with `fs_open` — which fails for directories — and could not report the read-only flag.

### Changes
- **`fs_stat(path, attr_filename_t*)` implemented** in fs.c: resolves the path, copies the file's `ATTR_FILENAME` payload (name, sizes, timestamps, parent MFT) into the caller's struct.
- **`fs_is_directory(path)` added** (companion to `fs_is_readonly`): returns 1/0/-1 for dir/file/not-found.
- **Shell `info` command rewritten** to use `fs_stat` + `fs_is_directory` + `fs_is_readonly`: works on directories, reports `Type: File|Directory`, `Access: read-only|read-write`, and the parent MFT. Bare `info <name>` alias added (the token parser always fills arg2, so the `!arg2` guard was fixed to `!*arg2`).
- Host tests: `fs_stat`/`fs_is_directory` mocks (basename semantics matching the kernel) + 1 new test (148/148 pass).
- QEMU driver: robust QEMU path resolution for MSYS2 python (HOME/expanduser/USERPROFILE/known paths).

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 148/148 pass.
- QEMU/WHPX end-to-end: `drive_vcat.py` 40/40 — 4 new `info` checks (file type/size, dir type, read-only access) plus all prior checks; stdio/pipe/dup regressions green.

## 2026-08-24 — Agent Session: fs_rmdir (safe directory deletion)

### Context
Directories could be created (`mkdir`) but `delete` on one just called `fs_unlink`, which cleared the MFT entry without checking children — orphaning any files inside (their `parent_mft` dangled). `fs_rmdir` was declared in fs.h but never implemented.

### Changes
- **`fs_rmdir` implemented** in fs.c: refuses non-directories, read-only directories, and non-empty directories (any in-use MFT entry whose filename attr references this dir); removes the directory's own index entry from its parent; then clears the MFT entry.
- **`fs_unlink` now refuses directories** (must go through `fs_rmdir`), so a bare `delete` on a directory can never orphan children.
- **Shell `delete` tries `fs_unlink` (files) then `fs_rmdir` (directories)**; error message now distinguishes read-only from not-found/non-empty.
- Host tests: functional `fs_mkdir`/`fs_rmdir`/`fs_unlink` mocks + a directory-delete flow test (147/147 pass).

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 147/147 pass.
- QEMU/WHPX end-to-end: `drive_vcat.py` 33/33 — new rmdir flow (mkdir → child write → non-empty delete refused → child delete → empty-dir delete) plus all prior checks (fd-layer virtual files, truncate, rename, phantom-entry).
- Driver hardened: commands are now echo-verified with one retry on dropped/garbled keystrokes, and waits track the end of the echoed command so fast kernel output isn't skipped.

## 2026-08-23 — Agent Session: fs_unlink removes parent index entry (phantom-entry fix)

### Context
`fs_unlink` cleared the MFT entry but left the file's entry in the parent's directory index root. Since `alloc_mft_entry` reuses cleared slots, a new file taking the freed MFT slot would be listed under the old name too — a phantom entry.

### Changes
- **`fs_unlink` now removes the child's index entry from its parent** via the `remove_index_entry` helper (added for fs_rename) and rewrites the parent entry — the index stays consistent with the MFT.
- Forward-declared `remove_index_entry` (fs_unlink precedes its definition).
- QEMU driver regression check added to `tools/drive_vcat.py`: write ghost1 → delete → write ghost2 (slot reuse) → `ls` shows only ghost2, never ghost1.

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 146/146 pass.
- QEMU/WHPX end-to-end: `drive_vcat.py` 28/28 (4 new ghost checks pass).

## 2026-08-23 — Agent Session: fs_rename (atomic move preserving metadata)

### Context
The shell's `move`/`rename` did copy+delete, which lost the read-only flag (the copy was always writable) and wasted clusters. `fs_rename` was declared in fs.h but never implemented.

### Changes
- **`fs_rename` implemented** in fs.c: updates the child's `ATTR_FILENAME` (name + parent_mft + modify_time), rewrites the same-directory index entry in place, or moves the index entry between parents for cross-directory moves (`find_index_entry`/`remove_index_entry` helpers; compaction preserves the end-of-index flag). MFT flags and data clusters are preserved. Refuses when the destination already exists.
- **Shell `move`/`rename` use `fs_rename` first**, falling back to copy+delete only when the rename is refused — so a renamed file keeps its read-only protection.
- Host suite: functional mock `fs_rename` added; new test `fs_vfs_rename_preserves_content` (rename → old name gone, content intact, rename onto existing refused).

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 146/146 pass (1 new).
- QEMU/WHPX end-to-end: `drive_vcat.py` 23/23 — new checks write→chmod -w→move→read (content intact)→chmod (still read-only)→delete refused→chmod +w→delete OK; all other drivers (stdio, dup, pipe) still green.

## 2026-08-23 — Agent Session: fs_truncate + VFS_O_TRUNC (copy-overwrite correctness)

### Context
`VFS_O_TRUNC` was accepted by `vfs_open` but silently ignored, and `fs_truncate` was declared in fs.h but never implemented — so `copy` over an existing larger file left stale tail bytes (non-resident writes preserved `existing_size`).

### Changes
- **`fs_truncate` implemented** in fs.c: frees data clusters beyond the new size (mirroring fs_unlink's bitmap clear), shrinks the resident data attribute length, updates filename `real_size`, and clamps `file->size`/`position`.
- **fs_vfs_ops honors `VFS_O_TRUNC`**: after open, truncates to zero and rewinds, so a shorter write through the fd layer cannot leave stale bytes.
- **Shell `cmd_copy` truncates the destination** (`fs_truncate(dst_file, 0)`), fixing the same stale-tail bug on the direct fs path.
- Host suite: functional mock `fs_truncate` added; new test `fs_vfs_truncate_removes_stale_tail` (write long → reopen with O_TRUNC → write short → read returns only the short payload).
- Driver fixes: `drive_vcat.py`/`drive_stdio.py` prompt wait now tolerates fast boots (prompt already in log); copy-overwrite scenario added (write long, copy short over it, read back — no stale tail).

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 145/145 pass (1 new).
- QEMU/WHPX end-to-end: `drive_vcat.py` 15/15 (incl. copy-shorter-over-longer → `read longfile.txt` prints exactly `short`); `drive_stdio.py` 2/2; `drive_dup.py` 5/5; `drive_pipe.py` 5/5.

## 2026-08-23 — Agent Session: Standard fds 0/1/2 (stdio console) + fs_write size fix

### Context
The fd layer had pipes, dup/dup2, and the unified mount, but no standard descriptors: fd 0/1/2 didn't exist, so dup2/pipe couldn't target a process's standard output — the classic Unix composition.

### Changes
- **Console device + std fds**: `console_vfs_ops` (read → EOF, write → terminal) and `fd_table_install_std()` populate fds 0 (stdin, read-only), 1 (stdout), 2 (stderr, write-only) in both the kernel fd table (`vfs_init`) and every new process table (`fd_table_create`). `fd_table_clone` rewritten to not double-install std fds.
- **Shell `stdio` command**: demos the primitive — dup2 a pipe's write end onto fd 1, `vfs_write(1, ...)`, read the redirected bytes back from the pipe.
- **fs_write size-sync fix**: `fs_write` updated `fn->real_size` (on-disk) and `file->position` but never `file->size` (in-memory), so after writing, `fs_seek`/EOF clamping used a stale size of 0 — reads after a write re-read from position 0 and EOF was never reached. All three write paths now set `file->size = file->position`. This was exposed by the dup driver running against the real fs backend (root mount is now fs_vfs_ops).
- **fs_vfs_ops mode guards removed**: the adapter denied reads on mode-1 (O_RDWR) handles, but fs_read is mode-agnostic and the VFS node flags already enforce access — the guards broke the dup test's read-via-dup.
- QEMU driver paths updated (`~`-expansion portable to MSYS2 python).

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 144/144 pass (4 new std-fd tests: fds installed, stdout write + stdin EOF, dup2 redirects stdout into a pipe, process-table std fds).
- QEMU/WHPX end-to-end: `stdio` 2/2 (stdout→pipe roundtrip, fdinfo shows device fds); `dup` 5/5, `pipe` 5/5, `vcat` 9/9 — all green after the fs_write fix.

## 2026-08-23 — Agent Session: fs.c VFS adapter (real files through the fd layer)

### Context
Known Issue #4's remaining half: the disk-backed fs.c filesystem had no `vfs_ops_t` adapter, so the fd/syscall layer could open virtual files and ramfs files but never the shell's actual files. Also, `fs_seek` was declared in fs.h but never implemented.

### Changes
- **`fs_seek` implemented** in fs.c (SET/CUR/END semantics, clamped to [0, size]).
- **New `src/fs/fs_vfs.c` + `include/fs_vfs.h`**: `fs_vfs_ops` adapter (open/close/read/write/mkdir/unlink/list/size) bridging fs.c onto the VFS fd interface, with an 8-slot fs_file_t handle table and offset-seeking reads.
- **kernel.c**: after `vfs_init()`, the root mount `/` is re-mounted with `fs_vfs_ops` — the real filesystem now serves the fd layer. Longest-prefix dispatch keeps `/proc`/`/sys`/`/dev` on the vfile adapter.
- **Makefile + CI**: `fs/fs_vfs.c` added to `FS_SRCS` and the CI fs source loop.
- **Host tests**: functional in-memory fs mock (open/read/write/seek/close/unlink/readdir) in `mock_kernel.c`; `fs_vfs.c` compiled into the host suite; 4 new tests (adapter roundtrip, offset read, unlink, mount precedence).

### Verification
- Kernel builds clean (0 warnings).
- Host suite: 140/140 pass (4 new).
- CI-equivalent host-gcc compile of fs_vfs.c clean.
- QEMU/WHPX end-to-end (`tools/drive_vcat.py`, 9/9): `write vcattest.txt hello-from-fs` then `vcat /vcattest.txt` reads the real file back through the fd layer; `vcat /proc/uptime`, `/sys/kernel/name`, `/proc/processes`, `/dev/null`, `/proc/nope` (fails at open), `/sys/kernel/arch` all still correct; `delete vcattest.txt` cleans up.

## 2026-08-23 — Agent Session: Unified VFS Mount (path-aware dispatch)

### Context
Known Issue #4: `find_ops()` ignored the path entirely and always returned the ramfs ops, so the fd/syscall layer could never open virtual files (`/proc`, `/sys`, `/dev` were registered with NULL ops) or reach them through normal file descriptors.

### Changes
- `find_ops()` is now path-aware: longest-prefix match over the mount table with component-boundary checks; bare names resolve against the root mount; NULL-ops mounts route to the new vfile VFS adapter.
- Added `vfile_vfs_ops` adapter (`open`/`close`/`read`/`write`/`size`) bridging the path-based vfile API onto the fd-based VFS interface, with a small open-path table and a static scratch buffer for the single-threaded kernel.
- `vfs_open` now uses `ops->size` instead of a hardcoded ramfs size.
- Added `vfile_exists()` (registered files + dynamic `/proc/self/fd/N` prefix) to vfile.c/h.
- Added shell `vcat <path>` command — reads any path (virtual or real) through the VFS fd layer, proving virtual resources are reachable via fds.
- 7 new host tests: path dispatch (virtual vs real vs bare name), virtual file open/read through the fd layer, dynamic `/proc/self/fd`, write through VFS.

### Verification
- Kernel builds clean (0 warnings; only the 2 pre-existing linker notes).
- Host suite: 136/136 pass.
- QEMU/WHPX end-to-end via `tools/drive_vcat.py` (monitor sendkey + serial file): `vcat /proc/uptime`, `/sys/kernel/name`, `/proc/processes`, `/dev/null` (0 bytes), `/proc/nope` (fails at open), `/sys/kernel/arch` — 6/6 PASS.

### Root-cause note
An earlier driver attempt hung because it wrote commands to the serial *output* TCP port; the kernel never reads serial input (shell input is PS/2/HID via QEMU monitor `sendkey`). Rewrote the driver to use the monitor, matching the working `drive_pipe.py` pattern.

## 2026-08-18 — Agent Session 1: Build Repair & Foundation

### Context
Deep codebase analysis performed on Plan 0 v0.4.1 to assess build status and identify issues blocking a production-grade kernel foundation.

### Issues Discovered

1. **CRITICAL: Missing PS/2 keyboard driver**
   - Header `include/ps2_keyboard.h` declares 5 functions
   - No `src/drivers/ps2_keyboard.c` implementation file existed
   - 5 linker errors: `ps2_keyboard_init`, `ps2_keyboard_has_key`, `ps2_keyboard_getc`, `ps2_keyboard_getc_block`, `ps2_keyboard_handler`
   - Also referenced from `boot/interrupts.asm` (IRQ1 handler calls `ps2_keyboard_handler`)

2. **Compiler warnings (6 total)**
   - `src/fs/fs.c:17` — unused variable `current_time`
   - `src/drivers/usb_host.c:594` — unused variable `cfg_hdr`
   - `src/drivers/usb_host.c:421` — unused variable `dev_desc`
   - `src/drivers/part.c:101` — unused parameter `partition_num`
   - `src/drivers/part.c:107` — unused parameter `partition_num`

3. **CI/Makefile inconsistency**
   - CI compiled `cpu_exceptions.c` but Makefile compiled `interrupt_handlers.c` — these have conflicting `isr_handler`/`irq_handler` symbols
   - CI missed `ps2_keyboard.c` and `part.c` in driver compilation loop

4. **Dead code: `src/arch/cpu_exceptions.c`**
   - Contains alternative `isr_handler()` and `irq_handler()` with different signatures
   - Not in Makefile build, but was in CI build
   - Should be removed or clearly marked as dead code

5. **Missing `.note.GNU-stack` section**
   - `src/arch/gdt_flush.asm` had no `.note.GNU-stack` section

### Changes Made

#### New files
- **`src/drivers/ps2_keyboard.c`** — Full PS/2 keyboard driver implementation
  - PS/2 controller initialization (self-test, port test, keyboard reset)
  - IRQ1 interrupt handler (scan code set 1 → ASCII)
  - Modifier tracking (shift, ctrl, alt, caps lock)
  - Ring buffer for key events
  - Ctrl+C support

#### Modified files
- **`Makefile`** — Added `drivers/ps2_keyboard.c` to `DRIVER_SRCS`
- **`.github/workflows/ci.yml`** — Fixed arch sources, added missing drivers
- **`src/fs/fs.c`** — Removed unused `current_time` variable
- **`src/drivers/usb_host.c`** — Commented out unused variables
- **`src/drivers/part.c`** — Added `(void)partition_num;` to suppress warnings
- **`src/arch/gdt_flush.asm`** — Added `.note.GNU-stack` section

#### New documentation
- **`AGENTS.md`** — Onboarding guide for future agents
- **`STATE.md`** — Project strategic state
- **`HISTORY.md`** — This file

### Build Results
- **Before**: 5 linker errors, 6 compiler warnings
- **After**: 0 linker errors, 0 compiler warnings

---

## 2026-08-18 — Agent Session 2: Test Suite, Context Switch & Cleanup

### Context
Continued mission work: fixed host-side test suite, wired preemptive context switch, standardized I/O port helpers, and added boot validation infrastructure.

### Changes Made

#### Test Suite Overhaul
- **`include/stddef.h`** — Rewrote to be safe for both freestanding kernel and hosted (test) compilation. Uses `__STDC_HOSTED__` to decide whether to `#include_next <stddef.h>` or provide own definitions.
- **`tests/test_framework.h`** — Changed ASSERT macros from `return` to `longjmp` so the test runner can properly track failures.
- **`tests/test_framework.c`** — Rewrote `test_suite_run()` to use `setjmp`/`longjmp` for proper failure counting. Previously always counted tests as passed.
- **`tests/test_runner.c`** — Removed unused `quiet` variable.
- **`tests/unit/test_memory.c`** — Added `#include "kheap.h"` for `kmalloc`/`kfree`. Changed memory_stats and pmm tests to validate structure layout rather than calling kernel functions.
- **`tests/unit/test_filesystem.c`** — Fixed `FS_MAGIC` assertion (0x4E414C50, not 0x4F50464E). Fixed `fs_boot_sector_t` size assertion (504 bytes, not 512).
- **`tests/unit/test_process.c`** — Fixed `cpu_context_t` size assertion (160 bytes, not 144). Suppressed unused parameter warning.
- **`tests/integration/test_kernel_integration.c`** — Added `<stdbool.h>` include. Fixed `pid_t` format specifier. Fixed sign-compare warnings with explicit casts.
- **`tests/mocks/mock_kernel.c`** — NEW FILE. Mock implementations of `kmalloc`, `kfree`, `pmm_*`, `memory_*` using host `malloc`/`free`.
- **`tests/Makefile`** — Added `mock_kernel.c` and `mock_hardware.c` to build. Added `$(OBJDIR)/mocks/` build rule.

**Test Results**: 37/37 tests pass across 4 suites (Memory: 9, Filesystem: 11, Process: 10, Integration: 7)

#### Preemptive Context Switch
- **`src/kernel/switch.c`** — Rewrote `switch_timer_tick()` to save current RSP into process context and call `context_switch()`. Previously, the scheduler tracked time slices but never actually switched contexts.

#### I/O Port Standardization
- **`src/arch/timer.c`** — Replaced local `outb`/`inb` with `#include "io.h"`
- **`src/arch/pic.c`** — Replaced local `outb`/`inb` with `#include "io.h"`
- **`src/drivers/serial.c`** — Replaced local `outb`/`inb` with `#include "io.h"`

#### Assembly Cleanup
- **`boot/switch_to_pcid.asm`** — Added missing `.note.GNU-stack` section

#### Boot Validation
- **`tools/qemu_boot_test.sh`** — NEW FILE. Script to build kernel, create GRUB ISO, boot in QEMU with serial output capture.

### Build Results
- **Kernel**: 0 compiler warnings, 0 linker errors (2 informational linker notes remain)
- **Tests**: 37/37 pass, 0 compiler warnings

### Files Changed Summary
| File | Action | Reason |
|------|--------|--------|
| `include/stddef.h` | Modified | Safe for both kernel and host compilation |
| `tests/test_framework.h` | Modified | longjmp-based ASSERT for failure tracking |
| `tests/test_framework.c` | Modified | setjmp/longjmp test runner |
| `tests/test_runner.c` | Modified | Remove unused variable |
| `tests/unit/test_memory.c` | Modified | Fix includes, validate layouts |
| `tests/unit/test_filesystem.c` | Modified | Fix constant assertions |
| `tests/unit/test_process.c` | Modified | Fix size assertions |
| `tests/integration/test_kernel_integration.c` | Modified | Add stdbool.h, fix warnings |
| `tests/mocks/mock_kernel.c` | Created | Host-side kernel function mocks |
| `tests/Makefile` | Modified | Add mock build rules |
| `src/kernel/switch.c` | Modified | Wire preemptive context switch |
| `src/arch/timer.c` | Modified | Use io.h instead of local outb/inb |
| `src/arch/pic.c` | Modified | Use io.h instead of local outb/inb |
| `src/drivers/serial.c` | Modified | Use io.h instead of local outb/inb |
| `boot/switch_to_pcid.asm` | Modified | Add .note.GNU-stack |
| `tools/qemu_boot_test.sh` | Created | QEMU boot validation script |
| `STATE.md` | Modified | Update roadmap and decisions |
| `HISTORY.md` | Modified | This entry |

---

## 2026-08-19 — Agent Session 3: Boot-Path Fixes & Functional Interactive Shell

### Context
Booting `bin/kernel0.bin` under QEMU produced zero serial output and never reached the kernel. Mission: isolate and fix the boot-path failure, then drive the OS to a functional interactive shell under QEMU (WHPX), ending only when the shell accepts commands.

### Root Causes Found & Fixed (boot path)

1. **TCG accelerator broken on this machine** — `-accel tcg` never completes SeaBIOS POST; `-accel whpx` works. All three QEMU builds (11.1.0 weilnetz, 11.0.1 MSYS2, 9.2.0) behaved identically under TCG. Not a kernel bug.

2. **Garbage boot page tables (`boot/boot.asm`)** — `pml4`/`pdpt`/`pd` live in `.bss`, but the multiboot header declares `bss_end_addr = 0`, so QEMU's a.out-kludge loader never zeroes BSS (ELF debug sections get raw-loaded over it). `setup_page_tables` now zeroes the three pages with `rep stosd` before populating.

3. **PMM handed back the kernel's own memory (`src/memory/memory.c`)** — `pmm_free_range` only skipped the low 1 MB, so the kernel image at 1 MB → `kernel_end` was freed and re-allocated, corrupting the running kernel (visible as pmm's own debug-marker fragment re-appearing mid-`paging_init`). `pmm_free_range` now skips everything below the bitmap.

### Root Causes Found & Fixed (shell / filesystem / keyboard)

4. **BSS never zeroed (`boot/boot.asm`)** — statics (`hid_active`, `ps2_active`, ring-buffer indices) started as garbage, so `keyboard_getc()` read an uninitialized HID buffer cyclically. Boot now zeroes BSS before C code runs.

5. **IRQ dispatch offset (`boot/interrupts.asm`)** — the IRQ stub read the pushed error code instead of the interrupt number, so no ISR ever dispatched. Fixed to read `[rsp + 15*8]`.

6. **Input layer never wired (`src/kernel/kernel.c`, `include/input.h`, `src/drivers/input.c`)** — `input_init()` was never called, so `ps2_active` stayed 0. Kernel now calls `input_init()` and unmask **IRQ1 only** (timer stays masked so the scheduler cannot preempt the kernel-context shell).

7. **PS/2 scancode-set mismatch (`src/drivers/ps2_keyboard.c`)** — driver cleared the controller's translation bit, so QEMU's set-2 scancodes (with `0xF0` break prefixes) were misread as set-1 makes (doubled, wrong chars). Translation now stays ON (`config |= 0x40`).

8. **Stale PS/2 reads on spurious IRQ1 (`src/drivers/ps2_keyboard.c`)** — QEMU returns the last-read byte when port 0x60 is read empty. The ISR now checks `OUTPUT_FULL` before reading, eliminating the stray boot characters (`d`, `t`) at the prompt.

### Root Causes Found & Fixed (filesystem — MFT attribute layout)

9. **Stale `ATTR_END` placeholder (`src/fs/fs.c`)** — `init_mft_entry` wrote a placeholder end-marker at `seq_attr_offset` and bumped `used_size` past it, so the first real attribute was appended *after* the marker. `find_attr` hit the stale `ATTR_END` immediately and returned NULL for every attribute on newly created entries. All attribute-append sites (`add_filename_attr`, `add_index_root_attr`, `fs_write` resident/non-resident creation) now write over the trailing end-marker and re-place it.

10. **`find_attr` return misinterpreted (`src/fs/fs.c`)** — `find_attr()` returns the `attr_header_t*`, but every ATTR_FILENAME caller cast it straight to `attr_filename_t*` (off by `sizeof(attr_header_t)` = 16 bytes), so parent refs/names/sizes were garbage. Added `find_attr_payload()` (null-safe struct pointer) and switched all 7 call sites; `fs_readdir` index path now reports real file sizes from the file's own entry.

11. **`add_dir_entry` overwrote the index (`src/fs/fs.c`)** — directory index entries were always written at a fixed offset, so each new file clobbered the previous one. Now walks to the last entry, demotes it, appends, and re-places the end-of-attributes marker.

### Shell multi-word command dispatch (`src/ui/shell.c`)
`process_command` truncated the command at the first space, so `cmd_equals(cmd, "show memory")` etc. could never match. Rewrote dispatch to match multi-word commands token-wise (`show memory`, `system information`, `open/close/move/focus window`, `list windows`, `show processes`, `make directory`, `information about`, `clear screen`, `current date time`, `go to`) before their single-token prefixes.

### New tool
- **`tools/drive_shell.ps1`** — boots the kernel under QEMU/WHPX with a TCP monitor and drives the PS/2 keyboard via `sendkey`, capturing serial output.

### Verified under QEMU (`-accel whpx`, no disk attached)
```
> create a.txt        → Created: a.txt
> write a.txt hello   → Wrote 5 bytes to a.txt
> read a.txt          → hello
> ls                  → 5 bytes a.txt / 5 bytes b.txt
> mkdir docs          → Created directory: docs
> ls                  → a.txt, b.txt, [DIR] docs
> rm a.txt            → Deleted: a.txt
> show memory         → Total RAM: 127 MB, Free: 0 MB
> system information  → full system info
> echo hello world    → hello
> uptime              → Uptime: 0 hours, 0 minutes, 0 seconds
```

### Results
- `_start` reached ✅
- `kernel_main` reached ✅
- Interactive shell prompt (`> `) accepts and executes commands ✅
- Filesystem create/write/read/list/mkdir/rm round-trips ✅
- Kernel build: 0 compiler warnings, 2 pre-existing informational linker notes
- Host tests: 37/37 pass
- QEMU versions used: 11.1.0 (D:\tools\qemu), 11.0.1 (MSYS2), 9.2.0 (diagnostic) — all under `-accel whpx`

### Known remaining limitations (out of scope for this session)
- `pwd`/`cd` dispatch through intent system returns nothing without a mounted state graph (disk-less boot)
- `Free RAM: 0 MB` — PMM frees only what the multiboot map reports; the disk-less run shows all free RAM consumed by formatting (fs caches MFT in memory)
- Filesystem writes target the ATA disk; with no disk attached they persist only in memory (a real disk image is required for persistence across boots)
- `-accel tcg` remains broken on this machine (WHPX required)

---

## 2026-08-19 — Agent Session 4: Natural-Language Shell Layer

### Context
The user asked for the shell to read like natural language while staying structured underneath: "list files in documents" instead of just "list directory", with the last word used as the path when nothing sits between. Built a phrase parser on top of the existing token dispatcher.

### New files
- **`include/nl_parser.h`** — `nl_parse(input, cmd, arg1, arg2)` → 0 on recognized phrase, -1 to fall back to the token parser.
- **`src/ui/nl_parser.c`** — tokenizes the phrase (tokens stored as start-pointer + length so args keep original case), finds the verb (multi-word phrase table first, then single-word verbs), then extracts args: filler words ("the", "please"), context nouns ("file", "directory"), location markers ("in", "from"), name markers ("called", "named", "to"), and special destinations ("current"→cwd, "home", "up") all normalize to the canonical (cmd, arg1, arg2) triple.
- **`tests/unit/test_nl_parser.c`** — 7 host-side test cases locking in phrase mappings and the fall-back (-1) behavior.

### Modified files
- **`Makefile`** + **`.github/workflows/ci.yml`** — added `src/ui/nl_parser.c` to the build loops.
- **`src/ui/shell.c`** — shell now runs input through `nl_parse()` first; `cmd_list` gained a path argument (falls back to cwd); help text mentions natural-language input.
- **`tests/Makefile`**, **`tests/test_runner.c`** — wired the new parser test suite in.

### Parser bugs found & fixed during testing (5 failing → 0)
1. **Token `orig` pointer overrun** — tokens stored `orig` into the input buffer, and after tokenization the spaces were restored, so every extracted arg read past its token into the rest of the line. Now each token carries its length and args are copied exactly.
2. **`tolower_copy` NUL-termination order** — the lowercase buffer contained the whole rest of the line because it was NUL-terminated after the copy.
3. **Phrase match consumed context nouns** — the skip loop skipped filler *and* context words between phrase words, but many phrase words are themselves context nouns ("directory", "files"), so "make a directory" matched nothing and fell through to `create`. Now only fillers are skipped.
4. **Filler words inside write content** — "append more text to notes.txt" lost "more" because "more" is a filler. Write/append content is now the verbatim token span before the "to" marker.
5. **show-family object scan** — checked the wrong verb token (index 0 vs actual verb index).

### Bonus fix: `append` overwrote files
`cmd_append` opened with mode 1 (write), and `fs_write` always wrote at `file->position` (0), so "append" replaced file contents instead of extending them. `fs_write` now honors `file->mode == 2` by writing at end-of-file, `fs_open` creates for any non-zero mode, and `cmd_append` opens with mode 2.

### Verified under QEMU (`-accel whpx`)
```
> make a directory called documents   → Created directory: documents
> create a file called notes.txt      → Created: notes.txt
> write hello world to notes.txt      → Wrote 11 bytes
> read notes.txt                      → hello world
> append more text to notes.txt       → Appended 9 bytes
> read notes.txt                      → hello worldmore text   (append works!)
> copy notes.txt to backup.txt        → Copied: 20 bytes
> delete the file backup.txt          → Deleted: backup.txt
> list the files in the current directory → lists everything with real sizes
> show me the memory / system information / echo hello there / say hi / go up / uptime
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **44/44 pass** (37 previous + 7 new parser tests)
- Natural-language phrases and the original token syntax (`write a.txt hello`) both work; ambiguous phrases fall back to the token parser

---

## 2026-08-19 — Virtual Filesystem Resource Layer

### Context
The shell/parser was stable: 44/44 host tests pass, filesystem operations verified under QEMU/WHPX. The next architectural goal was to make the filesystem the primary interface to the operating system — if a system resource can be represented as a file, expose it through the filesystem.

### What was built
A virtual filesystem resource layer (`vfile.c` + `vfile.h`) that provides read-only virtual files under `/proc`, `/sys`, and `/dev`. These files are generated on read, consume no disk storage, and are accessible through the normal `read`/`cat` shell commands.

**New files:**
- `include/vfile.h` — Virtual file registry API
- `src/fs/vfile.c` — Virtual file implementations (19 resources across 3 directory trees)
- `tests/unit/test_vfile.c` — 23 host-side unit tests for the virtual file system
- `tools/drive_vfile.py` — QEMU TCP monitor driver for end-to-end verification

**Modified files:**
- `src/kernel/kernel.c` — Calls `vfile_init()` before shell launch
- `src/ui/shell.c` — `cmd_read_file` intercepts virtual paths; `cmd_list` handles virtual directories
- `Makefile` — Added `fs/vfile.c` to `FS_SRCS`
- `.github/workflows/ci.yml` — Added `vfile` to fs compilation loop
- `tests/test_runner.c` — Registered vfile test suite
- `tests/Makefile` — Added `test_vfile.c` and `vfile.c` (real source)
- `tests/mocks/mock_kernel.c` — Added timer, RTC, and process mock stubs

### Virtual resource layout
```
/proc/          (directory)
  uptime        — seconds since boot
  memory        — RAM total/free/used in MB
  meminfo       — detailed memory (bytes, pages, page size)
  cpu           — vendor string, brand, feature flags
  cpuid         — raw CPUID leaf 0 (EAX/EBX/ECX/EDX)
  processes     — active process table (PID, name, state)
  datetime      — current date and time from RTC
  version       — "Plan 0 v0.4.1"
  hostname      — "plan0"
  timer         — raw tick and ms counters

/sys/           (directory)
  kernel/       (directory)
    name        — "Plan 0"
    version     — "0.4.1"
    arch        — "x86_64"
  hardware/     (directory)
    platform    — platform info (PC/AT, 64-bit, PML4)

/dev/           (directory)
  null          — write-only sink (read returns 0 bytes)
  console       — console info stub
```

### Architecture decisions
- **Intercept at the shell level** (`cmd_read_file` and `cmd_list`), not in `fs.c` — keeps the NTFS-style filesystem untouched and maintains clean separation between persistent storage and virtual resources.
- **Callback-based content generation** — each virtual file has a `read_fn` callback that fills a caller-provided buffer. No caching, no allocation.
- **Directory listing via callbacks** — directories have `list_fn` callbacks that emit entries one at a time.
- **No disk storage consumed** — virtual files are generated on every read.
- **Shell reads virtual paths transparently** — `read /proc/uptime` and `cat /proc/uptime` both work through the existing `cmd_read_file` path.

### Verified under QEMU (`-accel whpx`)
```
> ls /proc
  uptime, memory, meminfo, cpu, cpuid, processes, datetime, version, hostname, timer

> read /proc/uptime
  Uptime: 0h 0m 0s
  Total seconds: 0

> read /proc/memory
  Total RAM: 127 MB
  Free RAM:  0 MB
  Used RAM:  127 MB
  Free pages: 0

> read /proc/version
  Plan 0 v0.4.1

> read /proc/hostname
  plan0

> ls /sys
  [DIR] kernel
  [DIR] hardware

> read /sys/kernel/arch
  x86_64

> ls /dev
  null, console

> read /proc/processes
  PID  Name            State
  1    idle            other

> read /proc/meminfo
  MemTotal:  134086656 bytes
  MemFree:   0 bytes
  PagesFree: 0
  PageSize:  4096

> echo hello from shell
  hello
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: 23 new vfile tests written (test suite gcc temporarily broken on this machine; tests verified structurally)
- Virtual filesystem resources all verified under QEMU/WHPX
- Existing filesystem semantics preserved — `read a.txt`, `write b.txt hello`, `ls`, `mkdir`, `rm` all unchanged

---

## 2026-08-20 — Writable Virtual Files & Resource Expansion

### Context
The read-only virtual filesystem layer was complete and verified. The next architectural goal: strengthen Plan0's Unix philosophy by making the filesystem the primary interface to the OS — if a resource can be a file, it should be. Specifically, add writable virtual files and expand the resource tree.

### Changes Made

#### Writable virtual file support (`include/vfile.h`, `src/fs/vfile.c`)
- Added `vfile_write_fn` callback type and `write_fn` field to `vfile_entry_t`
- Added `vfile_write()` public API — writes to a virtual file by calling its write callback
- Added `vfile_is_writable()` — checks if a virtual file has a write handler
- Added `reg_file_rw()` registration function for read/write files
- Shell's `cmd_write_file` now intercepts virtual paths before hitting the real filesystem

#### Writable virtual resources
| Path | Behavior |
|------|----------|
| `/dev/null` | Accepts and discards all writes (returns byte count) |
| `/dev/zero` | Accepts and discards all writes |
| `/dev/console` | Writes appear on the terminal (VGA + serial) |
| `/proc/hostname` | Read returns hostname; write sets it |
| `/sys/kernel/hostname` | Alias for `/proc/hostname` (shares the same buffer) |

#### New read-only virtual resources
| Path | Content |
|------|----------|
| `/proc/stat` | CPU statistics (user/system/idle ticks, process count) |
| `/proc/interrupts` | Per-IRQ counters (16 lines: timer, keyboard, cascade, ...) |
| `/dev/zero` | Read returns NUL bytes |

#### Interrupt counting (`src/arch/interrupt_handlers.c`)
- `irq_handler()` now calls `vfile_irq_tick(irq)` to increment per-IRQ counters
- Counters are stored in `vfile.c` as static array, zeroed at `vfile_init()`
- Feed `/proc/interrupts` with live data from actual hardware interrupts

#### Host-side tests (`tests/unit/test_vfile.c`)
- Expanded from 23 to 39 tests
- New tests: write to `/dev/null`, `/dev/zero`, `/dev/console`, `/proc/hostname`
- Tests verify: `vfile_is_writable()`, write round-trip for hostname, `/sys/kernel/hostname` mirror, newline stripping, read-only rejection, non-virtual path rejection
- Added `terminal_putchar` and related mocks to `tests/mocks/mock_kernel.c`

### Virtual resource layout (final)
```
/proc/          (directory)
  uptime        — seconds since boot
  memory        — RAM total/free/used in MB
  meminfo       — detailed memory (bytes, pages, page size)
  cpu           — CPU vendor string, brand, feature flags
  cpuid         — raw CPUID leaf 0 (EAX/EBX/ECX/EDX)
  processes     — active process table (PID, name, state)
  datetime      — current date and time from RTC
  version       — "Plan 0 v0.4.1"
  hostname      — system hostname (R/W)
  timer         — raw tick and ms counters
  stat          — CPU statistics (user/system/idle ticks)
  interrupts    — per-IRQ counters (16 lines)

/sys/           (directory)
  kernel/       (directory)
    name        — "Plan 0"
    version     — "0.4.1"
    arch        — "x86_64"
    hostname    — hostname (R/W, mirrors /proc/hostname)
  hardware/     (directory)
    platform    — platform info (PC/AT, 64-bit, PML4)

/dev/           (directory)
  null          — discard writes, read returns 0 bytes (R/W)
  zero          — read returns NUL bytes, discard writes (R/W)
  console       — write appears on terminal (R/W)
```

### Verified under QEMU (`-accel whpx`)
```
> read /proc/stat
  cpu 0 0 0 0 0 0 0 0
  btime 0
  processes 1

> read /proc/interrupts
  IRQ  Count  Name
  0    0      timer
  1    0      keyboard
  ...16 lines total...

> ls /dev
  null, zero, console

> write /dev/null hello world
  Wrote 11 bytes to /dev/null
> read /dev/null
  (empty)

> write /proc/hostname mytesthost
  Wrote 10 bytes to /proc/hostname
> read /proc/hostname
  mytesthost
> read /sys/kernel/hostname
  mytesthost

> write /proc/hostname plan0
  Wrote 5 bytes to /proc/hostname
> read /proc/hostname
  plan0

> write /dev/console hello from console
  hello from console  Wrote 18 bytes to /dev/console
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **83/83 pass** (6 suites: Memory 9, Filesystem 11, Process 10, NL Parser 7, Integration 7, Virtual FS 39)
- All writable virtual resources verified end-to-end under QEMU/WHPX
- Existing filesystem semantics unchanged
- Total virtual resources: 24 (16 read-only + 5 read/write + 5 directories)

---

## 2026-08-20 — Proc/Sys Expansion, PCI Enumeration & Version Unification

### Context
Building on the writable VFS layer (90/90 tests passing). Next goals: make the filesystem genuinely useful for system introspection, add per-process info, expose hardware through the filesystem, unify the version string, and fix `ls /` to show virtual directories.

### Changes Made

#### New files
- **`include/version.h`** — Single source of truth for OS version (`PLAN0_VERSION`, `PLAN0_FULL_NAME`, etc.). Every file that needs the version string includes this header instead of hardcoding it.

#### New virtual resources
| Path | Type | Content |
|------|------|--------|
| `/proc/self/pid` | file | Current process PID (0 for kernel context) |
| `/proc/self/name` | file | Current process name ("kernel" for kernel context) |
| `/proc/self/status` | file | Process status: Name, PID, State, Priority, CPU time |
| `/proc/mounts` | file | Filesystem mount table (ramfs, vfile for /proc, /sys, /dev) |
| `/sys/devices/pci` | file | PCI hardware enumeration (real bus/device/vendor/class data) |
| `/proc/self/` | dir | Per-process information directory |
| `/sys/devices/` | dir | Hardware device directory |
| `/` | dir | Root directory listing (proc, sys, dev) |

#### Root directory listing fix
- Registered `/` as a virtual directory listing `proc`, `sys`, `dev`
- Shell's `cmd_list` now merges virtual AND real filesystem entries (not either/or)
- `ls /` now shows `[DIR] proc`, `[DIR] sys`, `[DIR] dev` alongside real FS entries

#### PCI hardware enumeration (`src/fs/vfile.c`)
- `/sys/devices/pci` scans all 256 buses × 32 devices, reads vendor/device/class/subclass via `pci_read_*()`
- Handles multi-function devices (checks header type bit 7)
- Under QEMU/WHPX, discovers: Intel 440FX host bridge, PIIX3 ISA/IDE, Cirrus Logic VGA, Intel e1000 NIC
- Added PCI mock stubs to `tests/mocks/mock_kernel.c` for host-side test compilation

#### Version string unification
- `kernel.c`, `shell.c`, `vfile.c` now use `version.h` macros instead of hardcoded "0.4.1"
- Changing the version in one place updates it everywhere

#### Updated files
- `src/fs/vfile.c` — Added 8 new virtual resources, root directory, PCI enumeration, `append_hex()` helper
- `src/ui/shell.c` — Merged virtual + real directory listings in `cmd_list`, uses `version.h`
- `src/kernel/kernel.c` — Uses `version.h`
- `include/vfile.h` — Updated documentation header
- `tests/unit/test_vfile.c` — Expanded from 39 to 46 tests (added root listing, /proc/self, /proc/mounts, /sys/devices)
- `tests/mocks/mock_kernel.c` — Added PCI function mocks

### Verified under QEMU (`-accel whpx`)
```
> ls /
  [DIR] proc
  [DIR] sys
  [DIR] dev

> ls /proc
  uptime, memory, meminfo, cpu, cpuid, processes, datetime, version, hostname, timer, stat, interrupts, mounts
  [DIR] self

> ls /proc/self
  pid, name, status

> read /proc/self/status
  Name:    kernel
  PID:     0
  State:   kernel
  Priority: 0
  CPU time: 0 ms

> read /proc/mounts
  Filesystem    Mount    Type
  ramfs         /        ramfs
  vfile         /proc    virtual
  vfile         /sys     virtual
  vfile         /dev     virtual

> ls /sys
  [DIR] kernel
  [DIR] hardware
  [DIR] devices

> read /sys/devices/pci
  Bus  Dev  Func  Vendor    Device    Class  Sub
  0    0    0     0x00008086  0x00001237  0x00000006  0x00000000  (440FX)
  0    1    0     0x00008086  0x00007000  0x00000006  0x00000001  (PIIX3 ISA)
  0    1    1     0x00008086  0x00007010  0x00000001  0x00000001  (PIIX3 IDE)
  0    2    0     0x00001234  0x00001111  0x00000003  0x00000000  (Cirrus VGA)
  0    3    0     0x00008086  0x0000100e  0x00000002  0x00000000  (e1000 NIC)

> read /sys/kernel/version
  0.4.1

> read /proc/version
  Plan 0 v0.4.1
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **90/90 pass** (6 suites: Memory 9, Filesystem 11, Process 10, NL Parser 7, Integration 7, Virtual FS 46)
- All new virtual resources verified end-to-end under QEMU/WHPX
- PCI enumeration discovers real QEMU hardware devices
- Version string unified across codebase (single source of truth)
- `ls /` now shows virtual directories alongside real filesystem entries
- Total virtual resources: 32 (22 read-only + 5 read/write + 5 directories)

---

## 2026-08-20 — Shell Commands, Intent Dispatcher Fix & Usability

### Context
The VFS layer was complete with 90/90 tests passing. The intent dispatcher was only initialized when a disk was mounted, breaking `cd`/`pwd`/`find` without a disk. The shell lacked standard Unix commands like `mount`, `df`, and `ps`.

### Changes Made

#### Intent dispatcher always initialized (`src/kernel/kernel.c`)
- Moved `intent_dispatcher_init()` out of the `if (fs_mount() < 0)` block so it runs unconditionally
- This enables `cd`, `pwd`, and `find` to work even without a disk (disk-less QEMU boot)
- The ACL entries (kernel can do everything) are now always available

#### New shell commands (`src/ui/shell.c`)
| Command | Description |
|---------|-------------|
| `mount` | Shows mounted filesystems (reads `/proc/mounts`) |
| `df` | Disk free — shows filesystem size/used/available |
| `ps` | Alias for `show processes` |

#### Build and test results
- Kernel: 0 compiler warnings, clean build
- Host tests: **90/90 pass** (unchanged — new commands are shell-only)
- All new commands verified under QEMU/WHPX

### Verified under QEMU (`-accel whpx`)
```
> mount
  Filesystem    Mount    Type
  ramfs         /        ramfs
  vfile         /proc    virtual
  vfile         /sys     virtual
  vfile         /dev     virtual

> df
  Filesystem      Size      Used      Avail     Mount
  ramfs           130944 KB    130944 KB    0 KB    /
  vfile           0 KB      0 KB      0 KB      /proc,/sys,/dev

> ps
  PID  Name        Active    Window
  1    idle        NO        0
  1 processes total

> create test.txt
  Created: test.txt
> write test.txt hello
  Wrote 5 bytes to test.txt
> read test.txt
  hello
> rm test.txt
  Deleted: test.txt
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: 90/90 pass
- Intent dispatcher now works without a disk
- Shell has standard Unix commands (mount, df, ps)
- All file operations verified end-to-end under QEMU/WHPX

## 2026-08-20 — Context-Switch iretq Investigation & TSS Wiring

### Context
Attempted to make the shell run as a proper process with preemptive round-robin scheduling (the scheduler/context_switch machinery existed but was never wired in). This caused the boot to hang/panic at the first context switch.

### Root-Cause Investigation (evidence-based)
1. The first `context_switch(0, &idle->context)` loaded a **byte-perfect iretq frame** (verified via QEMU monitor `x`/`xp` memory dumps): RIP=idle_process_func, CS=0x08, RFLAGS=0x202.
2. The GDT was verified in memory (base 0x125800, limit 0x17, valid CS64 descriptor; the CPU had even set the accessed bit).
3. The IDT gates are clean (`ist = 0` for all 256 entries).
4. A **bare iretq in isolation** (no scheduler involvement, same stack, frame built by 5 instructions) **also #GPs** — with ERR=0x5F4, which is exactly the low 16 bits of the target address (0x1005F4) — not any documented #GP error-code format.
5. The #GP was delivered with a **malformed privilege-change frame** (RSP/SS slots pushed for a ring-0 fault), and the exception frame was displaced on the stack.
6. MSYS2 QEMU 11.0.1 with `-accel tcg` triple-faults at power-on before even reaching `kernel_main` (0 bytes of serial output), so TCG cannot serve as a comparison.
7. Both QEMU builds under WHPX fail identically; the normal ISR `iretq` (interrupt return) works throughout boot — only the context-switching `iretq` fails.

### Conclusion
WHPX (Windows Hypervisor Platform) mishandles this iretq pattern even with every kernel-side state verified correct (GDT, IDT, CS, frame, target, RFLAGS with and without IF). The kernel-side evidence is exhaustive; no further kernel fix is indicated.

### Fixes Applied (kept — architecturally correct regardless)
- **`src/arch/gdt.c`** — Fixed the 64-bit TSS descriptor base: `tss_addr` now uses the heap TSS (`tss`) not the pointer variable's address (`&tss`); stored base[63:48] in the second descriptor's `base_low` so a higher-half TSS produces a canonical base (previously truncated → non-canonical → ltr #GP → triple fault).
- **`src/kernel/kernel.c`** — Wired in the previously dead TSS/GDT infrastructure: `tss_init()` + `tss_set_rsp0()` + `gdt64_init()` before `idt_init()`. The GDT now has all 8 entries (kernel/user code/data + TSS) and `ltr` runs. This is required for any future user-mode work.
- **`boot/context_switch.asm`** — Kept the correctness fixes (hardcoded CS=0x08, RFLAGS via `pushfq`); removed the temporary debug hex printer.
- **`src/kernel/switch.c`** — EOI is now sent to the PIC before `context_switch` (the assembly iretq bypasses the ISR's normal EOI path); reschedule is forced when there is no current process.
- **`src/arch/interrupt_handlers.c`** — `panic()` now mirrors to serial (COM1) so headless QEMU runs show faults.

### Resolution
Reverted the shell-as-process change: the shell runs inline in `kernel_main` again (IRQ0 stays masked), restoring the known-good interactive shell. Preemptive multitasking is documented as deferred pending a non-WHPX environment (real hardware, KVM, or a fixed WHPX).

### Results
- Boot under QEMU/WHPX: clean, reaches interactive shell, prompt accepts commands.
- End-to-end shell verification: `/dev/null`, `/proc/hostname`, `/sys/kernel/hostname`, `/dev/console` writes all pass; `ps`, `mount`, `df`, VFS resources all work.
- Host tests: 90/90 pass.

---

## 2026-08-21 — Kernel Heap Validation & /proc/heap

### Context
STATE.md's near-term backlog listed "kernel heap leak detection (magic number validation)". `kheap.h` declared `kheap_validate()` and `kheap_dump()` but `kheap.c` never implemented them (a declared-but-missing gap, like the old PS/2 case), and `kheap_get_stats()` left `free_blocks`/`largest_free`/`block_count`/`allocated_blocks` stubbed at 0.

### Changes Made

#### `src/memory/kheap.c`
- Implemented `kheap_validate()` — walks the block list and checks the magic number on every block, validates flags are FREE/ALLOCATED, verifies the linked list is contiguous (next block starts exactly where the current one ends), and returns the corruption count (0 = healthy). Silent; diagnostics go through `kheap_dump()`.
- Implemented `kheap_dump()` — prints every block (index, address, size, state) to the terminal.
- Completed `kheap_get_stats()` — now walks the block list and fills `free_blocks`, `largest_free`, `block_count`, `allocated_blocks` instead of leaving them at 0.

#### `include/kheap.h`
- `kheap_validate()` signature changed from `void` to `int` (returns corruption count) — no existing callers depended on the old signature.

#### `src/fs/vfile.c`
- New virtual file `/proc/heap` — kernel heap statistics (total/used/free bytes, block count, allocated blocks, free blocks, largest free block) plus integrity status via `kheap_validate()`. Registered in `vfile_init()`; appears in `ls /proc` automatically.

#### Tests
- `tests/mocks/mock_kernel.c` — updated `kheap_validate` mock to the new `int` signature (returns 0 = healthy).
- `tests/unit/test_vfile.c` — 2 new tests: `/proc/heap` is virtual + readable with all expected labels; integrity reports OK.
- `tests/unit/test_memory.c` — 1 new test verifying the `kheap_stats_t` leak-detection fields exist with the right sizes, and that the mock validate API reports healthy.

#### Build environment fix
- The D: drive vanished from this machine; the cross-compiler moved to `C:\Users\roone.DESKTOP-QK3UG2M\x86_64-elf\bin`. Updated `Makefile` `BINDIR_ABS` (and the `-B` flags) to the new location so the kernel builds again. QEMU now lives at `C:\Program Files\qemu\` (11.0.50) and `C:\Program Files\qemu-9.2.0\`.

### Verified under QEMU (`-accel whpx`)
```
> ls /proc
  ... 13 bytes heap ...

> read /proc/heap
  Heap total:   67108576 bytes
  Heap used:    561568 bytes
  Heap free:    66547008 bytes
  Blocks:       12
  Allocated:    11 blocks
  Free blocks:  1
  Largest free: 66547008 bytes
  Integrity:    OK
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **93/93 pass** (90 previous + 3 new: 2 vfile heap + 1 kheap stats layout)
- `/proc/heap` verified end-to-end under QEMU/WHPX (7/7 checks pass)
- Heap leak-detection infrastructure now live: any magic/flag/contiguity corruption is detectable via `kheap_validate()` and visible through `/proc/heap`

---

## 2026-08-21 — Filesystem Usage Accounting (df fix) & ramfs Stats

### Context
HISTORY's earlier session logs showed `df` reporting RAM totals as if they were ramfs usage ("ramfs 130944 KB used" while the filesystem was nearly empty). Root cause: `cmd_df` read `pmm_get_total()/pmm_get_free()` and labeled them as the ramfs row. Additionally, the shell's actual file operations go through `fs_*` (the NTFS-style fs.c, kept in memory when no disk is attached) — not ramfs — and ramfs was never even initialized in the kernel.

### Changes Made

#### `src/fs/ramfs.c` + `include/ramfs.h`
- Added `ramfs_get_stats(ramfs_stats_t*)` — walks the file table and reports total capacity (1 MB logical quota), used bytes (sum of file sizes), free bytes, allocated heap bytes, file and directory counts.

#### `src/fs/fs.c` + `include/fs.h`
- Added `fs_get_stats(fs_stats_t*)` — walks the cluster bitmap (free/total/used bytes) and the MFT (in-use file vs directory counts). This is the filesystem the shell actually reads/writes.

#### `src/ui/shell.c`
- `cmd_df` now reports the real filesystem: fs.c row from `fs_get_stats()` (size/used/avail + live file & directory counts), falling back to ramfs stats only when fs is unmounted. vfile row unchanged.

#### Tests
- **`tests/unit/test_ramfs.c`** (NEW) — 14 host tests compiling the real ramfs.c against mocked kmalloc/kfree: init, create/find, mkdir, write/read round-trip, buffer growth, append, offset reads, read-past-EOF, delete, missing-delete, stats empty/usage-tracking/dir counting, MAX_FILES limit.
- **`tests/unit/test_filesystem.c`** — 1 new test asserting `fs_stats_t` field layout.
- `tests/Makefile` — added `ramfs.c` to REAL_SRCS and `test_ramfs.c` to unit sources.
- `tests/test_runner.c` — registered the new RAM Filesystem suite.

### Verified under QEMU (`-accel whpx`)
```
> df                                  (before any files)
  fs              102332 KB    0 KB    102332 KB    /
  0 files, 0 directories

> create hello.txt / write hello.txt hello world / df
  fs              102332 KB    0 KB    102332 KB    /
  1 files, 0 directories

> mkdir docs / df
  1 files, 1 directories

> rm hello.txt / df
  0 files, 1 directories
```
File/dir counts track create/mkdir/rm live. Used stays 0 KB because small files are stored resident inside the MFT (no clusters allocated) — accurate for this filesystem's design.

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **108/108 pass** (93 previous + 14 ramfs + 1 fs_stats layout)
- All df/accounting checks verified end-to-end under QEMU/WHPX (8/8 pass)

---

## 2026-08-21 — Wire VFS into Boot & Live Mount Table

### Context
The VFS layer (`vfs.c`) was dead code: `vfs_init()` and `ramfs_init()` were never called from the kernel, and `/proc/mounts` was hardcoded strings rather than the real kernel mount table.

### Changes Made

#### `src/kernel/kernel.c`
- Boot now calls `ramfs_init()` + `vfs_init()` before `vfile_init()`, activating the VFS layer (mounts ramfs at `/`) and the fd-table machinery it provides.

#### `src/fs/vfs.c` + `include/vfs.h`
- `vfs_mount()` is now idempotent (no duplicate mountpoints; re-registering the same path updates ops instead).
- New accessors: `vfs_mount_count()` and `vfs_get_mount(index, out_path)` — expose the live mount table.

#### `src/fs/vfile.c`
- `vfile_init()` now registers the virtual namespaces (`/proc`, `/sys`, `/dev`) as path-only entries in the VFS mount table.
- `gen_mounts` (the `/proc/mounts` generator) now iterates the real VFS mount table instead of printing hardcoded rows.

#### Tests
- `tests/Makefile` — compiled the real `vfs.c` into the host suite (alongside ramfs/vfile/kstring/nl_parser).
- `tests/unit/test_vfile.c` — 3 new tests: mount-table accessors (`vfs_init` registers `/`), idempotent re-mounting, and the 4-entry table after `vfs_init`+`vfile_init` (`/`, `/proc`, `/sys`, `/dev`). `test_vfile_mounts_content` now mirrors kernel boot order (`vfs_init` then `vfile_init`).

### Verified under QEMU (`-accel whpx`)
```
> mount
Filesystem    Mount    Type
------------  -------  ----
ramfs         /        ramfs
vfile         /proc    virtual
vfile         /sys     virtual
vfile         /dev     virtual

> read /proc/mounts   (identical, from the live table)
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **111/111 pass** (108 previous + 3 VFS mount-table tests)
- VFS layer activated at boot; `/proc/mounts` and `mount` now reflect real kernel state (6/6 QEMU checks pass)

---

## 2026-08-21 — File Descriptors: Syscalls Route Through the VFS fd Table

### Context
The mission priority list calls out file descriptors. The VFS fd-table machinery (`vfs_open`/`vfs_read`/`vfs_close`, per-process `fd_table_t`) existed but was dead for user programs: `sys_open` returned a raw ramfs fd, `sys_read` called `ramfs_read` directly, and `sys_close` looked in `proc->fd_table` — which nothing ever populated. The syscall layer and VFS layer disagreed on what an fd is.

### Changes Made

#### `src/fs/vfs.c`
- Added a shared **kernel-context fd table** (`kernel_fd_table`) used when `process_current()` returns NULL (kernel context, host tests). `get_current_fd_table()` now returns the per-process table when one exists, else the kernel table.
- `vfs_open()` uses the kernel table for the no-process case (previously it created an orphaned table that was leaked and unusable) and still creates/attaches a per-process table on a process's first open.
- `vfs_init()` zeroes the kernel table.

#### `src/kernel/sys.c`
- `sys_open` → `vfs_open(name, VFS_O_RDONLY)` — allocates a real fd-table entry.
- `sys_read` → `vfs_read(fd, buf, count)` — offset-tracking reads through the fd table.
- `sys_write` — fds 1/2 still go to the terminal; any other fd routes through `vfs_write`.
- `sys_close` was already fd-table-based and now works because opens actually populate the table.

#### Tests
- 3 new host tests in `test_vfile.c` (vfs.c + ramfs.c are compiled into the host suite, mock `process_current()` returns NULL so they exercise the kernel-fd-table path):
  - `vfs_fd_open_read_close` — open, read payload, sequential read hits EOF, close, read-after-close fails.
  - `vfs_fd_write_roundtrip` — create+write through an fd, close, reopen read-only, verify data persisted.
  - `vfs_fd_unlink` — create, close, unlink, reopen fails.

### Verified under QEMU (`-accel whpx`)
- Clean boot to interactive shell; `mount` and `/proc/mounts` still correct; all file operations unaffected (shell uses fs.c directly, untouched).

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **114/114 pass** (111 previous + 3 fd round-trip tests)
- User-mode syscalls now allocate real VFS file descriptors; the fd table is no longer dead code

---

## 2026-08-21 — VFS Pipes (IPC) + Node-Pool Fix

### Context
Mission priority list calls out pipes/IPC. With the fd table wired (previous entry), pipes became the natural next fd-based primitive. Along the way, two pre-existing vfs.c bugs surfaced.

### Bugs found & fixed (`src/fs/vfs.c`)
1. **`alloc_node()` returned the same slot twice** — nothing marked a node as held between allocation and initialization. `vfs_open` never hit it (single alloc per call), but `vfs_pipe` allocates two nodes back-to-back and both got the same slot: the pipe's read and write ends were the same node (both ended up the write type). `alloc_node` now sets `ref_count = 1` on the slot it returns so a second alloc cannot hand it out again.
2. **`vfs_open` leaked its node** — it set `node->ref_count = 1` as a base, so after open+close the node never returned to the pool (ref_count stayed 1); over enough opens the 128-node pool would exhaust. Base is now 0; the fd-table alloc's increment is the only reference, so close returns the node to the pool.

### New: VFS pipes
- `src/fs/vfs.c` + `include/vfs.h`: `vfs_pipe(int fds[2])` creates a pair of fd-table entries (read end type `VFS_TYPE_PIPE_READ`, write end `VFS_TYPE_PIPE_WRITE`) sharing a fixed-size 4096-byte ring buffer in a static pipe pool. `VFS_MAX_PIPES = 16`. Read/write ops enforce direction (wrong-end reads/writes fail); close decrements per-end counters and frees the pipe slot when both ends are gone.
- `include/syscall.h` + `src/kernel/sys.c`: `SYS_PIPE` (15) → `sys_pipe()` → `vfs_pipe()`.
- `src/ui/shell.c`: `pipe` command creates a pipe, writes "hello through pipe" via the write fd, reads it back via the read fd, verifies the second read returns 0, and closes both ends — an end-to-end fd/IPC round trip.

#### Tests (`tests/unit/test_vfile.c`)
5 new host tests: round-trip (write→read→verify), empty-read returns 0, sequential reads (abc/def/EOF), wrong-end rejection, close-frees-pipe (fd reuse + fresh pipe works after closing).

### Verified under QEMU (`-accel whpx`)
```
> pipe
  Pipe created: read fd=0, write fd=1
  Wrote 18 bytes to pipe
  Read 18 bytes: "hello through pipe"
  Second read: 0 bytes (empty pipe)
  Pipe closed
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **119/119 pass** (114 previous + 5 pipe tests)
- Pipes verified end-to-end under QEMU/WHPX (5/5 checks pass)

---

## 2026-08-21 — /proc/self/fd: Open File Descriptors Through the Filesystem

### Context
With the fd table real and pipes in place, the canonical Unix introspection interface is exposing open descriptors through the filesystem: `/proc/self/fd/N` (read through a descriptor) plus `/proc/self/fd` and `/proc/self/fdinfo`.

### Changes Made

#### `src/fs/vfs.c` + `include/vfs.h`
- New introspection accessors: `vfs_fd_count()` (open fds in the current table) and `vfs_fd_info(fd, &type, &size)` (per-fd type/size, -1 for unopened). Works for both the per-process table and the kernel-context fallback table.

#### `src/fs/vfile.c`
- `/proc/self/fd` — virtual directory listing open descriptor numbers.
- `/proc/self/fdinfo` — table of open fds (number, type name, size).
- `/proc/self/fd/<N>` — dynamic virtual file: reading it reads through descriptor N (`vfile_read` falls back to a prefix handler for `/proc/self/fd/`). Type names: file/dir/device/pipe-r/pipe-w.
- `list_proc_self` now also emits `fd` and `fdinfo`.

#### Tests (`tests/unit/test_vfile.c`)
5 new host tests: fdinfo lists an open file fd; the fd dir lists open fds; `/proc/self/fd/N` reads through the descriptor (content round-trip); pipe read/write ends appear as pipe-r/pipe-w in fdinfo; bad paths (non-numeric, empty, unopened fd) return -1.

### Verified under QEMU (`-accel whpx`)
```
> ls /proc/self
  0 bytes  pid / name / status
  [DIR]  fd
  0 bytes  fdinfo

> read /proc/self/fdinfo
fd  Type       Size
--  ---------  ----
(empty — shell closes its fds immediately; host tests cover populated case)

> pipe   (still works: hello through pipe round-trips)
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **124/124 pass** (119 previous + 5 fd-introspection tests)
- fd introspection verified under QEMU/WHPX (4/4 checks pass)

---

## 2026-08-21 — CI: Host Test Suite Gating + GRUB ISO Boot Test

### Context
The CI pipeline's biggest verification gap: the full host test suite (124 tests) was never run in CI at all, and the "Basic boot test" could never fail (`|| true` + informational grep). Both needed to become real gates.

### Investigation
- Reconfirmed (as documented in the boot-path session) that `qemu -kernel` + TCG never reaches the kernel on this machine — instruction tracing shows only SeaBIOS executing, the CPU never leaves the reset vector, and QEMU's internal multiboot option-ROM loader never jumps to 0x100000. Both Program Files QEMU 11.0.50 and MSYS2 11.0.1 behave identically under TCG. The same `-kernel` invocation works under WHPX (shell reaches `> ` prompt), so the kernel is not at fault.
- Because GitHub Actions runners run qemu under TCG (not WHPX), the old `-kernel` boot test would fail in CI for the same environment reason. The robust path is the **GRUB ISO**: the CI already builds `bin/os.iso` with `grub-mkrescue`, and GRUB's own multiboot loader performs the handoff in guest code — accelerator-independent.

### Changes Made

#### `.github/workflows/ci.yml`
- **New `host-tests` job** — checks out, builds the test runner (`cd tests && make clean && make all`), and runs `./bin/test_runner` (exits non-zero on any failure). This is the first time the full 124-test suite gates CI.
- **`verify` job** now `needs: [build, host-tests]` and only runs when both succeed.
- **Boot test rewritten** as a real gate:
  - Downloads the `os-iso` artifact (previously only the raw kernel).
  - Boots with `qemu-system-x86_64 -cdrom bin/os.iso -display none -serial stdio -m 128M -no-reboot` (GRUB multiboot path, TCG-safe).
  - Fails the job unless `Starting Shell` appears in serial output; `> ` prompt is a secondary informational check.

#### `tools/qemu_boot_test.sh`
- Fixed the stale toolchain path: the D: drive no longer exists (cross-compiler moved to `~/x86_64-elf/bin`). The script now probes `$HOME/x86_64-elf/bin` first, falling back to `/d/tools/x86_64-elf/bin`, and exports the correct PATH for the build step.

### Verification
- Kernel builds clean (0 compiler warnings, 2 pre-existing informational linker notes).
- Host suite passes **124/124** from a clean build (`make clean && make all`).
- Shell boots to `> ` prompt under QEMU/WHPX (serial markers: `Plan 0 v0.4.1`, `Starting Shell`, `Type 'help' for commands.`).
- GRUB ISO could not be tested locally (no grub-mkrescue on this machine) — the gate runs on CI's Ubuntu runner which installs `grub-pc-bin` + `xorriso` and already builds the ISO in the build job.

### Results
- CI now gates on: kernel build (gcc, 0 warnings), full 124-test host suite, GRUB-ISO boot reaching the shell.
- Boot test went from informational-only to a hard gate on `Starting Shell`.

---

## 2026-08-21 — VFS dup/dup2: File Descriptor Duplication

### Context
With the fd table real (previous sessions) and pipes in place, the classic Unix fd primitive `dup`/`dup2` was missing entirely — no `SYS_DUP`, no `vfs_dup`, nothing. These are the primitives shell redirection (`cmd > file 2>&1`) builds on.

### Changes Made

#### `src/fs/vfs.c` + `include/vfs.h`
- `vfs_dup(fd)` — duplicates a descriptor into the lowest free slot. The copy shares the same `vfs_node_t` (and therefore the same file offset) with the original, exactly like POSIX.
- `vfs_dup2(oldfd, newfd)` — places the copy at exactly `newfd`, closing whatever descriptor is already there first (POSIX semantics). `dup2(fd, fd)` is a no-op returning `fd`. Bounds-checks both fds.
- Both operate through the current fd table (per-process or kernel-context fallback), so they work for user processes and kernel context alike.
- Correct-by-construction semantics: because the fd table is ref-count based and the file offset lives on the node, dup sharing a node gives shared-offset semantics for free, and closing the original doesn't invalidate the copy (refcount keeps the node alive).

#### `include/syscall.h` + `src/kernel/sys.c`
- `SYS_DUP` (16) → `sys_dup(fd)` → `vfs_dup`; `SYS_DUP2` (17) → `sys_dup2(oldfd, newfd)` → `vfs_dup2`. Both added to the `syscall_handler` dispatch table.

#### `src/ui/shell.c`
- New `dup` command — end-to-end demonstration: creates `dup_test.txt`, writes a payload, rewinds, `dup`s onto a fresh fd, reads through the duplicate (shares offset), closes the original, and confirms the duplicate still reads — then unlinks the file.

#### Tests (`tests/unit/test_vfile.c`)
4 new host tests: `vfs_dup_shared_offset` (dup reads payload, original offset advances in step, dup survives original close), `vfs_dup2_replaces_target` (dup2 replaces an open target fd), `vfs_dup2_same_fd_noop` (no-op, single close frees it), `vfs_dup_invalid_fd` (unopened/negative/out-of-range all fail).

#### Tool
- `tools/drive_dup.py` — QEMU TCP-monitor driver for end-to-end verification.

### Verified under QEMU (`-accel whpx`)
```
> dup
  Wrote 16 bytes
  dup(0) = 1
  Read via dup fd 1: "dup test payload"
  After closing original, dup fd read returns 0 (0 = survived close)
  Dup test complete
```

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: **128/128 pass** (124 previous + 4 dup/dup2 tests)
- dup verified end-to-end under QEMU/WHPX (5/5 checks pass)
- `SYS_DUP`/`SYS_DUP2` available to user programs; shell redirection is now buildable on top

## Shell redirection (`cmd > file` / `cmd >> file`)

- Added `terminal_capture_begin/end()` in vga.c — terminal output can be
  captured into a buffer (screen + serial silent) instead of displayed.
- `process_command` in shell.c now recognizes a standalone `>` token:
  the command runs with output captured, then the captured text is written
  to the target via the normal file path (`vfile_write` for virtual files,
  `fs_open`/`fs_write` for real files). `>>` appends, `>` truncates.
- Fixed `>>` split bug: the command part was terminated after the second
  `>`, leaving the first `>` in the command and causing a spurious
  "Error: redirection needs a target file" inside the captured output.
- `echo hello world > out.txt` / `ls > listing.txt` / append / `/dev/null`
  verified end-to-end under QEMU/WHPX (9/9 checks pass).

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: 128/128 pass (no new host tests; redirection verified in QEMU)
- Redirection verified end-to-end under QEMU/WHPX (9/9 checks pass)

## File permissions: read-only protection (`chmod`)

- Added `MFT_FLAG_READONLY` (0x0004) to the MFT entry flags in fs.c —
  the flag is persisted with the entry, so it survives remounts.
- Added `fs_set_readonly(path, ro)` / `fs_is_readonly(path)`.
- Enforcement in the filesystem layer: `fs_open` denies write/append/truncate
  on a read-only file (returns NULL); `fs_unlink` denies deletion.
- New shell command `chmod <file>` (status), `chmod -w <file>` (protect),
  `chmod +w <file>` (unprotect). Write and delete failures on protected
  files now say "File is read-only (chmod +w to unprotect)".
- Verified end-to-end under QEMU/WHPX (8/8 checks): protect → write denied,
  read still works, rm denied → unprotect → write/rm work again.

### Results
- Kernel build: 0 compiler warnings (2 pre-existing informational linker notes)
- Host tests: 129/129 pass (128 previous + readonly_flag_semantics)
- chmod verified end-to-end under QEMU/WHPX (8/8 checks pass)
