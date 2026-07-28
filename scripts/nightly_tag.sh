#!/usr/bin/env sh
# scripts/nightly_tag.sh — THE ONE PLACE THAT SPELLS THE NIGHTLY TAG.
#
# ── WHY A SCRIPT FOR A STRING ─────────────────────────────────────────────────────────────────
# Two workflows have to agree on this name EXACTLY: `nightly.yml` creates it, `release.yml` looks
# it up by exact name in order to promote it. There is no "closest match" and no "newest
# prerelease" — that was the point of making the lookup exact. The failure mode of two hand-copied
# derivations is therefore not a warning or a red lane: the release simply reports "no nightly to
# promote" and stops, and nobody finds out until a release is being cut. One character of drift
# costs a release. So the string is derived HERE and both sides call this.
#
# ── THE SHAPE, AND WHY EACH HALF IS IN IT (owner ruling, quoted in release.yml) ───────────────
#   "ao publicar o artefato, publica como '*-nightly', o * troca-se para o número do BUILD que
#    gerou o artefato e não da versão"
#
#   v<A.B.C.D>-nightly_<sha8>
#
#   <A.B.C.D>  teko.tkp's `version`, VERBATIM and WITHOUT its prerelease suffix. The 4th field IS
#              the build number (scripts/derive_version.sh, owner ruling 2026-07-04), which is
#              precisely what the ruling asks the `*` to become. The suffix is stripped because a
#              nightly is cut from a COMMIT, while `suffix` is a release-time decision that can
#              change after the bytes exist — keying the nightly on it would make the same build
#              unfindable the moment someone edited `suffix`.
#   <sha8>     the first 8 hex characters of the commit the artifacts were built from.
#
# BOTH HALVES ARE KNOWABLE FROM THE TAG BEING RELEASED, which is what turns the promotion into a
# lookup by exact name instead of a search. And they are what makes the DELETE safe: two nightlies
# can only coexist if they came from different commits, in which case they have different names,
# and the one deleted is by construction the one just downloaded.
#
# Usage:   nightly_tag.sh [<sha>]
#          <sha> defaults to $GITHUB_SHA. Fewer than 8 characters is refused rather than padded:
#          a truncated name is a name that will not be found later.
#
# POSIX sh only. Prints the tag on stdout and nothing else.
set -eu

SHA="${1:-${GITHUB_SHA:-}}"
if [ -z "$SHA" ]; then
    echo "nightly_tag: no commit sha given and \$GITHUB_SHA is unset" >&2
    exit 1
fi

SHA8="$(printf '%s' "$SHA" | cut -c1-8)"
if [ "${#SHA8}" -ne 8 ]; then
    echo "nightly_tag: '$SHA' is shorter than 8 characters — refusing to mint a truncated tag" >&2
    exit 1
fi

TAG="$(sh scripts/derive_version.sh)"     # v<A.B.C.D>[-<suffix>]
NUM="${TAG#v}"                            #  <A.B.C.D>[-<suffix>]
NUM="${NUM%%-*}"                          #  <A.B.C.D>

case "$NUM" in
    *.*.*.*) ;;
    *) echo "nightly_tag: '$NUM' is not an A.B.C.D version — teko.tkp is malformed" >&2; exit 1 ;;
esac

printf 'v%s-nightly_%s\n' "$NUM" "$SHA8"
