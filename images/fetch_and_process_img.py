import os
import requests
import numpy as np
from PIL import Image, ImageEnhance, ImageFilter, ImageOps

BASE_URL = "https://www.sacred-texts.com/tarot/pkt/img/ar{:02d}.jpg"
OUTPUT_DIR = "output"

# --------------------------------------------------
# CONFIG
# --------------------------------------------------

DOWNLOAD_ORIGINALS = False

SIZES = {
    "1": (128, 218),   # 1-card
    "3": (88, 153),    # 3-card
}

FOLDERS = {
    "orig": "01_original",
    "gray": "02_gray",
    "resized": "03_resized",
    "bit1": "04_1bit",
    "bin": "05_binary",
}

# --------------------------------------------------
# E-INK IMAGE PROCESSING TWEAKS
# --------------------------------------------------

# Global tone
GAMMA = 0.9
CONTRAST = 1.0

# Midtones (fake gray bias)
MIDTONE_BIAS = 0.35      # -0.3 → darker mids, +0.3 → lighter

# Local contrast
LOCAL_CONTRAST = 0.8     # 0 = off
LOCAL_RADIUS = 8         # px

# Edge preservation
SHARPEN_AMOUNT = 0.4     # 0–0.6 typical
SHARPEN_RADIUS = 1.0

# Grayscale quantization (fake gray)
GRAY_LEVELS = 3          # 2 = pure B/W, 3–4 recommended

# Noise (anti-ghosting)
NOISE_AMOUNT = 0.02      # 0.0–0.05

# Thresholding
THRESHOLD_MODE = "fixed" # "fixed" | "mean"
FIXED_THRESHOLD = 135

# Dithering
DITHER_MODE = "ordered"  # "none" | "fs" | "ordered"
DITHER_STRENGTH = 0.7
BAYER_SIZE = 4           # 2 or 4

# --------------------------------------------------
# Directory setup
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
# Download
# --------------------------------------------------
def download_image(index):
    path = os.path.join(OUTPUT_DIR, FOLDERS["orig"], f"ar{index:02d}.jpg")

    if not DOWNLOAD_ORIGINALS:
        if not os.path.exists(path):
            raise FileNotFoundError(path)
        return path

    resp = requests.get(BASE_URL.format(index))
    if resp.status_code != 200:
        return None

    with open(path, "wb") as f:
        f.write(resp.content)

    print(f"Downloaded ar{index:02d}")
    return path

# --------------------------------------------------
# Image processing helpers
# --------------------------------------------------
def apply_gamma(img, gamma):
    if gamma == 1.0:
        return img
    inv = 1.0 / gamma
    lut = [int((i / 255) ** inv * 255) for i in range(256)]
    return img.point(lut)

def midtone_bias(img, bias):
    if bias == 0:
        return img
    lut = []
    for i in range(256):
        x = i / 255
        x = x + bias * (1 - abs(2 * x - 1))
        x = min(1, max(0, x))
        lut.append(int(x * 255))
    return img.point(lut)

def local_contrast(img, amount, radius):
    if amount == 0:
        return img
    blur = img.filter(ImageFilter.GaussianBlur(radius))
    return Image.blend(blur, img, 1 + amount)

def edge_sharpen(img, amount, radius):
    if amount == 0:
        return img
    return img.filter(
        ImageFilter.UnsharpMask(
            radius=radius,
            percent=int(amount * 100),
            threshold=2,
        )
    )

def quantize_levels(img, levels):
    if levels <= 2:
        return img
    step = 255 / (levels - 1)
    return img.point(lambda p: int(round(p / step) * step))

def add_noise(img, amount):
    if amount == 0:
        return img
    arr = np.array(img, dtype=np.float32)
    noise = np.random.uniform(-255, 255, arr.shape) * amount
    arr = np.clip(arr + noise, 0, 255)
    return Image.fromarray(arr.astype(np.uint8))

def threshold_image(img):
    if THRESHOLD_MODE == "fixed":
        return img.point(lambda p: 255 if p > FIXED_THRESHOLD else 0)
    if THRESHOLD_MODE == "mean":
        mean = sum(img.getdata()) / (img.width * img.height)
        return img.point(lambda p: 255 if p > mean else 0)
    raise ValueError("Invalid THRESHOLD_MODE")

def ordered_dither(img, strength, size):
    if size == 2:
        bayer = np.array([[0, 2],
                          [3, 1]]) / 4.0
    elif size == 4:
        bayer = np.array([
            [0,  8,  2, 10],
            [12, 4, 14, 6],
            [3, 11, 1,  9],
            [15, 7, 13, 5]
        ]) / 16.0
    else:
        raise ValueError("BAYER_SIZE must be 2 or 4")

    img_np = np.array(img, dtype=np.float32)
    h, w = img_np.shape
    out = np.zeros_like(img_np)

    for y in range(h):
        for x in range(w):
            t = (bayer[y % size, x % size] - 0.5) * 255 * strength
            out[y, x] = 255 if img_np[y, x] + t > 128 else 0

    return Image.fromarray(out.astype(np.uint8))

# --------------------------------------------------
# BIN packing
# --------------------------------------------------
def write_bin_bitmap(img, output_path):
    pixels = img.load()
    w, h = img.size
    if w % 8 != 0:
        raise ValueError("Width must be divisible by 8")

    data = bytearray()
    for y in range(h):
        for x in range(0, w, 8):
            byte = 0
            for b in range(8):
                bit = 1 if not pixels[x + b, y] else 0
                byte |= bit << (7 - b)
            data.append(byte)

    with open(output_path, "wb") as f:
        f.write(data)

# --------------------------------------------------
# Main processing
# --------------------------------------------------
def process_image(path, index):
    img = Image.open(path).convert("L")

    img = apply_gamma(img, GAMMA)
    img = midtone_bias(img, MIDTONE_BIAS)
    img = local_contrast(img, LOCAL_CONTRAST, LOCAL_RADIUS)
    img = edge_sharpen(img, SHARPEN_AMOUNT, SHARPEN_RADIUS)

    for key, (W, H) in SIZES.items():
        gray_path = os.path.join(
            OUTPUT_DIR, FOLDERS["gray"], key, f"ar{index:02d}.png"
        )
        img.save(gray_path)

        resized = img.resize((W, H), Image.NEAREST)
        resized_path = os.path.join(
            OUTPUT_DIR, FOLDERS["resized"], key, f"ar{index:02d}.png"
        )
        resized.save(resized_path)

        proc = quantize_levels(resized, GRAY_LEVELS)
        proc = add_noise(proc, NOISE_AMOUNT)
        proc = threshold_image(proc)

        if DITHER_MODE == "fs":
            bit = proc.convert("1", dither=Image.FLOYDSTEINBERG)
        elif DITHER_MODE == "ordered":
            bit = ordered_dither(proc, DITHER_STRENGTH, BAYER_SIZE).convert(
                "1", dither=Image.NONE
            )
        else:
            bit = proc.convert("1", dither=Image.NONE)

        bit_path = os.path.join(
            OUTPUT_DIR, FOLDERS["bit1"], key, f"ar{index:02d}.png"
        )
        bit.save(bit_path)

        bin_path = os.path.join(
            OUTPUT_DIR, FOLDERS["bin"], key, f"ar{index:02d}.bin"
        )
        write_bin_bitmap(bit, bin_path)

# --------------------------------------------------
# Entry point
# --------------------------------------------------
if __name__ == "__main__":
    ensure_dirs()

    for i in range(22):  # ar00–ar21
        p = download_image(i)
        if p:
            process_image(p, i)
