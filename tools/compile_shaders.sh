#!/bin/sh
# Compiles GLSL shaders to SPIR-V into app/src/main/assets/shaders (needs the NDK's glslc).
set -e
D="$(cd "$(dirname "$0")/.." && pwd)/app/src/main/assets/shaders"
GLSLC="${GLSLC:-$HOME/Android/Sdk/ndk/27.0.12077973/shader-tools/linux-x86_64/glslc}"
cd "$D"
for f in *.vert *.frag; do
  "$GLSLC" --target-env=vulkan1.1 -O -I. "$f" -o "$f.spv"
done
echo "shaders ok"
