#!/usr/bin/env sh
# scripts/provenance_gate_test.sh — the REGRESSION for `scripts/provenance_gate.sh`, the gate that
# refuses a committed `bootstrap/teko.c` unless a matching `bootstrap/PROVENANCE` record proves
# which CI run produced it, and that record is proven against a file git actually tracks.
#
# WHY THIS DESERVES A TEST OF ITS OWN, when the gate is only ever exercised against a real
# committed tree: the failure mode it closes is SILENT by nature — a hand-built C that happens to
# sit at the right path costs nothing to commit and nothing distinguishes it from a real harvest
# until something checks. A gate whose own verdict is unverified would be exactly that same blind
# trust, one layer up. Each load-bearing claim is proven here against throwaway fixtures:
#
#   1. no C, no record — nothing to prove, PASS;
#   2. a C sitting outside any git working tree at all — FATAL (exit 2), tracked-ness undecidable;
#   3. C present, tracked, no record at all — REFUSED (exit 1), the owner's literal gap;
#   4. C present but NOT tracked by git — REFUSED (exit 1), the sibling gate's own criterion
#      (`scripts/no_c_in_tests_gate.sh`) reused rather than re-derived;
#   5. a complete, matching record over a tracked file — PASS;
#   6. the C changes after the record was written, without updating it — REFUSED (exit 1);
#   7. a record whose `sha256` was simply wrong from the start — REFUSED (exit 1);
#   8. every way a record can be malformed — a missing key, a `c:` that names the wrong file or
#      one that does not exist, an empty file — is FATAL (exit 2), never read as "no record".
#
# Each fixture is a throwaway git repository built in a temp dir (the same shape
# `scripts/no_c_in_tests_gate_test.sh` uses), so this needs no compiler and no network and runs in
# the `CI gate` job alongside `scripts/degrau_test.sh`, in under a second, on every PR.
#
# Usage:  sh scripts/provenance_gate_test.sh
# POSIX sh only.
set -eu

HERE="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
GATE="$HERE/provenance_gate.sh"

FAILED=0
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT INT TERM
ROOT="$WORK/repo"

pass() { printf '  ok   %s\n' "$1"; }
fail() { printf 'ERROR: %s\n' "$1" >&2; FAILED=1; }

# init_repo — a fresh git repository at $ROOT, with the identity git needs to stage a path. Every
# fixture that needs a tracked file starts from this, so a stale index from the PREVIOUS fixture
# can never leak into the next.
init_repo() {
    rm -rf "$ROOT"
    mkdir -p "$ROOT/bootstrap"
    ( cd "$ROOT" && git init -q && git config user.email t@example.com && git config user.name t )
}

# track_bootstrap — stages every path under `bootstrap/` so `git ls-files` reports it, WITHOUT
# committing. `git ls-files` reads the index, not HEAD, so this is enough to make a path "tracked"
# for the gate's purposes — the same fact `scripts/no_c_in_tests_gate.sh` relies on.
track_bootstrap() {
    ( cd "$ROOT" && git add -A -- bootstrap >/dev/null )
}

# digest_of FILE — the same portable sha256, computed independently of the gate under test so a
# fixture can hand it a value the gate must AGREE with rather than one the gate produced itself.
digest_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

# gate_rc ROOT_DIR — the gate's verdict for the current fixture: 0 pass, 1 refused, 2 fatal.
# Wrapped so the caller can compare a number instead of arranging its own `|| rc=$?` around
# `set -e`.
gate_rc() {
    gr_rc=0
    sh "$GATE" "$1" >"$WORK/out.log" 2>"$WORK/err.log" || gr_rc=$?
    printf '%s' "$gr_rc"
}

# expect_rc ROOT_DIR WANT NAME — asserts the verdict for the fixture the caller just laid down.
expect_rc() {
    er_root="$1"; er_want="$2"; er_name="$3"
    er_got="$(gate_rc "$er_root")"
    if [ "$er_got" = "$er_want" ]; then
        pass "$er_name (rc=$er_got)"
    else
        fail "$er_name: want rc=$er_want, got rc=$er_got"
        sed 's/^/    /' "$WORK/err.log" >&2
    fi
}

# write_c CONTENT — lays down the fixture's `bootstrap/teko.c` with the given throwaway content
# (never real compiler output — see the header), tracked in the fixture's index.
write_c() {
    printf '%s' "$1" > "$ROOT/bootstrap/teko.c"
    track_bootstrap
}

