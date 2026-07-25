#!/usr/bin/env sh
# scripts/ci_provision_teko.sh — put the LATEST released teko compiler on PATH.
#
# The Teko-only ruling retired the C bootstrap: CI no longer builds the compiler,
# it downloads the previously-released per-platform binary and runs the PR corpus
# through it. This is the classic self-hosting seed — each merge publishes a new
# release, so an open PR always validates against the prior merge's compiler.
#
# SAME MECHANICS AS install.sh (owner ruling 2026-07-18): the seed comes from the
# CANONICAL repo's public releases over plain HTTPS (curl/wget), NOT from the
# repository CI happens to run in. The old form (`gh` + GH_TOKEN scoped to
# GITHUB_REPOSITORY) broke on every fork — a fork has no releases, so provisioning
# always failed there. Downloading from the canonical repo works identically on
# the canonical repo itself, on any fork, and on a bare container — no `gh`, no
# required token. GH_TOKEN, when present, is used ONLY as an optional
# Authorization header on the release-LIST API call (runner IPs share the
# unauthenticated rate limit); asset downloads are plain public URLs.
#
# COMMITTED-SEED FALLBACK (TRANSITIONAL — .31 ONLY, removed in the first .32 wagon).
# The merge train squash-merges its wagons, so the intermediate SHAs the pinned ladder in
# scripts/build_with_seed_fallback.sh stands on never become reachable from main. A host that
# finds NO usable published seed therefore has no rung at all. bootstrap/seeds/ holds one
# xz-compressed seed per HOST THAT RUNS THE COMPILER (five: the GitHub-hosted runner set), cut
# by the `seeds` job in native.yml with scripts/cross_compile_seeds.sh. This script falls back
# to that blob, and the fallback verifies the archive's sha256 against
# bootstrap/seeds/SEEDS.sha256 BEFORE decompressing anything.
#
# THE VERIFICATION IS SHELL + sha256sum, DELIBERATELY: never Teko code compiled by the very
# seed under test, which would make the check circular.
#
# Usage:   ci_provision_teko.sh <LABEL>
#          LABEL = the release asset platform label, e.g. linux-x86_64 /
#          macos-arm64 / windows-x86_64 (matches teko-<LABEL>.{tar.gz,zip}).
#
# Environment:
#   TEKO_SEED_REPO              owner/repo to seed from (default: teko-org/teko-lang).
#   TEKO_SEED_PREFER_COMMITTED  set to 1 to take the committed bootstrap/seeds/ blob FIRST and
#                               skip the release probe entirely. The debut lane in native.yml
#                               sets it so the fallback branch is itself exercised on every full
#                               run — a fallback nothing takes is a fallback nobody has tested.
#   TEKO_SEEDS_DIR              where the committed seeds live (default: bootstrap/seeds).
#
# Requires: curl or wget, tar or unzip. Extracts into ./.seed and appends it to
# $GITHUB_PATH so subsequent steps call `teko` directly.
set -eu

LABEL="${1:?usage: ci_provision_teko.sh <LABEL>}"
REPO="${TEKO_SEED_REPO:-teko-org/teko-lang}"
REPO_URL="https://github.com/${REPO}"
SEEDS_DIR="${TEKO_SEEDS_DIR:-bootstrap/seeds}"

