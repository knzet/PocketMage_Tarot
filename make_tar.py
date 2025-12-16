Import("projenv")
import subprocess, os

app_name = "tarot"

def create_tar(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    
    bin_path = os.path.join(build_dir, f"{app_name}.bin")
    icon_path = os.path.join(project_dir, f"{app_name}_ICON.bin")
    tar_path = os.path.join(project_dir, f"{app_name}.tar")

    if not os.path.exists(bin_path):
        print("BIN not found, cannot create TAR")
        return

    cmd = ["tar", "-cf", tar_path]

    # --- Add tarot.bin and tarot_ICON.bin at TAR root ---
    cmd += ["-C", build_dir, f"{app_name}.bin"]
    if os.path.exists(icon_path):
        cmd += ["-C", project_dir, f"{app_name}_ICON.bin"]
    else:
        print("Warning: icon not found, creating TAR without icon")

    # --- Add binary assets under assets/... ---
    binary_assets_src = os.path.join(project_dir, "images/output/05_binary")
    for folder in os.listdir(binary_assets_src):
        folder_path = os.path.join(binary_assets_src, folder)
        if os.path.isdir(folder_path):
            # Include folder contents recursively, mapped to assets/<folder>
            cmd += [
                "-C", binary_assets_src,
                "--transform", f"s,^{folder},assets/{folder},",
                folder
            ]

    subprocess.run(cmd, check=True)
    print(f"Created TAR in project root: {tar_path}")

projenv.AddPostAction("$BUILD_DIR/firmware.bin", create_tar)
