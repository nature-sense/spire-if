#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 NatureSense
#
# Put LVGL where `components/lvgl/CMakeLists.txt` looks for it, and move the pin.
#
#   tools/vendor-lvgl.sh              # check the submodule out — what a fresh clone needs
#   tools/vendor-lvgl.sh --status     # the pinned commit — a leading `-` means "not checked out yet"
#   tools/vendor-lvgl.sh v9.6.1       # move the pin to a tag, and stage the new gitlink
#
# LVGL is a **git submodule** at `components/lvgl/upstream`, so the pin is the commit git records for
# it: one place, and one that cannot drift from the source that is actually compiled. There is no
# version constant in the component's CMakeLists to disagree with it.
#
# This script is only the convenience around that, because the two things people get wrong are a single
# git command each: a fresh clone needs `--init`, and moving the pin is a fetch that misses the tag, or
# a checkout that leaves the superproject's gitlink pointing at the old commit.
#
# A checkout that never runs this still **builds**. The component registers nothing until `upstream/`
# is populated, so a missing submodule costs a display, not a failed build.

set -euo pipefail

cd "$(cd "$(dirname "$0")/.." && pwd)"

path="components/lvgl/upstream"

[ -f components/lvgl/CMakeLists.txt ] \
    || { echo "no components/lvgl — run this from the spire-idf repository" >&2; exit 1; }

case "${1:-}" in
    "")
        # A fresh clone: fetch the pinned commit, and no more history than that.
        git submodule update --init --depth 1 -- "$path"
        ;;
    --status|-s)
        git submodule status -- "$path"
        exit 0
        ;;
    -h|--help)
        awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "$0"
        exit 0
        ;;
    -*)
        echo "usage: $(basename "$0") [<tag>|--status]" >&2
        exit 2
        ;;
    *)
        ref="$1"
        # Move the pin. The submodule has to exist before it can be stood on something.
        git submodule update --init -- "$path"
        # `--depth 1`: the tag, and none of the history between it and the old pin.
        git -C "$path" fetch --depth 1 origin "refs/tags/${ref}:refs/tags/${ref}"
        git -C "$path" checkout --detach "$ref"
        # The pin *is* the gitlink, and it is only real once the new one is staged.
        git add -- "$path"
        echo "staged — commit components/lvgl/upstream to make the new pin real."
        ;;
esac

git submodule status -- "$path"