log() { printf '%s\n' "ci_provision_teko: $*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

# download_ok URL OUT — fetch URL to OUT, returning non-zero (not aborting) when it is
# missing, exactly like install.sh's probe helper. curl preferred, wget fallback.
download_ok() {
  url="$1"; out="$2"
  if have curl; then
    curl -fsSL "$url" -o "$out" 2>/dev/null
  elif have wget; then
    wget -qO "$out" "$url" 2>/dev/null
  else
    log "no downloader found (need curl or wget)"; exit 1
  fi
}

# api_get URL OUT — like download_ok, but adds the optional GH_TOKEN Authorization
# header when present (rate-limit relief on shared runner IPs; NEVER required — a
# public repo's releases list is readable without it).
api_get() {
  url="$1"; out="$2"
  if have curl; then
    if [ -n "${GH_TOKEN:-}" ]; then
      curl -fsSL -H "Authorization: Bearer ${GH_TOKEN}" -H "Accept: application/vnd.github+json" "$url" -o "$out" 2>/dev/null
    else
      curl -fsSL -H "Accept: application/vnd.github+json" "$url" -o "$out" 2>/dev/null
    fi
  elif have wget; then
    if [ -n "${GH_TOKEN:-}" ]; then
      wget -qO "$out" --header="Authorization: Bearer ${GH_TOKEN}" "$url" 2>/dev/null
    else
      wget -qO "$out" "$url" 2>/dev/null
    fi
  else
    log "no downloader found (need curl or wget)"; exit 1
  fi
}

# sha256_of FILE — portable digest (sha256sum on Linux, shasum -a 256 on macOS),
# mirroring install.sh. Empty output when neither tool exists (verification skipped).
sha256_of() {
  f="$1"
  if have sha256sum; then
    sha256sum "$f" | awk '{print $1}'
  elif have shasum; then
    shasum -a 256 "$f" | awk '{print $1}'
  else
    printf '%s' ""
  fi
}

# host_seed_label LABEL — echoes the committed-seed label for a release asset LABEL, or empty
# when no host seed exists for it. The two axes are NOT the same: a release LABEL names a
# PUBLISHED TARGET (six Linux ones, glibc x musl x three arches), a seed names a HOST THAT RUNS
# THE COMPILER. musl and riscv64 are cross-compiled outputs with no runner behind them, so a
# request for one maps onto the host that actually executes there (x86_64/arm64) or onto nothing
# (riscv64 — that lane builds gen1 natively on x86_64 instead; see native.yml's riscv64-qemu).
host_seed_label() {
  case "$1" in
    linux-x86_64|linux-x86_64-*)     printf '%s' "linux-x86_64" ;;
    linux-arm64|linux-arm64-*)       printf '%s' "linux-arm64" ;;
    macos-arm64)                     printf '%s' "macos-arm64" ;;
    windows-x86_64)                  printf '%s' "windows-x86_64" ;;
    windows-arm64)                   printf '%s' "windows-arm64" ;;
    *)                               printf '%s' "" ;;
  esac
}

# unxz IN OUT — decompress IN to OUT. `xz` is NOT guaranteed on every runner (Git-for-Windows'
# bundled MSYS tools do not ship it), so this walks a chain of decompressors that between them
# cover every host in the matrix; python3 is present on all GitHub-hosted images and 7z on the
# Windows ones. Fails loud, naming the tools it looked for, rather than leaving a truncated file.
unxz() {
  in="$1"; out="$2"
  if have xz; then
    xz -dc "$in" > "$out" && return 0
  fi
  for py in python3 python; do
    if have "$py"; then
      "$py" -c 'import lzma,sys
with lzma.open(sys.argv[1]) as f, open(sys.argv[2], "wb") as g:
    g.write(f.read())' "$in" "$out" && return 0
    fi
  done
  if have 7z; then
    7z x -so "$in" > "$out" && return 0
  fi
  log "no xz decompressor available (looked for xz, python3, python, 7z)"
  return 1
}