# write_provenance C RUN COMMIT SHA256 — lays down a complete-shaped bootstrap/PROVENANCE, tracked
# in the fixture's index.
write_provenance() {
    printf 'c:       %s\nrun:     %s\ncommit:  %s\nsha256:  %s\n' "$1" "$2" "$3" "$4" \
        > "$ROOT/bootstrap/PROVENANCE"
    track_bootstrap
}

echo "--- provenance gate ---"

init_repo
expect_rc "$ROOT" 0 "no C, no record — nothing to prove"

NOTAGIT="$WORK/not-a-repo"
mkdir -p "$NOTAGIT/bootstrap"
printf 'x' > "$NOTAGIT/bootstrap/teko.c"
expect_rc "$NOTAGIT" 2 "a C outside any git working tree at all is FATAL (undecidable)"

write_c 'fixture: not a real compiler output, first revision'
expect_rc "$ROOT" 1 "C present, tracked, no record at all — the owner's literal gap"
grep -q "PROVENANCE' does not exist" "$WORK/err.log" \
    && pass "the missing-record message names the file" \
    || fail "the FAIL message did not name the missing record: $(cat "$WORK/err.log")"

# THE TECHNIQUE REUSED FROM scripts/no_c_in_tests_gate.sh: a file that git does not track is
# GENERATED (or otherwise stray), never the one legitimate committed C — asked here of `git
# ls-files` directly, the same question that gate asks of a whole project tree.
DIGEST="$(digest_of "$ROOT/bootstrap/teko.c")"
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d "$DIGEST"
( cd "$ROOT" && git rm -q --cached bootstrap/teko.c )
expect_rc "$ROOT" 1 "C present but NOT tracked by git — refused before its hash is even read"
grep -q "NOT tracked by git" "$WORK/err.log" \
    && pass "the untracked-C message says so plainly" \
    || fail "the FAIL message did not name the untracked file: $(cat "$WORK/err.log")"

write_c 'fixture: not a real compiler output, first revision'
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d "$DIGEST"
expect_rc "$ROOT" 0 "a complete record over a tracked file whose digest matches"

write_c 'fixture: not a real compiler output, SECOND revision (unrecorded)'
expect_rc "$ROOT" 1 "teko.c changed after the record was written, without updating it"

write_c 'fixture: not a real compiler output, first revision'
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d 0000000000000000000000000000000000000000000000000000000000000000
expect_rc "$ROOT" 1 "a sha256 that was simply wrong from the start"

# EVERY MALFORMED RECORD IS FATAL, NEVER "no record". A `bootstrap/PROVENANCE` that exists but does
# not parse means a human started the ceremony and did not finish it — degrading that to "none"
# would read as the ordinary unrecorded-harvest case (rc=1) and hide which state actually held.
write_provenance bootstrap/teko.c 4242 deadbeefcafef00d ""
expect_rc "$ROOT" 2 "a record missing its sha256 is FATAL, not 'no record'"

printf 'run: 4242\ncommit: deadbeefcafef00d\nsha256: %s\n' "$DIGEST" > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" 2 "a record missing its c: is FATAL"

printf 'c: bootstrap/teko.c\ncommit: deadbeefcafef00d\nsha256: %s\n' "$DIGEST" > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" 2 "a record missing its run: is FATAL"

printf 'c: bootstrap/teko.c\nrun: 4242\nsha256: %s\n' "$DIGEST" > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" 2 "a record missing its commit: is FATAL"

write_provenance bootstrap/other.c 4242 deadbeefcafef00d "$DIGEST"
expect_rc "$ROOT" 2 "a record naming a file other than the one being checked is FATAL"

write_provenance bootstrap/absent.c 4242 deadbeefcafef00d "$DIGEST"
expect_rc "$ROOT" 2 "a record naming a file that does not exist is FATAL"

: > "$ROOT/bootstrap/PROVENANCE"
track_bootstrap
expect_rc "$ROOT" 2 "an empty record is FATAL"

write_provenance bootstrap/teko.c 4242 deadbeefcafef00d "$DIGEST"
expect_rc "$ROOT" 0 "restored to a complete, matching record over a tracked file — PASS again"

if [ "$FAILED" = "1" ]; then
    echo "provenance_gate_test FAILED." >&2
    exit 1
fi
echo "provenance_gate_test OK — no C without a record, no untracked C mistaken for a committed"
echo "one, no changed C without a matching record, and a broken record never reads as absent."
