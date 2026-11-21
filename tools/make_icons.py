# Genera res/gdVideoBtn.png (boton del editor, 160x160) y logo.png (336x336) del Video Importer.
# Misma familia visual que GIF Importer (marco blanco / atardecer troceado en rectangulos) pero propia: fotograma de pelicula con
# perforaciones, boton de reproduccion y la etiqueta "MP4".
from PIL import Image, ImageDraw, ImageFont
import math, random

SS = 4  # supersampling

def sunset(x, y, w, h):
    """color (r,g,b) del paisaje en coordenadas normalizadas 0..1"""
    u, v = x / w, y / h
    # cielo: coral -> violeta -> azul noche
    stops = [(0.0, (236, 122, 104)), (0.45, (170, 104, 130)), (0.62, (112, 92, 148)), (0.72, (66, 62, 128)), (1.0, (28, 28, 76))]
    for i in range(len(stops) - 1):
        a, ca = stops[i]; b, cb = stops[i + 1]
        if a <= v <= b:
            t = (v - a) / (b - a); c = tuple(int(ca[k] + (cb[k] - ca[k]) * t) for k in range(3)); break
    else:
        c = stops[-1][1]
    # colinas
    hill = 0.70 + 0.05 * math.sin(u * 9.0) + 0.03 * math.sin(u * 23.0 + 1.0)
    if v > hill: c = (38, 36, 98) if v < hill + 0.12 else (26, 26, 70)
    # sol
    d = math.hypot((u - 0.52) * w / h, v - 0.42)
    if d < 0.2: c = (255, 226, 80)
    return c

def picture(wpx, hpx, mincell, seed=3):
    """imagen estilo 'BSP': quadtree adaptativo con borde oscuro en cada celda"""
    img = Image.new("RGB", (wpx, hpx)); dr = ImageDraw.Draw(img)
    def avg_var(x0, y0, x1, y1):
        pts = [sunset(x0 + (x1 - x0) * i / 4.0, y0 + (y1 - y0) * j / 4.0, wpx, hpx) for i in range(5) for j in range(5)]
        m = [sum(p[k] for p in pts) / len(pts) for k in range(3)]
        var = max(sum((p[k] - m[k]) ** 2 for p in pts) / len(pts) for k in range(3))
        return tuple(int(c) for c in m), var
    def rec(x0, y0, x1, y1, depth):
        c, var = avg_var(x0, y0, x1, y1)
        w, h = x1 - x0, y1 - y0
        if (var > 90 and min(w, h) > mincell * 2) or max(w, h) > wpx / 4.5:
            if w >= h: m = (x0 + x1) // 2; rec(x0, y0, m, y1, depth + 1); rec(m, y0, x1, y1, depth + 1)
            else: m = (y0 + y1) // 2; rec(x0, y0, x1, m, depth + 1); rec(x0, m, x1, y1, depth + 1)
            return
        dr.rectangle([x0, y0, x1 - 1, y1 - 1], fill=c, outline=(24, 24, 40))
    rec(0, 0, wpx, hpx, 0)
    return img

def rounded_mask(size, r):
    m = Image.new("L", size, 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1], r, fill=255); return m

def play_triangle(dr, cx, cy, size, fill, outline, ow):
    pts = [(cx - size * 0.38, cy - size * 0.5), (cx - size * 0.38, cy + size * 0.5), (cx + size * 0.55, cy)]
    dr.polygon(pts, fill=outline)
    # triangulo interior (encogido hacia su centro) para dejar el contorno
    gx = sum(p[0] for p in pts) / 3; gy = sum(p[1] for p in pts) / 3
    k = 1 - ow / (size * 0.5)
    dr.polygon([(gx + (p[0] - gx) * k, gy + (p[1] - gy) * k) for p in pts], fill=fill)

def perforations(dr, x, y0, y1, w, h, gap, color, r):
    y = y0 + gap / 2
    while y + h <= y1:
        dr.rounded_rectangle([x, y, x + w, y + h], r, fill=color); y += h + gap

