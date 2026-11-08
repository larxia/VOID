#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build VOID for the M-VAVE FM-1 on fm1-base.
#   ./build.sh                    app/build/void.fwsc (needs JieLi's toolchain and the AC79 SDK: see README.md)
#   ./build.sh --release 0.1      app/build/void-0.1.fwsc, the version the install page writes
#   ./build.sh --host             the simulator: app/build/host/void_host (any Linux or macOS C compiler)
#   ./build.sh --web              the browser emulator: app/build/emu (Emscripten)
set -e
cd "$(dirname "$0")"
BASE_COMMIT=317cb86cc60d6286fa80a02743e94e6a60ff842d   # the fm1-base VOID is tested on

if [ ! -d fm1-base/.git ]; then
    git clone -q https://github.com/charlesvestal/fm1-base fm1-base
fi
if [ "$(git -C fm1-base rev-parse HEAD)" != "$BASE_COMMIT" ]; then
    git -C fm1-base fetch -q origin "$BASE_COMMIT" 2>/dev/null || git -C fm1-base fetch -q origin
    git -C fm1-base checkout -q "$BASE_COMMIT"
fi
git -C fm1-base submodule update -q --init --recursive

# On GitHub, the links in fm1.toml point at this repository and its GitHub Pages site
if [ -n "$GITHUB_REPOSITORY" ]; then
    owner=$(echo "${GITHUB_REPOSITORY%/*}" | tr 'A-Z' 'a-z')
    repo=${GITHUB_REPOSITORY#*/}
    sed -i "s#github.com/OWNER/REPO#github.com/$GITHUB_REPOSITORY#; s#OWNER.github.io/REPO#$owner.github.io/$repo#g" app/fm1.toml
fi

export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
exec python3 fm1-base/tools/build.py app "$@"
