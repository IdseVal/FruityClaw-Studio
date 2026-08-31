#!/usr/bin/env bash
#
# Everything the container needs that the image cannot bake in: project dependencies, which
# change with the lockfiles. (Skills are committed in .claude/skills/ and arrive with the
# checkout -- the v0.1.1 install step is gone on purpose.)
#
# Deliberately NOT `set -e` around the whole file. A failed optional step must not leave a
# developer with no container at all; a failed dependency install must.
set -uo pipefail

cd /workspace

# FruityClaw Studio is a C++/Qt desktop application built with MSVC on the Windows host
# (see docs/ for the CMake-preset build recipe). The Qt/MSVC toolchain cannot live in this
# Linux container, so the container carries only the workflow tooling (git, gh, jq, python,
# node) that the dispatcher, digest and agents rely on -- no project build dependencies.

echo "==> Python dependencies (workflow tooling)"
set -e
python -m pip install --quiet --upgrade pip
python -m pip install --quiet pyyaml
set +e

echo "==> Done. Project builds happen on the Windows host (Qt/MSVC); see docs/."
