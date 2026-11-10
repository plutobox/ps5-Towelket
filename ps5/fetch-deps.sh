#!/usr/bin/env bash
# Fetch the sources the PS5 build compiles itself, because PacBrew has no
# package for them: liblcf (RPG Maker data), pixman (the Player's software
# renderer) and inih (liblcf's INI reader). They go in ps5/deps/ (ignored by
# git); pins below.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
mkdir -p deps
cd deps

LIBLCF_REF=6854310   # EasyRPG/liblcf master; the Player needs >= 0.8.1
PIXMAN_VERSION=0.43.4
INIH_TAG=r58

if [[ ! -d liblcf ]]; then
    git clone https://github.com/EasyRPG/liblcf.git liblcf
    git -C liblcf checkout "$LIBLCF_REF"
fi
if [[ ! -d pixman ]]; then
    curl -fsSL "https://cairographics.org/releases/pixman-$PIXMAN_VERSION.tar.gz" | tar xz
    mv "pixman-$PIXMAN_VERSION" pixman
fi
if [[ ! -d inih ]]; then
    git clone --depth 1 --branch "$INIH_TAG" https://github.com/benhoyt/inih.git inih
fi
echo "deps ready in $PWD"
