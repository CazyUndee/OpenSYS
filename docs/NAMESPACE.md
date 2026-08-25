# Plan0 Namespace Specification

**Status**: Authoritative. Decisions recorded here are mirrored in `STATE.md`.
**Version**: 1.0 (2026-08-25)

---

## 1. Core model

```
Plan0 path → path parser → namespace resolver → resource → backend/driver/fs/network
```

Everything addressable in Plan0 is a *resource*. Resources are addressed by
Plan0 paths. Applications never learn *how* a resource is provided; the
resolver determines the backend. There is exactly ONE namespace — not one
namespace per device type, protocol, or mount convention.

## 2. Numeric resource roots

A Plan0 path begins with a single decimal digit — the **resource domain root**:

| Root | Meaning |
|------|---------|
| `0`  | The local Plan0 machine |
| `1`–`9` | Reserved for future resource domains (other machines, network resources, remote systems) |

Roots are single digits. `10/` is NOT a valid root — it parses as root `1`,
component `"0"` (which will not resolve unless a future domain defines it).
This keeps the grammar fixed-width at the position where disambiguation
matters most and leaves the entire remaining component space free.

Example future use (NOT implemented now):

```
9/main/index.html     ← a resource exposed by domain 9
```

An editor could open that path with no web-specific syntax. The resolver,
not the application, decides how domain 9 is reached.

## 3. Root `0/` — the local machine

`0/` is the local Plan0 system. Its defined subtrees:

```
0/
├── hardware/          physical resources
│   ├── memory/
│   │   └── ram        system RAM (introspection)
│   ├── storage/
│   │   ├── hdd/       spinning storage device(s)
│   │   └── ssd/       solid-state storage device(s)
│   └── cpu/           processor information
│
├── system/            OS-provided information & controls
├── dev/               character device shims (null, zero, console)
└── user/              logical user storage (stable across hardware changes)
```

Levels exist only when they mark a real architectural distinction
(device vs. partition vs. filesystem vs. logical view). No level exists
for tradition.

## 4. Hardware namespace — full paths

Full paths are the conceptual source of truth:

```
0/hardware/memory/ram
0/hardware/storage/hdd
0/hardware/storage/ssd
0/hardware/cpu
```

A full path explains itself. A reader who has never seen Plan0 can infer
what `0/hardware/storage/ssd/partitions/1` addresses. This property takes
priority over brevity everywhere in this specification.

## 5. Short aliases

Frequently used paths gain a short alias. An alias is a pure rename — it
resolves to the SAME resource object as its canonical path. There is one
implementation; two spellings.

| Alias | Canonical |
|-------|-----------|
| `0/hmr` | `0/hardware/memory/ram` |
| `0/hsh` | `0/hardware/storage/hdd` |
| `0/hss` | `0/hardware/storage/ssd` |

Rules:

1. An alias covers the WHOLE subtree: `0/hss/partitions/1` ≡
   `0/hardware/storage/ssd/partitions/1`.
2. Resolution expands aliases BEFORE classification, so downstream code
   sees only canonical paths. No backend ever checks for alias strings.
3. Every alias must be discoverable through introspection (see §13):
   asking about `0/hardware/storage/ssd` reports its alias `0/hss`.
