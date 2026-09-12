#!/bin/sh
# compile.sh - build spirv/shader_data.c from the GLSL in this directory.
#
# A port of compile.bat, which needs Windows and %VULKAN_SDK% and so cannot run
# on the machine that actually builds the releases. Both are kept: the .bat for
# anyone working from a Windows checkout, this for everywhere else. They must
# stay in step - a shader added to one and not the other is a shader that
# silently does not exist in half the builds.
#
# Nothing here runs during an ordinary build. The Makefile only invokes it when
# a .vert, .frag or .tmpl is newer than spirv/shader_data.c, so a checkout that
# has not touched a shader uses the file committed to the repo and never needs
# glslang at all.
#
#   ./compile.sh                 regenerate if glslang is present
#   GLSLANG=/path/to/it ./compile.sh
#
# Requires glslangValidator (apt: glslang-tools, or the Vulkan SDK) and a C
# compiler for bin2hex.

set -e

cd "$(dirname "$0")"

GLSLANG=${GLSLANG:-glslangValidator}
if ! command -v "$GLSLANG" >/dev/null 2>&1; then
    echo "compile.sh: $GLSLANG not found - install glslang-tools or set GLSLANG" >&2
    exit 1
fi

CC=${CC:-cc}
BH=./bin2hex
TMP=spirv/data.spv
OUT=spirv/shader_data.c

mkdir -p spirv

# bin2hex is a four-line C program; building it here keeps the whole thing to
# one command rather than a documented prerequisite nobody reads.
"$CC" -O2 -o "$BH" bin2hex.c

rm -f "$OUT" "$TMP"

# emit <stage> <array-name> <source> [defines...]
emit() {
    stage=$1
    name=$2
    src=$3
    shift 3
    "$GLSLANG" -S "$stage" -V -o "$TMP" "$src" "$@" >/dev/null
    "$BH" "$TMP" "+$OUT" "$name"
    rm -f "$TMP"
}

# ---------------------------------------------------------------------------
# Ray-query shaders need SPIR-V 1.4, and glslang will not tell you otherwise
# ---------------------------------------------------------------------------
#
# Compile a GL_EXT_ray_query shader without --target-env and glslang emits a
# SPIR-V *1.0* module containing ray-query opcodes, exits 0, and prints nothing.
# The result is a module no driver will accept, produced by a build that
# reported success - the same silent-wrong-artifact shape as a cvar nothing
# reads or a shader name the pak does not contain.
#
# So these are emitted separately from the *.frag loop below (which uses the
# default target) and the version word in the output is checked. A module that
# comes back as anything but 1.4 fails the build here rather than at
# vkCreateShaderModule on somebody's machine.
#
# emit_rt <stage> <array-name> <source> [defines...]
emit_rt() {
    stage=$1
    name=$2
    src=$3
    shift 3
    "$GLSLANG" -S "$stage" -V --target-env spirv1.4 -o "$TMP" "$src" "$@" >/dev/null

    # bytes 4-7 of a SPIR-V module are the version: 0x00 major minor 0x00
    ver=$(od -An -tx1 -j 4 -N 4 "$TMP" | tr -d ' \n')
    if [ "$ver" != "00040100" ]; then
        echo "compile.sh: $src produced SPIR-V version bytes $ver, expected 00040100 (1.4)." >&2
        echo "compile.sh: a ray-query shader in a pre-1.4 module will be rejected by the" >&2
        echo "compile.sh: driver. Check that $GLSLANG supports --target-env spirv1.4." >&2
        rm -f "$TMP" "$BH"
        exit 1
    fi

    "$BH" "$TMP" "+$OUT" "$name"
    rm -f "$TMP"
}

# ---------------------------------------------------------------------------
# individual shaders - name follows the file, as in the .bat: color.vert
# becomes color_vert_spv
# ---------------------------------------------------------------------------

for f in *.vert; do
    emit vert "$(basename "$f" .vert)_vert_spv" "$f"
done

for f in *.frag; do
    emit frag "$(basename "$f" .frag)_frag_spv" "$f"
done

# ---------------------------------------------------------------------------
# ray query (R13)
# ---------------------------------------------------------------------------
#
# rtao is a .tmpl rather than a .frag on purpose, and not only because it has
# variants: the loop above walks *.frag with the default SPIR-V target, so a
# ray-query shader named .frag would be picked up by it and silently emitted as
# a 1.0 module - the very thing emit_rt exists to prevent. The extension is what
# keeps it out of that loop, with no special case to forget.
#
# Two variants because the depth attachment is multisampled when
# r_ext_multisample is on, and sampler2D cannot read a multisampled image.

emit_rt frag rtao_frag_spv    rtao.tmpl
emit_rt frag rtao_frag_ms_spv rtao.tmpl -DUSE_MSAA

