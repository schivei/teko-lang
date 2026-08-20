# Package toolchain + own linker — design-ahead (0.3.x roadmap)

> **Status:** design-ahead / roadmap. NOT 0.3.1 execution. No crumbs, no implementation until the
> memory campaign closes (≤1.5 GB C-leg fixpoint + gen2==gen3 + green tests — D52). Reconstructed by the
> coordinator from the `arch-toolchain-linker` architect's report (the original worktree doc was lost to a
> force-remove; content preserved verbatim from the agent's code-grounded findings).

## Why this exists

Teko's first big projects are a multi-paradigm DB (as a local **package**), a **bare-metal kernel**, and
maybe an OS. Two owner rulings drive the toolchain: (1) packages must be readable/addressable/linkable so
side-cars (DB, `tk_data`, …) live as isolated projects; (2) a bare-metal kernel has **no OS `ld`** — teko
needs its **own linker**. The endgame is a **fully self-contained toolchain**: teko emits AND links its own
binaries, zero external tools (no cc/gcc/clang AND, for freestanding, no `ld`).

## 1. Package mode — the reality in `src/` (three corrections to the initial briefing)

1. **Generation ALREADY works** (owner's belief confirmed). `teko build` on an `[artifact] kind="package"`
   emits `<name>-<version>.tkl` — `src/build/project.tks:1173-1203`.
2. **The package UNIT is `.tkl`, not `.tkb`.** `.tkl` is a ZIP of `.tkh` (public header) + `.tkb` (typed
   tree) + `.tsym`. `.tkb` is the typed tree *inside* the package — a vocabulary fix, not a tension.
3. **There is no `package.tks`.** Package logic lives in `src/build/manifest.tks` (`[dependencies]`) +
   `src/build/project.tks` (`load_dep_program`/`load_deps_program`, `:104-183`). **Consumption already
   exists** at the typed-tree level: it reads `packages/<dep>-*.tkl` and merges before monomorphization
   (`:224`). So "teach `package.tks`" really means: **harden that read path + promote `[dependencies]`**
   (keys-only today, `manifest.tks:360`) + add LOCAL `path=` resolution + optionally extract a named
   `package.tks`.

## 2. Own linker — two-level model (Level 1 is already half-built)

- **Level 1 (M4):** teko already links ELF **without cc**, driving `ld`/`lld` directly with its own argv —
  `link_object_elf_direct` (`project.tks:1021-1052`); arch assertion + MinGW ban in `src/build/linker.tks`.
  It still uses the libc crt (`Scrt1.o`/`crti.o`/`crtn.o`, `-lc`). M4 finishes Mach-O/COFF direct + the
  no-C endgame.
- **Level 2 (own linker — a NEW milestone, "M-linker"):** resolve symbols + relocate + section/segment
  layout + synthesize teko's own `_start` + emit a **freestanding** image, with **no `ld`**. It **reuses
  the existing backend**: the linker's object reader is the inverse of `src/backend/objfile_elf.tks` (same
  section/symtab/reloc model, `R_X86_64_*` at `:188-198`); the ELF-writing half already exists; the entry
  seam is `wrap_native_entry`/`native_entry_stub` (`src/lir/lower.tks:6518-6566`), whose freestanding
  variant drops the libc-crt0 assumptions. **The bare-metal kernel is what forces Level 2** — hosted
  programs are content with Level 1 + OS `ld`.

## 3. D54 split (teach now / use later)

- **TEACH NOW (not D52-gated):** the consumption surface — `[dependencies]` promotion + LOCAL `path`
  resolution + `.tkl` read-path hardening.
- **USE, deferred behind D52 (native is last):** `.tkb`→native-`.o` linking + the own linker (Level 2).
- The package **server** stays out of scope (a separate repo, after native).

## 4. Design fork — resolved law-first (no HALT)

The one substantive fork: **Model A** = typed-tree pre-link (the legislated semantic contract, §215-226)
vs **Model B** = the owner's "link `.tkb` into `.o`". Resolution: **A is the semantic contract; B is a
post-native build-cache that the own linker enables.** Not a tension — two layers.

## 5. Adjacent findings (real gaps in `src/`, flagged — not yet issues)

- `load_dep_program` picks the **first** `.tkl` with **no version check** and **ignores the `.tkh` public
  gate**.
- `[dependencies]` **sub-keys are silently dropped** (`manifest.tks:360`).
- The **`freestanding` knob is a live stub that detours to `invoke_cc`** (`project.tks:921`) — **the last
  place the native path still reaches for a C driver.** This is a concrete no-C endgame target (it belongs
  with the M4 / M-linker work).
