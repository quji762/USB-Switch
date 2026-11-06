from PIL import Image
from pathlib import Path

resources = Path(__file__).resolve().parent.parent / "resources"
src = resources / "usb-switch.ico"
dst = resources / "preview.png"

im = Image.open(src)
print("ico frames:", im.ico.sizes())
im.size = (256, 256)
bg = Image.new("RGBA", (256, 256), (235, 238, 242, 255))
bg.alpha_composite(im)
bg.convert("RGB").save(dst)
print("preview saved:", dst)