# The denoiser fires no rays - it reads the target the trace wrote - so it goes
# through the ordinary emit and the default SPIR-V target. Deliberately not
# emit_rt: asking for 1.4 where 1.0 will do costs nothing here but would put a
# version requirement on a module that has no reason to carry one.
#
# Still a .tmpl, for the same two variants: it samples depth to decide which
# neighbours belong to the same surface, and depth is multisampled when
# r_ext_multisample is on.

emit frag rtao_blur_frag_spv    rtao_blur.tmpl
emit frag rtao_blur_frag_ms_spv rtao_blur.tmpl -DUSE_MSAA

# ---------------------------------------------------------------------------
# lighting variations from templates
# ---------------------------------------------------------------------------

emit vert vert_light          light_vert.tmpl
emit vert vert_light_fog      light_vert.tmpl -DUSE_FOG
emit frag frag_light          light_frag.tmpl
emit frag frag_light_fog      light_frag.tmpl -DUSE_FOG
emit frag frag_light_line     light_frag.tmpl -DUSE_LINE
emit frag frag_light_line_fog light_frag.tmpl -DUSE_LINE -DUSE_FOG

# ---------------------------------------------------------------------------
# generic vertex variations
# ---------------------------------------------------------------------------

# single-texture
emit vert vert_tx0                 gen_vert.tmpl
emit vert vert_tx0_fog             gen_vert.tmpl -DUSE_FOG
emit vert vert_tx0_env             gen_vert.tmpl -DUSE_ENV
emit vert vert_tx0_env_fog         gen_vert.tmpl -DUSE_FOG -DUSE_ENV

# single-texture, identity (1.0) colors
emit vert vert_tx0_ident1          gen_vert.tmpl -DUSE_CLX_IDENT
emit vert vert_tx0_ident1_fog      gen_vert.tmpl -DUSE_CLX_IDENT -DUSE_FOG
emit vert vert_tx0_ident1_env      gen_vert.tmpl -DUSE_CLX_IDENT -DUSE_ENV
emit vert vert_tx0_ident1_env_fog  gen_vert.tmpl -DUSE_CLX_IDENT -DUSE_FOG -DUSE_ENV

# single-texture, fixed (rgb+a) colors
emit vert vert_tx0_fixed           gen_vert.tmpl -DUSE_FIXED_COLOR
emit vert vert_tx0_fixed_fog       gen_vert.tmpl -DUSE_FIXED_COLOR -DUSE_FOG
emit vert vert_tx0_fixed_env       gen_vert.tmpl -DUSE_FIXED_COLOR -DUSE_ENV
emit vert vert_tx0_fixed_env_fog   gen_vert.tmpl -DUSE_FIXED_COLOR -DUSE_FOG -DUSE_ENV

# double-texture
emit vert vert_tx1                 gen_vert.tmpl -DUSE_TX1
emit vert vert_tx1_fog             gen_vert.tmpl -DUSE_TX1 -DUSE_FOG
emit vert vert_tx1_env             gen_vert.tmpl -DUSE_TX1 -DUSE_ENV
emit vert vert_tx1_env_fog         gen_vert.tmpl -DUSE_TX1 -DUSE_FOG -DUSE_ENV

# double-texture, identity (1.0) colors
emit vert vert_tx1_ident1          gen_vert.tmpl -DUSE_CLX_IDENT -DUSE_TX1
emit vert vert_tx1_ident1_fog      gen_vert.tmpl -DUSE_CLX_IDENT -DUSE_TX1 -DUSE_FOG
emit vert vert_tx1_ident1_env      gen_vert.tmpl -DUSE_CLX_IDENT -DUSE_TX1 -DUSE_ENV
emit vert vert_tx1_ident1_env_fog  gen_vert.tmpl -DUSE_CLX_IDENT -DUSE_TX1 -DUSE_FOG -DUSE_ENV

# double-texture, fixed (rgb+a) colors
emit vert vert_tx1_fixed           gen_vert.tmpl -DUSE_FIXED_COLOR -DUSE_TX1
emit vert vert_tx1_fixed_fog       gen_vert.tmpl -DUSE_FIXED_COLOR -DUSE_TX1 -DUSE_FOG
emit vert vert_tx1_fixed_env       gen_vert.tmpl -DUSE_FIXED_COLOR -DUSE_TX1 -DUSE_ENV
emit vert vert_tx1_fixed_env_fog   gen_vert.tmpl -DUSE_FIXED_COLOR -DUSE_TX1 -DUSE_FOG -DUSE_ENV

# double-texture, non-identical colors
emit vert vert_tx1_cl              gen_vert.tmpl -DUSE_CL1 -DUSE_TX1
emit vert vert_tx1_cl_fog          gen_vert.tmpl -DUSE_CL1 -DUSE_TX1 -DUSE_FOG
emit vert vert_tx1_cl_env          gen_vert.tmpl -DUSE_CL1 -DUSE_TX1 -DUSE_ENV
emit vert vert_tx1_cl_env_fog      gen_vert.tmpl -DUSE_CL1 -DUSE_TX1 -DUSE_ENV -DUSE_FOG