# seed_from_committed LABEL — provision from bootstrap/seeds/, the transitional committed rung.
# ORDER MATTERS: the sha256 is checked against SEEDS.sha256 BEFORE the archive is decompressed,
# so a blob that does not match is never expanded, never made executable, and never runs.
# A missing sha256 tool is a REFUSAL here, not a skip: unlike the release path (which is served
# over HTTPS from a known host), this blob's only integrity claim IS the manifest.
seed_from_committed() {
  seed_label="$(host_seed_label "$1")"
  if [ -z "$seed_label" ]; then
    log "no committed host seed exists for '$1' (musl/riscv64 are cross-compiled targets, not hosts)"
    return 1
  fi
  sums="$SEEDS_DIR/SEEDS.sha256"
  [ -f "$sums" ] || { log "no committed seed manifest at $sums"; return 1; }

  archive="$SEEDS_DIR/teko-$seed_label.xz"
  exe=""
  if [ ! -f "$archive" ]; then
    archive="$SEEDS_DIR/teko-$seed_label.exe.xz"
    exe=".exe"
  fi
  [ -f "$archive" ] || { log "no committed seed archive for '$seed_label' under $SEEDS_DIR"; return 1; }

  want="$(awk -v n="$(basename "$archive")" '$2 == n || $2 == "*" n { print $1 }' "$sums" | head -n1)"
  [ -n "$want" ] || { log "$archive is absent from $sums — refusing an unlisted seed"; return 1; }
  digest="$(sha256_of "$archive")"
  if [ -z "$digest" ]; then
    log "no sha256 tool on this host — refusing an unverifiable committed seed"; return 1
  fi
  if [ "$want" != "$digest" ]; then
    log "$archive sha256 mismatch (manifest says $want, file is $digest) — refusing it"; return 1
  fi

  rm -rf .seed; mkdir -p .seed
  bin=".seed/teko$exe"
  unxz "$archive" "$bin" || { rm -rf .seed; return 1; }
  chmod +x "$bin"
  ver="$("$bin" --version 2>/dev/null || echo '')"
  if [ -z "$ver" ]; then
    log "the committed seed for '$seed_label' did not run on this host"; rm -rf .seed; return 1
  fi
  # NO version-vs-tag sanity here, and that is not an oversight: a committed seed has no tag to
  # be compared against. It is cut from the train's own tip, so it reports whatever teko.tkp said
  # when the wagon that committed it ran — one bump behind, by construction.
  seed_dir="$(CDPATH='' cd -- .seed && pwd)"
  [ -n "${GITHUB_PATH:-}" ] && printf '%s\n' "$seed_dir" >> "$GITHUB_PATH"
  log "committed seed '$seed_label' verified and ready at $seed_dir (version $ver)"
  return 0
}

if [ "${TEKO_SEED_PREFER_COMMITTED:-0}" = "1" ]; then
  log "TEKO_SEED_PREFER_COMMITTED=1 — taking the committed seed for '$LABEL', no release probe"
  if seed_from_committed "$LABEL"; then
    exit 0
  fi
  log "the committed seed was demanded but is not usable for '$LABEL'"
  exit 1
fi

# Candidate releases, NEWEST-VERSION FIRST (the /releases API is NOT version-ordered so
# `[0]` can be stale — filter to MAJOR.MINOR.PATCH.BUILD + reverse sort -V). Same tag
# discovery shape as install.sh's latest_tag() fallback, over the CANONICAL repo.
rels_json=".seed-releases.json"
TAGS=""
if api_get "https://api.github.com/repos/${REPO}/releases?per_page=100" "$rels_json"; then
  TAGS="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$rels_json" \
    | grep -E '^v?[0-9]+([.][0-9]+){3}' \
    | awk '{ ver=$0; sub(/^v/,"",ver); print ver"\t"$0 }' | sort -rV | cut -f2)"
else
  log "cannot list releases for $REPO (network/API error)"
fi
rm -f "$rels_json"
# An empty candidate list is no longer terminal: the committed seed below is exactly the rung
# that exists for a host the published releases cannot serve.
if [ -z "$TAGS" ]; then
  log "no published release candidate for $REPO — going straight to the committed seed"
fi

# SEED = the NEWEST usable released seed, ALWAYS. Features are ADDITIVE, so the freshest
# compiler builds any older-or-equal source; there is no reason to pin an older one.
# `TAGS` is already newest-version-first, and the `seed_from_tag` loop below skips a tag
# whose asset is missing (an in-progress release building ITSELF), whose checksum does
# not verify, or whose binary miscompiled (version-sanity), falling to the next — so
# "newest first" self-heals the self-build case without a channel split.
log "newest-first seed from $REPO (label '$LABEL')"

