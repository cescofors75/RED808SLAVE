from PIL import Image
import numpy as np
import os

BASE = os.path.dirname(os.path.abspath(__file__))

def to_rgb565_c(img, var_name):
    arr = np.array(img)
    h, w = arr.shape[:2]
    inc = '#include "lvgl.h"'
    lines = [
        "// Auto-generated RGB565 image: %dx%d" % (w, h),
        inc, "",
        "static const LV_ATTRIBUTE_MEM_ALIGN uint8_t %s_map[] = {" % var_name,
    ]
    bstrs = []
    for y in range(h):
        for x in range(w):
            r = int(arr[y, x, 0])
            g = int(arr[y, x, 1])
            b = int(arr[y, x, 2])
            p = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            lo = p & 0xFF
            hi = (p >> 8) & 0xFF
            bstrs.append("0x%02x,0x%02x" % (lo, hi))
    for i in range(0, len(bstrs), 12):
        chunk = ",".join(bstrs[i : i + 12])
        lines.append("  " + chunk + ",")
    lines.append("};")
    lines.append("")
    lines.append("const lv_img_dsc_t img_%s = {" % var_name)
    lines.append("  .header.always_zero = 0,")
    lines.append("  .header.w = %d," % w)
    lines.append("  .header.h = %d," % h)
    lines.append("  .data_size = %d," % (w * h * 2))
    lines.append("  .header.cf = LV_IMG_CF_TRUE_COLOR,")
    lines.append("  .data = %s_map," % var_name)
    lines.append("};")
    return "\n".join(lines)


# Logo: 48x32 (~3KB)
logo = Image.open(os.path.join(BASE, "logo.png")).resize((48, 32), Image.LANCZOS)
out_logo = os.path.join(BASE, "src", "ui", "img_dev_logo.c")
with open(out_logo, "w") as f:
    f.write(to_rgb565_c(logo, "dev_logo"))
print("logo: 48x32 = %d bytes -> %s" % (48 * 32 * 2, out_logo))

# Name: 200x133 (~53KB)
name = Image.open(os.path.join(BASE, "name.png")).resize((200, 133), Image.LANCZOS)
out_name = os.path.join(BASE, "src", "ui", "img_dev_name.c")
with open(out_name, "w") as f:
    f.write(to_rgb565_c(name, "dev_name"))
print("name: 200x133 = %d bytes -> %s" % (200 * 133 * 2, out_name))
print("Done!")
