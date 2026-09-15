#!/usr/bin/env bash
#
# Clones the openFrameworks addons patternForge needs, at the commits it
# is known to build against, into this openFrameworks tree's addons/.
#
# There is only one addon -- but it is NOT the upstream one, which is the
# whole reason this script exists. patternForge builds against a fork of
# ofxImGui whose HEAD is one commit ahead of jvcleave/master, and that
# commit does not exist upstream. Clone upstream and it does not compile.
# So the URL and the commit are both pinned here.
#
# Safe to re-run: an addon that is already present and on the right
# commit is left alone.
#
#   ./scripts/install-addons.sh            clone or verify
#   ./scripts/install-addons.sh --update   also move an existing clone
#                                          onto the pinned commit

set -euo pipefail

# --- what we need ----------------------------------------------------
#
# name | git URL | commit to pin to
#
# HTTPS rather than SSH on purpose: this has to work on a machine with
# no keys loaded, and the fork is public.
ADDONS=(
  "ofxImGui|https://github.com/danzeeeman/ofxImGui.git|3ddb3519ba5687e3f45257330e36d73fecef80de"
)

# openFrameworks release this tree is expected to be.
OF_WANT_MAJOR=0
OF_WANT_MINOR=12

UPDATE=0
[[ "${1:-}" == "--update" ]] && UPDATE=1

bold() { printf '\033[1m%s\033[0m\n' "$*"; }
warn() { printf '\033[33m%s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

command -v git >/dev/null || die "git is not installed."

# --- find the openFrameworks root ------------------------------------
#
# config.make holds OF_ROOT relative to the project, which is the same
# answer the Makefile uses -- so read that rather than guessing a depth.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
of_rel="$(sed -nE 's/^[[:space:]]*OF_ROOT[[:space:]]*=[[:space:]]*(.+)$/\1/p' "$here/config.make" 2>/dev/null | tail -1 || true)"
[[ -z "$of_rel" ]] && of_rel="../../.."
OF_ROOT="$(cd "$here/$of_rel" 2>/dev/null && pwd || true)"

if [[ -z "$OF_ROOT" || ! -d "$OF_ROOT/libs/openFrameworks" ]]; then
  die "Could not find an openFrameworks tree at '$here/$of_rel'.
This project has to live in <openFrameworks>/apps/<group>/patternForge,
or config.make's OF_ROOT has to point at one."
fi

bold "openFrameworks: $OF_ROOT"

# Version check: a warning, not a failure. It may well build on a
# neighbouring release, and refusing to clone an addon over it would be
# unhelpful.
ofc="$OF_ROOT/libs/openFrameworks/utils/ofConstants.h"
if [[ -f "$ofc" ]]; then
  maj="$(sed -nE 's/^#define OF_VERSION_MAJOR[[:space:]]+([0-9]+).*/\1/p' "$ofc" | head -1)"
  min="$(sed -nE 's/^#define OF_VERSION_MINOR[[:space:]]+([0-9]+).*/\1/p' "$ofc" | head -1)"
  pat="$(sed -nE 's/^#define OF_VERSION_PATCH[[:space:]]+([0-9]+).*/\1/p' "$ofc" | head -1)"
  echo "  version $maj.$min.${pat:-0}"
  if [[ "$maj" != "$OF_WANT_MAJOR" || "$min" != "$OF_WANT_MINOR" ]]; then
    warn "  note: built and tested against ${OF_WANT_MAJOR}.${OF_WANT_MINOR}.x"
  fi
fi

mkdir -p "$OF_ROOT/addons"

# --- clone / verify each addon ---------------------------------------
fail=0
for entry in "${ADDONS[@]}"; do
  IFS='|' read -r name url commit <<< "$entry"
  dir="$OF_ROOT/addons/$name"
  echo
  bold "$name"

  if [[ ! -e "$dir" ]]; then
    echo "  cloning $url"
    git clone --quiet "$url" "$dir"
    git -C "$dir" checkout --quiet "$commit"
    echo "  at $(git -C "$dir" rev-parse --short HEAD)"
  elif [[ ! -d "$dir/.git" ]]; then
    warn "  $dir exists but is not a git clone -- leaving it alone."
    warn "  Expected $url @ ${commit:0:8}. Remove it and re-run to replace it."
  else
    have="$(git -C "$dir" rev-parse HEAD)"
    if [[ "$have" == "$commit" ]]; then
      echo "  already at the pinned commit ${commit:0:8}"
    elif [[ "$UPDATE" == "1" ]]; then
      echo "  fetching $url"
      git -C "$dir" fetch --quiet "$url" "$commit" 2>/dev/null \
        || git -C "$dir" fetch --quiet --all
      if git -C "$dir" cat-file -e "$commit^{commit}" 2>/dev/null; then
        if [[ -n "$(git -C "$dir" status --porcelain)" ]]; then
          warn "  has local changes -- not checking out. Commit or stash them first."
          fail=1
        else
          git -C "$dir" checkout --quiet "$commit"
          echo "  moved to ${commit:0:8}"
        fi
      else
        warn "  could not fetch $commit"
        fail=1
      fi
    else
      warn "  at ${have:0:8}, pinned is ${commit:0:8}"
      warn "  re-run with --update to move it (this project may not build otherwise)"
    fi
  fi
done

# --- check the addons the project asks for are all covered -----------
#
# addons.make is what the build actually reads. If it grows an entry this
# script does not know about, say so rather than let `make` fail later
# with a missing header.
echo
if [[ -f "$here/addons.make" ]]; then
  while read -r want; do
    [[ -z "$want" || "$want" == \#* ]] && continue
    if [[ ! -d "$OF_ROOT/addons/$want" ]]; then
      warn "addons.make asks for '$want' and it is not installed, and this"
      warn "script does not know where to get it. Add it to ADDONS above."
      fail=1
    fi
  done < <(tr -d '\r' < "$here/addons.make")
fi

# --- prove it is usable ----------------------------------------------
if [[ -f "$OF_ROOT/addons/ofxImGui/src/ofxImGui.h" \
   && -f "$OF_ROOT/addons/ofxImGui/libs/imgui/src/imgui.cpp" ]]; then
  echo "ofxImGui headers and vendored imgui sources present."
else
  warn "ofxImGui looks incomplete: expected src/ofxImGui.h and"
  warn "libs/imgui/src/imgui.cpp. imgui is vendored in-tree, not a"
  warn "submodule, so a shallow or partial clone will miss it."
  fail=1
fi

echo
if [[ "$fail" == "0" ]]; then
  bold "Done. Build with:  make -j8"
else
  die "Finished with problems -- see above."
fi
