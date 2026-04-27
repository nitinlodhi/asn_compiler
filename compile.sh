#!/usr/bin/env bash
set -e

# Build everything: compiler, generated codec libraries, and dist/ package
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

echo ""
echo "Distribution package ready at: build/dist/"
echo "  build/dist/include/   — generated headers + runtime headers"
echo "  build/dist/lib/       — static codec libraries (.a)"
