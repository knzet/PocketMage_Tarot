import os
import requests
from PIL import Image

BASE_URL = "https://www.sacred-texts.com/tarot/pkt/img/ar{:02d}.jpg"
OUTPUT_DIR = "output"

# -----------------------------
# CONFIG
# -----------------------------
DOWNLOAD_ORIGINALS = False  # <-- set True to redownload originals

SIZES = {
    "single": (128, 218),   # 1-card
    "spread3": (88, 153),   # 3-card
}

FOLDERS = {
    "orig": "01_original",
    "gray": "02_gray",
    "resized": "03_resized",
    "bit1": "04_1bit",
    "bin": "05_binary",
}


# --------------------------------------------------
# Ensure directory structure
# --------------------------------------------------
def ensure_dirs():
    os.makedirs(os.path.join(OUTPUT_DIR, FOLDERS["orig"]), exist_ok=True)
    for stage in ["gray", "resized", "bit1", "bin"]:
        for key in SIZES:
            os.makedirs(
                os.path.join(OUTPUT_DIR, FOLDERS[stage], key),
                exist_ok=True,
            )


# --------------------------------------------------
# Download image
# --------------------------------------------------
def download_image(index):
    path = os.path.join(OUTPUT_DIR, FOLDERS["orig"], f"ar{index:02d}.jpg")

    if not DOWNLOAD_ORIGINALS:
        if not os.path.exists(path):
            raise FileNotFoundError(
                f"Missing original image: {path} (DOWNLOAD_ORIGINALS=False)"
            )
        return path

    url = BASE_URL.format(index)
    resp = requests.get(url)
    if resp.status_code != 200:
        print(f"Failed to download {url}")
        return None

    with open(path, "wb") as f:
        f.write(resp.content)

    print(f"Downloaded ar{index:02d}")
    return path


# --------------------------------------------------
# PACK image into 1-bit-per-pixel .bin format
# --------------------------------------------------
def write_bin_bitmap(img, output_path):
    pixels = img.load()
    width, height = img.size

    if width % 8 != 0:
        raise ValueError("Width must be divisible by 8")

    data = bytearray()

    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for bit in range(8):
                px = pixels[x + bit, y]
                # Treat any non-zero value as white, zero as black
                bitval = 1 if not px else 0
                byte |= bitval << (7 - bit)
            data.append(byte)

    with open(output_path, "wb") as f:
        f.write(data)

    print(f"BIN written: {output_path} ({len(data)} bytes)")


# --------------------------------------------------
# Process image for all sizes
# --------------------------------------------------
def process_image(input_path, index):
    base_gray = Image.open(input_path).convert("L")

    for key, (W, H) in SIZES.items():
        # grayscale
        gray_path = os.path.join(
            OUTPUT_DIR, FOLDERS["gray"], key, f"ar{index:02d}.png"
        )
        base_gray.save(gray_path)

        # resize
        resized = base_gray.resize((W, H), Image.NEAREST)
        resized_path = os.path.join(
            OUTPUT_DIR, FOLDERS["resized"], key, f"ar{index:02d}.png"
        )
        resized.save(resized_path)

        # 1-bit
        bit_img = resized.convert("1", dither=Image.NONE)
        bit_path = os.path.join(
            OUTPUT_DIR, FOLDERS["bit1"], key, f"ar{index:02d}.png"
        )
        bit_img.save(bit_path)

        # bin
        bin_path = os.path.join(
            OUTPUT_DIR, FOLDERS["bin"], key, f"ar{index:02d}.bin"
        )
        write_bin_bitmap(bit_img, bin_path)


# --------------------------------------------------
# Main
# --------------------------------------------------
if __name__ == "__main__":
    ensure_dirs()

    for i in range(22):  # ar00–ar21
        img_path = download_image(i)
        if img_path:
            process_image(img_path, i)
