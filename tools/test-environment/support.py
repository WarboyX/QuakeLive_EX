"""Use standard host tools, or the workspace's already-installed SDK."""
import os
from pathlib import Path


def tool_environment(repo, runtime, software=False):
    env = os.environ.copy()
    sdk = next((parent / "toolchain/usr" for parent in repo.parents
                if (parent / "toolchain/usr/bin/glslangValidator").is_file()), None)
    if sdk:
        env["PATH"] = str(sdk / "bin") + os.pathsep + env.get("PATH", "")
        lib = sdk / "lib/x86_64-linux-gnu"
        env["LD_LIBRARY_PATH"] = str(lib) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        env["VK_LAYER_PATH"] = str(sdk / "share/vulkan/explicit_layer.d")
        if software:
            env["VK_DRIVER_FILES"] = str(sdk / "share/vulkan/icd.d/lvp_icd.json")
    elif software:
        icd = next(iter(Path("/usr/share/vulkan/icd.d").glob("lvp*.json")), None)
        if not icd:
            raise RuntimeError("Install Mesa's Vulkan software driver for --software")
        env["VK_DRIVER_FILES"] = str(icd)
    runtime.mkdir(parents=True, exist_ok=True)
    runtime.chmod(0o700)
    env["XDG_RUNTIME_DIR"] = str(runtime)
    return env, sdk
