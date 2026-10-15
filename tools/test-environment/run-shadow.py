#!/usr/bin/env python3
"""Compile the selected tree's full shadow shader and run real Vulkan fixtures.

These are synthetic geometry/light cases, not a screenshot or world-build test.
Pass --software for deterministic CPU Vulkan; omit it to use the host driver.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from support import tool_environment

REPO = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "test-environment")
    parser.add_argument("--software", action="store_true")
    args = parser.parse_args()
    run = args.root.resolve() / "graphics" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    run.mkdir(parents=True)
    env, sdk = tool_environment(REPO, run / "runtime", args.software)
    shader = REPO / "code/renderervk/shaders/actorshadow.tmpl"
    source = shader.read_text()
    changes = {
        "layout(location = 0) in vec2 frag_tex_coord;": "layout(local_size_x=8,local_size_y=8) in;\nlayout(set=0,binding=4,std430) buffer Results { vec4 results[]; };",
        "layout(location = 0) out vec4 out_color;": "vec4 out_color;",
        "gl_FragCoord.xy": "(vec2(gl_GlobalInvocationID.xy)+vec2(0.5))",
        "void main() {": "void shadowMain() {",
    }
    for old, new in changes.items():
        if old not in source: raise RuntimeError(f"Shader interface changed; update fixture wrapper: {old}")
        source = source.replace(old, new)
    source += "\nvoid main(){ if(any(greaterThanEqual(ivec2(gl_GlobalInvocationID.xy),depthSize())))return; shadowMain(); results[gl_GlobalInvocationID.y*uint(depthSize().x)+gl_GlobalInvocationID.x]=out_color;}\n"
    compute = run / "actorshadow-test.comp"; compute.write_text(source)
    compiler = ["c++", "-std=c++17", "-O2", str(Path(__file__).with_name("shadow-fixture.cpp")), "-o", str(run / "shadow-fixture")]
    if sdk: compiler.extend(("-I" + str(sdk / "include"), "-L" + str(sdk / "lib/x86_64-linux-gnu")))
    compiler.append("-lvulkan")
    commands = [compiler, ["glslangValidator", "-V", "--target-env", "vulkan1.2", str(compute), "-o", str(run / "actorshadow-test.spv")],
                ["spirv-val", "--target-env", "vulkan1.2", str(run / "actorshadow-test.spv")],
                [str(run / "shadow-fixture"), str(run / "actorshadow-test.spv")]]
    error = None
    print(f"Compiling current shader and running Vulkan fixtures: {run}", flush=True)
    with (run / "validation.log").open("w") as log:
        for command in commands:
            try: subprocess.run(command, cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            except (OSError, subprocess.SubprocessError) as failure:
                error = str(failure); break
    body = (run / "validation.log").read_text(errors="replace")
    if not error and "Vulkan validation errors: 0" not in body: error = "Missing zero-error validation summary"
    result = {"status": "FAIL" if error else "PASS", "error": error, "software_requested": args.software,
              "shader_sha256": hashlib.sha256(shader.read_bytes()).hexdigest(), "commands": commands,
              "scope": "full shader on synthetic geometry; excludes world submission, model ownership and hardware fast updates"}
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(body, end="", flush=True)
    print(f"{result['status']}: {run}", flush=True)
    return bool(error)


if __name__ == "__main__":
    raise SystemExit(main())