# triple-texture
emit vert vert_tx2                 gen_vert.tmpl -DUSE_TX2
emit vert vert_tx2_fog             gen_vert.tmpl -DUSE_TX2 -DUSE_FOG
emit vert vert_tx2_env             gen_vert.tmpl -DUSE_TX2 -DUSE_ENV
emit vert vert_tx2_env_fog         gen_vert.tmpl -DUSE_TX2 -DUSE_ENV -DUSE_FOG

# triple-texture, non-identical colors
emit vert vert_tx2_cl              gen_vert.tmpl -DUSE_CL2 -DUSE_TX2
emit vert vert_tx2_cl_fog          gen_vert.tmpl -DUSE_CL2 -DUSE_TX2 -DUSE_FOG
emit vert vert_tx2_cl_env          gen_vert.tmpl -DUSE_CL2 -DUSE_TX2 -DUSE_ENV
emit vert vert_tx2_cl_env_fog      gen_vert.tmpl -DUSE_CL2 -DUSE_TX2 -DUSE_ENV -DUSE_FOG

# ---------------------------------------------------------------------------
# generic fragment variations
# ---------------------------------------------------------------------------

# single-texture, generic
emit frag frag_tx0                 gen_frag.tmpl -DUSE_ATEST
emit frag frag_tx0_fog             gen_frag.tmpl -DUSE_ATEST -DUSE_FOG

# single-texture, identity (1.0) color
emit frag frag_tx0_ident1          gen_frag.tmpl -DUSE_CLX_IDENT -DUSE_ATEST
emit frag frag_tx0_ident1_fog      gen_frag.tmpl -DUSE_CLX_IDENT -DUSE_ATEST -DUSE_FOG

# single-texture, fixed (rgb+a) color
emit frag frag_tx0_fixed           gen_frag.tmpl -DUSE_FIXED_COLOR -DUSE_ATEST
emit frag frag_tx0_fixed_fog       gen_frag.tmpl -DUSE_FIXED_COLOR -DUSE_ATEST -DUSE_FOG

# single-texture, entity color
emit frag frag_tx0_ent             gen_frag.tmpl -DUSE_ENT_COLOR -DUSE_ATEST
emit frag frag_tx0_ent_fog         gen_frag.tmpl -DUSE_ENT_COLOR -DUSE_ATEST -DUSE_FOG

# single-texture, depth-fragment
emit frag frag_tx0_df              gen_frag.tmpl -DUSE_CLX_IDENT -DUSE_ATEST -DUSE_DF

# double-texture
emit frag frag_tx1                 gen_frag.tmpl -DUSE_TX1
emit frag frag_tx1_fog             gen_frag.tmpl -DUSE_TX1 -DUSE_FOG

# double-texture, identity (1.0) colors
emit frag frag_tx1_ident1          gen_frag.tmpl -DUSE_CLX_IDENT -DUSE_TX1
emit frag frag_tx1_ident1_fog      gen_frag.tmpl -DUSE_CLX_IDENT -DUSE_TX1 -DUSE_FOG

# double-texture, fixed (rgb+a) colors
emit frag frag_tx1_fixed           gen_frag.tmpl -DUSE_FIXED_COLOR -DUSE_TX1
emit frag frag_tx1_fixed_fog       gen_frag.tmpl -DUSE_FIXED_COLOR -DUSE_TX1 -DUSE_FOG

# double-texture, entity colors: commented out in compile.bat, kept commented
# here so the two files still read as the same list
# emit frag frag_tx1_ent           gen_frag.tmpl -DUSE_ENT_COLOR -DUSE_TX1
# emit frag frag_tx1_ent_fog       gen_frag.tmpl -DUSE_ENT_COLOR -DUSE_TX1 -DUSE_FOG

# double-texture, non-identical colors
emit frag frag_tx1_cl              gen_frag.tmpl -DUSE_CL1 -DUSE_TX1
emit frag frag_tx1_cl_fog          gen_frag.tmpl -DUSE_CL1 -DUSE_TX1 -DUSE_FOG

# triple-texture
emit frag frag_tx2                 gen_frag.tmpl -DUSE_TX2
emit frag frag_tx2_fog             gen_frag.tmpl -DUSE_TX2 -DUSE_FOG

# triple-texture, non-identical colors
emit frag frag_tx2_cl              gen_frag.tmpl -DUSE_CL2 -DUSE_TX2
emit frag frag_tx2_cl_fog          gen_frag.tmpl -DUSE_CL2 -DUSE_TX2 -DUSE_FOG

rm -f "$TMP" "$BH"

echo "compile.sh: $(grep -c 'const unsigned char' "$OUT") shaders -> $OUT"