def make_logo():
    S = 336 * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bg = Image.new("RGBA", (S, S), (20, 20, 31, 255)); img.paste(bg, (0, 0), rounded_mask((S, S), 30 * SS))
    pic = picture(236 * SS, 336 * SS, 5 * SS).convert("RGBA")
    img.alpha_composite(pic, (50 * SS, 0))
    dr = ImageDraw.Draw(img)
    # tiras de pelicula a los lados (se pintan SOBRE el borde del paisaje)
    for x0 in (0, 286):
        dr.rectangle([x0 * SS, 0, (x0 + 50) * SS, S], fill=(20, 20, 31))
        perforations(dr, (x0 + 13) * SS, 6 * SS, 330 * SS, 24 * SS, 22 * SS, 14 * SS, (232, 232, 242), 5 * SS)
    img.putalpha(rounded_mask((S, S), 30 * SS))
    dr = ImageDraw.Draw(img)
    play_triangle(dr, S / 2, S / 2, 150 * SS, (255, 255, 255), (20, 20, 31), 12 * SS)
    return img.resize((336, 336), Image.LANCZOS)

def make_button():
    S = 160 * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0)); dr = ImageDraw.Draw(img)
    # marco blanco de pelicula con perforaciones oscuras a los lados
    dr.rounded_rectangle([8 * SS, 20 * SS, 150 * SS, 134 * SS], 14 * SS, fill=(255, 255, 255, 255))
    pic = picture(104 * SS, 96 * SS, 5 * SS).convert("RGBA")
    pm = rounded_mask(pic.size, 6 * SS)
    img.paste(pic, (28 * SS, 29 * SS), pm)
    for x in (13 * SS, 134 * SS):
        perforations(dr, x, 26 * SS, 128 * SS, 11 * SS, 10 * SS, 7 * SS, (40, 40, 56), 3 * SS)
    dr = ImageDraw.Draw(img)
    # boton de reproduccion en el centro del fotograma
    cx, cy = 80 * SS, 77 * SS
    dr.ellipse([cx - 19 * SS, cy - 19 * SS, cx + 19 * SS, cy + 19 * SS], fill=(20, 20, 31, 215))
    play_triangle(dr, cx + 2 * SS, cy, 22 * SS, (255, 255, 255), (255, 255, 255), 0)
    # etiqueta MP4 (arriba a la izquierda, como la de GIF)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30 * SS)
    except Exception:
        font = ImageFont.load_default()
    bx0, by0, bx1, by1 = 3 * SS, 3 * SS, 100 * SS, 46 * SS
    dr.rounded_rectangle([bx0 - 2 * SS, by0 - 2 * SS, bx1 + 2 * SS, by1 + 2 * SS], 12 * SS, fill=(255, 255, 255, 255))
    dr.rounded_rectangle([bx0, by0, bx1, by1], 10 * SS, fill=(22, 22, 28, 255))
    tw = dr.textlength("MP4", font=font)
    dr.text(((bx0 + bx1) / 2 - tw / 2, (by0 + by1) / 2 - 1 * SS), "MP4", font=font, fill=(255, 224, 70, 255), anchor="lm")
    # flecha verde abajo a la derecha (misma pista visual que GIF: "convertir hacia el editor")
    ax, ay = 112 * SS, 128 * SS
    arrow = [(ax - 34 * SS, ay - 12 * SS), (ax - 8 * SS, ay - 12 * SS), (ax - 8 * SS, ay - 26 * SS), (ax + 26 * SS, ay), (ax - 8 * SS, ay + 26 * SS), (ax - 8 * SS, ay + 12 * SS), (ax - 34 * SS, ay + 12 * SS)]
    # (flecha apuntando a la derecha; contorno verde oscuro)
    dr.polygon(arrow, fill=(10, 80, 20, 255))
    gx = sum(p[0] for p in arrow) / len(arrow); gy = sum(p[1] for p in arrow) / len(arrow)
    dr.polygon([(gx + (p[0] - gx) * 0.88, gy + (p[1] - gy) * 0.88) for p in arrow], fill=(86, 220, 90, 255))
    return img.resize((160, 160), Image.LANCZOS)

if __name__ == "__main__":
    make_button().save("res/gdVideoBtn.png"); make_logo().save("logo.png"); print("ok")
