# `.32` — a GENERATED HARNESS that spawns the work as PROCESSES

Owner ruling 2026-07-28: *"sobre os testes paralelos: vamos corrigir na .32, mas já tenho uma ideia
(a mesma que sugeri para testes unitários), gerar um executável com uma main que dispara estes
processos paralelos, isso muda o jogo. Para testes unitários, seriam isolamentos de fato, para
regressões, seriam processos."*

## What it fixes that parallelism alone does not

**Unit tests get real isolation, and the `.32` mock item disappears.** Today all 1026 unit tests run
in ONE process, sequentially: a test that reaches `exit()`, `panic()` or a segfault kills the binary
and takes every test after it with it. The `.32` list carried "panic/exit mocks so a failed unit test
does not kill the binary" as the answer. A process per test is a BETTER answer, because a mock stops
testing the thing it replaces — under a subprocess the real `exit` HAPPENS and is OBSERVED.

**Regressions lose the shell.** The runner reaches every child through `sh -c`, and the portability
tax of that is visible in the source itself: `windows_path_to_sh`, `windows_drive_to_mount` and the
git-bash mount-form detour all exist because Windows has no `/bin/sh`. A harness that spawns children
itself deletes that whole layer.

## The one primitive it needs

`teko::process::run` is SYNCHRONOUS — it forks, execs, and waits, in one call. Parallelism needs it
split in two: a `spawn` returning the pid and a `wait` reaping the status. The `fork`/`execvp` is
already in `src/runtime/teko_rt.c`; this separates the wait, it does not add a syscall.

## Two hazards, both already answered by what shipped in .31

1. **The verdict channel.** If a child reports pass/fail through its exit status, a test that
   legitimately calls `exit(3)` is indistinguishable from one that failed with 3. The verdict needs a
   channel of its own — which is exactly what the batch runner already does: each child writes its
   own `<prefix>.rc`, beside but separate from its `.out` and `.err`.
2. **Output ordering.** Parallel children interleave stdout, and the report's `test X ... ok` lines
   are ordered. Same answer: capture per child, replay in index order at the end.

## Why the .31 work is half of this and not throwaway

`run_captured_batch` is the CAPTURE-AND-FOLD half: per-child `.rc`/`.out`/`.err`, read back and
reassembled by index, with a documented time attribution. This ruling replaces the LAUNCHER — `sh -c`
out, a generated `main` in. The layer above it does not move.

The natural shape of that `main` is SELF-RE-EXECUTION: the binary already carries the test dispatch
table, so it takes a selector in `argv[1]` and invokes itself once per test. No new codegen — the
same dispatch that runs a test in-process runs it in a child.

## Ordering against the rest of `.32`

This lands BEFORE the per-row file-sourced regression builds
(`docs/memory/bulk-native-verdicts-0.3.1.md`): 203 independent builds inside one channel are only
affordable once the launcher is cheap, and only trustworthy once a crashing row cannot take the run
down with it.
