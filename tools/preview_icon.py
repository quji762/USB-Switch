from PIL import Image

src = r"E:\Documents\deepseek-harness\default-workspace\quickeject\resources\quickeject.ico"
dst = r"E:\Documents\deepseek-harness\default-workspace\quickeject\resources\preview.png"

im = Image.open(src)
print("ico frames:", im.ico.sizes())
im.size = (256, 256)
bg = Image.new("RGBA", (256, 256), (235, 238, 242, 255))
bg.alpha_composite(im)
bg.convert("RGB").save(dst)
print("preview saved:", dst)