seed_from_tag() {
  tag="$1"
  base="${REPO_URL}/releases/download/${tag}"
  # Candidate asset labels: exact; then the glibc-suffixed form (a bare linux-<arch> maps
  # to the glibc default); then the unsuffixed legacy form (pre-split releases).
  cands="$LABEL"
  case "$LABEL" in
    linux-*-glibc) cands="$cands ${LABEL%-glibc}" ;;
    linux-*-musl)  : ;;
    linux-*)       cands="$cands ${LABEL}-glibc" ;;
  esac
  rm -f teko-*.tar.gz teko-*.zip SHA256SUMS.txt
  archive=""
  for c in $cands; do
    for ext in tar.gz zip; do
      if download_ok "${base}/teko-${c}.${ext}" "teko-${c}.${ext}"; then
        archive="teko-${c}.${ext}"; break 2
      fi
    done
  done
  [ -n "$archive" ] || { log "$tag has no asset for '$LABEL' (tried: $cands) — trying older"; return 1; }

  # CHECKSUM (same policy as install.sh): verify against the release's SHA256SUMS.txt.
  # A release without sums, or with a mismatching sum, is not a usable seed — fall to the
  # next. (Skipped only when the runner has no sha256 tool at all.)
  digest="$(sha256_of "$archive")"
  if [ -n "$digest" ]; then
    if ! download_ok "${base}/SHA256SUMS.txt" "SHA256SUMS.txt"; then
      log "$tag has no SHA256SUMS.txt — refusing an unverified seed, trying older"; return 1
    fi
    want="$(grep "$archive" "SHA256SUMS.txt" | awk '{print $1}' | head -n1)"
    if [ -z "$want" ] || [ "$want" != "$digest" ]; then
      log "$tag sha256 mismatch for $archive (expected '${want:-<none>}', got $digest) — trying older"; return 1
    fi
  fi

  rm -rf .seed; mkdir -p .seed
  case "$archive" in
    *.zip) unzip -o "$archive" -d .seed >/dev/null ;;
    *)     tar -xzf "$archive" -C .seed ;;
  esac
  chmod +x .seed/teko .seed/teko.exe 2>/dev/null || true
  bin=".seed/teko"; [ -f .seed/teko.exe ] && bin=".seed/teko.exe"
  # VERSION SANITY: the seed must report EXACTLY its own tag's version number.
  #
  # The comparison is ANCHORED (string equality of the number), not a substring test. The old
  # `case "$ver" in *"$expectnum"*)` accepted any report CONTAINING the tag's number, so tag
  # v0.3.1.4 accepted a binary reporting 0.3.1.40 — and with the fourth field now a build counter,
  # prefix collision is routine, not hypothetical.
  #
  # The rejection message states only what was OBSERVED. The previous one asserted "broken build",
  # a cause nothing here verified: a version mismatch is equally consistent with a perfectly good
  # binary published under a mislabelled tag, or with a binary that did not run at all. Naming an
  # unverified cause in a log is the same M.3 failure as naming one in a diagnostic.
  expectnum="${tag#v}"; expectnum="${expectnum%%-*}"
  ver="$("$bin" --version 2>/dev/null || echo '')"
  vernum="${ver##* }"          # "teko 0.3.1.4-beta" -> "0.3.1.4-beta"
  vernum="${vernum%%-*}"       # -> "0.3.1.4"
  if [ -z "$ver" ]; then
    log "$tag seed did not report a version (the downloaded binary did not run here) — trying older"
    return 1
  fi
  if [ "$vernum" != "$expectnum" ]; then
    log "$tag version mismatch: the tag names $expectnum, the downloaded binary reports '$ver' — trying older"
    return 1
  fi
  seed_dir="$(CDPATH='' cd -- .seed && pwd)"
  [ -n "${GITHUB_PATH:-}" ] && printf '%s\n' "$seed_dir" >> "$GITHUB_PATH"
  log "teko $tag ready at $seed_dir (version $ver)"
  return 0
}

for TAG in $TAGS; do
  if seed_from_tag "$TAG"; then
    exit 0
  fi
done
log "no usable seed found for '$LABEL' in any release of $REPO — trying the committed seed"
if seed_from_committed "$LABEL"; then
  exit 0
fi
log "no usable seed for '$LABEL': neither a release of $REPO nor $SEEDS_DIR could serve one"
exit 1