4. Abbreviations must justify themselves against the whole hierarchy
   (`hss` = **h**ardware/**s**torage/**s**sd — three initials, one obvious
   expansion, documented). Stacked unexplained abbreviations are banned
   (`0/hss/p/1/u/d/c` style is structural rot).

## 6. Partition representation — DECISION

**Every filesystem-capable storage volume is addressed as
`<device>/partitions/<n>`, always — including single-partition devices.**

```
0/hardware/storage/ssd/partitions/1        ← even if the SSD has exactly one partition
0/hardware/storage/hdd/partitions/1
0/hardware/storage/hdd/partitions/2
```

Analysis that led to this decision:

- **Resource kinds differ.** The device (`0/hardware/storage/ssd`) is a
  block device: raw sectors, model, size, partition table. A partition is a
  different kind of resource: a block range that can carry a filesystem.
  Two genuine architectural distinctions justify both levels.
- **Mounting determinism.** If a bare device could sometimes carry a
  filesystem directly, mount resolution needs fallback rules ("try
  partitions/1, then the device"). Fallbacks are nondeterministic and are
  exactly the special-case rot this namespace bans. With this decision the
  rule is total: *filesystems mount on partitions, period.*
- **Uniform applications.** Shell, APIs, and future programs treat every
  volume identically regardless of partitioning. No code asks "is this
  device partitioned?" — it lists `partitions/` and gets the honest count
  (one entry for a single-partition SSD).
- **Topology honesty.** The tree mirrors actual GPT state. Devices with
  different partition counts simply list differently. Nothing pretends.
- **Future domains inherit the shape.** A network domain exposing volumes
  uses the same `<container>/partitions/<n>` pattern if it has them.
- **Cost accepted:** one extra component in the common case. Deliberate —
  structure is never flattened to save characters (§ Design philosophy).

The bare device path remains valid and meaningful: it addresses the DEVICE
(raw I/O, geometry, partition table introspection) — never a filesystem.

## 7. Logical user storage

Physical topology and logical storage are distinct layers:

```
physical : 0/hardware/storage/ssd/partitions/1
logical  : 0/user/...
```

- `0/user/` is the stable user-storage root. Applications bind to it, never
  to hardware paths, so relocating user storage (different disk, different
  partition, later a network volume) never changes application paths.
- Boot policy binds `0/user/` to a concrete volume (initially: first
  available storage partition). The binding is introspectable.
- Device-scoped views like `0/hss/user/` MAY be provided later for
  explicitly targeting the user area of one specific device. They are
  conveniences, never the primary addressing.

## 8. Memory namespace

`0/hardware/memory/ram` exposes RAM as a resource. Initial semantics:
introspection only (totals, free, stats — backed by the existing PMM/heap
reporting). Deeper decomposition (physical pages, kernel memory, process
mappings) is explicitly future work; the namespace will NOT grow one layer
per internal bookkeeping detail. Internal structures stay internal until an
architectural distinction justifies exposure.

## 9. System namespace

OS-provided information and controls live under `0/system/`:

```
0/system/kernel/name      0/system/kernel/version     0/system/kernel/arch
0/system/runtime/uptime   0/system/processes          0/system/mounts
0/system/heap             0/system/hostname           ...
```

(Exact inventory migrates incrementally from the legacy `/proc`, `/sys`
trees — see §14.)

Character shims remain under `0/dev/`: `null`, `zero`, `console`. These are
genuinely device-like; they do not justify a Unix-style global `/dev`.

## 10. Path parsing rules (grammar)

```
path        := digit ( '/' component )*          bare root "0" is valid
component   := ( lower | digit | '-' | '_' | '.' )+      but not all dots
```

- Case-insensitive on input; canonicalized to lowercase.
- Dots are allowed inside components so content-bearing leaves parse
  (`9/main/index.html`, `readme.txt`); all-dot components (`..`) are
  rejected — there is no traversal semantics in this namespace.
- Empty components (`//`) rejected. Trailing slash allowed and ignored.
- The bare root (`0`, `9`, …) addresses the domain root itself.
- Maximum path length 256 bytes, maximum depth 16 components.
- `partitions/<n>` requires n ≥ 1.
- Anything violating the grammar fails resolution with a parse error —
  parsers reject early; resolvers never guess.

## 11. Namespace resolution

Resolution pipeline (table-driven; there are no per-path string compares
scattered in kernel code):

```
input path
  → normalize (lowercase, strip trailing '/')
  → alias expansion (longest-match over the alias table, repeatable)
  → split into components
  → walk the resource-node table (static, declarative)
  → leaf handler binds the resource to a backend
```

Result descriptor:

```
ns_resource_t {
    kind          RESOURCE_*   (storage-device, partition, memory-ram,
                               system-node, dev-shim, user-store, ...)
    domain        numeric root (always 0 today)
    part_index    partition number when applicable (-1 otherwise)
    canonical[]   fully expanded canonical path
}
```

Backends register capability, they do not register path strings. Adding a
resource = adding one table row (+ handler), not touching resolvers.

Unknown components fail with a structured "no such resource" result —
never a silent fallthrough into some other subsystem.

## 12. Application access

Applications use one call family regardless of resource kind:

```
ns_open(path)      → resource handle (future: unified fd integration)
ns_read / ns_write / ns_close
ns_stat(path)      → kind, size, writability
ns_describe(path)  → human-readable explanation incl. aliases (introspection)
```

During migration the shell and kernel continue using existing calls;
new namespace calls wrap them (see §14). Applications should never parse
paths themselves to decide "which subsystem do I call" — that decision
belongs to the resolver.

## 13. Discoverability / introspection

The system describes its own namespace:

- `ns <path>` (shell) prints what a path is: canonical form, alias links,
  resource kind, backend, and children for directories.
- Listing a directory resource enumerates children (e.g. listing
  `0/hardware/storage` yields `hdd`, `ssd` — reflecting REAL detected
  hardware, not a hardcoded wish).
- Asking about an aliased path shows BOTH spellings.

A user must be able to answer "what is 0/hss?" from inside Plan0 without
external docs.

## 14. Compatibility and migration from the current kernel

Audited current state (2026-08-25):

| Current | Nature | Target under `0/` |
|---|---|---|
| `/` (VFS root mount) | whole-disk fs.c via fs_vfs_ops | `0/hardware/storage/<dev>/partitions/<n>/` once fs mounts on partitions; until then unchanged |
| `/proc/*` | vfile registry (Unix proc) | `0/system/*` (uptime→runtime/uptime, processes, mounts, heap, hostname…) |
| `/sys/kernel/*` | vfile registry | `0/system/kernel/*` |
| `/sys/hardware/platform`, `/sys/devices/pci` | vfile registry | `0/hardware/platform`, `0/hardware/pci` |
| `/dev/null|zero|console` | vfile rw shims | `0/dev/*` |
| drive-letter thinking | none present | none introduced |

Migration rules:

1. **Legacy paths keep working throughout.** They become compatibility
   aliases resolved by the same machinery (alias → canonical), never
   separate implementations. Existing tests must stay green at every step.
2. New features integrate through the namespace first; old surfaces migrate
   only after their replacements are proven.
3. Removal of a legacy path is a deliberate, recorded decision — never drift.

## 15. Explicit non-goals (now)

- No networking implementation (domain 9 stays reserved).
- No NVMe/AHCI (namespace already models them: additional rows, not changes).
- No permission model on resources yet (grammar/resolver leave room: a
  resource descriptor gains an ACL field when needed).
- No renaming of working shell commands; they gain namespace awareness
  gradually.

## 16. First implementation slice (accepted)

Slice 1 — foundation, zero disruption:

1. `src/fs/ns.c` + `include/ns.h`: grammar parser, alias expansion,
   table-driven resolver producing `ns_resource_t`; `ns_describe()`.
2. Storage topology hook: resolver reports real device/partition facts from
   the existing disk/GPT/part stack (read-only).
3. Shell `ns <path>` introspection command (QEMU-observable).
4. Host tests `tests/unit/test_ns.c`: grammar accept/reject, alias≡canonical
   equivalence, partition-index rules, unknown-resource errors.

Explicitly OUT of slice 1: VFS/vfile rewiring, fs relocation, legacy-path
aliasing (legacy trees untouched and green).

## 17. Testing requirements (ongoing)

- Parsing: valid/invalid grammar cases (§10).
- Equivalence: alias and canonical resolve to identical descriptors.
- Topology: storage paths reflect actual detected hardware.
- Errors: invalid root, unknown resource, bad partition index, malformed path.
- Compatibility: entire existing suite passes unchanged at every stage.
