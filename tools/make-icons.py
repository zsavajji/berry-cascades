#!/usr/bin/env python3
"""Draws the action bar icons (white line art, 81x81) and the app icon
(114x114) into assets/. Run from anywhere; needs Pillow."""
import math
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "images")
S = 4  # supersampling
N = 81 * S
W = 6 * S  # stroke width
WHITE = (255, 255, 255, 255)


def canvas(size=N):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def save(img, name, size=81):
    img.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, name))


def p(x, y):
    return (x * N / 81.0, y * N / 81.0)


def line(d, pts, width=W):
    d.line([p(*q) for q in pts], fill=WHITE, width=width, joint="curve")
    for q in (pts[0], pts[-1]):
        x, y = p(*q)
        r = width / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=WHITE)


def arc(d, box, start, end, width=W):
    d.arc([p(box[0], box[1]), p(box[2], box[3])], start, end, fill=WHITE, width=width)


def arrow(name, flip):
    img, d = canvas()
    xs = (lambda x: 81 - x) if flip else (lambda x: x)
    line(d, [(xs(20), 40.5), (xs(61), 40.5)])
    line(d, [(xs(38), 22), (xs(20), 40.5), (xs(38), 59)])
    save(img, name)


def reload_icon():
    img, d = canvas()
    arc(d, (20, 20, 61, 61), 20, 310)
    # a filled head at the arc's end, pointing along the clockwise tangent
    a = math.radians(310)
    cx, cy, r = 40.5, 40.5, 20.5
    ex, ey = cx + r * math.cos(a), cy + r * math.sin(a)
    tx, ty = -math.sin(a), math.cos(a)  # clockwise tangent
    nx, ny = math.cos(a), math.sin(a)  # outward normal
    tip = (ex + 9 * tx, ey + 9 * ty)
    d.polygon([p(*tip), p(ex - 2 * tx + 9 * nx, ey - 2 * ty + 9 * ny),
               p(ex - 2 * tx - 9 * nx, ey - 2 * ty - 9 * ny)], fill=WHITE)
    save(img, "reload.png")


def stop_icon():
    img, d = canvas()
    line(d, [(24, 24), (57, 57)])
    line(d, [(57, 24), (24, 57)])
    save(img, "stop.png")


def home_icon():
    img, d = canvas()
    line(d, [(16, 41), (40.5, 18), (65, 41)])
    line(d, [(24, 36), (24, 63), (57, 63), (57, 36)])
    line(d, [(35, 63), (35, 48), (46, 48), (46, 63)])
    save(img, "home.png")


def star(d, cx, cy, r_out, r_in, fill=None):
    pts = []
    for i in range(10):
        r = r_out if i % 2 == 0 else r_in
        a = math.radians(-90 + 36 * i)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    if fill:
        d.polygon([p(*q) for q in pts], fill=fill)
    else:
        line(d, pts + [pts[0]], width=5 * S)


def bookmark_add_icon():
    img, d = canvas()
    star(d, 36, 40, 24, 10)
    line(d, [(62, 50), (62, 70)], width=5 * S)
    line(d, [(52, 60), (72, 60)], width=5 * S)
    save(img, "bookmark_add.png")


def bookmarks_icon():
    img, d = canvas()
    star(d, 40.5, 42, 26, 11)
    save(img, "bookmarks.png")


def settings_icon():
    img, d = canvas()
    cx, cy = 40.5, 40.5
    for i in range(8):
        a = math.radians(45 * i)
        line(d, [(cx + 17 * math.cos(a), cy + 17 * math.sin(a)),
                 (cx + 26 * math.cos(a), cy + 26 * math.sin(a))], width=9 * S)
    arc(d, (22, 22, 59, 59), 0, 360)
    arc(d, (33, 33, 48, 48), 0, 360, width=5 * S)
    save(img, "settings.png")


def share_icon():
    img, d = canvas()
    pts = [(58, 20), (24, 40.5), (58, 61)]
    line(d, [pts[0], pts[1]], width=5 * S)
    line(d, [pts[1], pts[2]], width=5 * S)
    for x, y in pts:
        X, Y = p(x, y)
        r = 8 * S
        d.ellipse([X - r, Y - r, X + r, Y + r], fill=WHITE)
    save(img, "share.png")


def copy_icon():
    img, d = canvas()
    line(d, [(30, 22), (60, 22), (60, 52)], width=5 * S)
    line(d, [(21, 31), (51, 31), (51, 61), (21, 61), (21, 31)], width=5 * S)
    save(img, "copy.png")


def restart_icon():
    img, d = canvas()
    arc(d, (20, 20, 61, 61), -230, 50)
    line(d, [(40.5, 12), (40.5, 36)])
    save(img, "restart.png")


def search_icon():
    img, d = canvas()
    arc(d, (16, 16, 50, 50), 0, 360)
    line(d, [(46, 46), (64, 64)], width=8 * S)
    save(img, "search.png")


def select_all_icon():
    img, d = canvas()
    dash = 5 * S
    for a, b in [((16, 16), (65, 16)), ((65, 16), (65, 65)), ((65, 65), (16, 65)), ((16, 65), (16, 16))]:
        n = 5
        for i in range(n):
            t0, t1 = i / n, (i + 0.55) / n
            line(d, [(a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0),
                     (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)], width=dash)
    for y in (32, 42, 52):
        line(d, [(28, y), (53, y)], width=4 * S)
    save(img, "selectall.png")


def chevron(name, up):
    img, d = canvas()
    if up:
        line(d, [(20, 50), (40.5, 29), (61, 50)])
    else:
        line(d, [(20, 31), (40.5, 52), (61, 31)])
    save(img, name)


def tabs_icon():
    img, d = canvas()
    line(d, [(26, 18), (64, 18), (64, 52)], width=5 * S)
    line(d, [(16, 28), (54, 28), (54, 64), (16, 64), (16, 28)], width=5 * S)
    save(img, "tabs.png")


def new_tab_icon():
    img, d = canvas()
    line(d, [(16, 22), (52, 22), (52, 62), (16, 62), (16, 22)], width=5 * S)
    line(d, [(62, 8), (62, 30)], width=5 * S)
    line(d, [(51, 19), (73, 19)], width=5 * S)
    save(img, "newtab.png")


def history_icon():
    img, d = canvas()
    arc(d, (16, 16, 65, 65), 0, 360)
    line(d, [(40.5, 26), (40.5, 41), (52, 48)], width=5 * S)
    save(img, "history.png")


def paste_icon():
    img, d = canvas()
    line(d, [(30, 18), (18, 18), (18, 66), (63, 66), (63, 18), (51, 18)], width=5 * S)
    line(d, [(30, 12), (51, 12), (51, 24), (30, 24), (30, 12)], width=5 * S)
    for y in (38, 48):
        line(d, [(29, y), (52, y)], width=4 * S)
    save(img, "paste.png")


def downloads_icon():
    img, d = canvas()
    line(d, [(40.5, 14), (40.5, 50)])
    line(d, [(26, 37), (40.5, 51), (55, 37)])
    line(d, [(16, 54), (16, 65), (65, 65), (65, 54)])
    save(img, "downloads.png")


def app_icon():
    size = 114 * S
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=22 * S, fill=(38, 35, 64, 255))
    berry = (122, 64, 196, 255)
    hi = (178, 140, 235, 255)
    for cx, cy in [(44, 50), (70, 50), (57, 72), (31, 72), (83, 72), (44, 94), (70, 94)]:
        r = 13 * S
        X, Y = cx * S, cy * S
        d.ellipse([X - r, Y - r, X + r, Y + r], fill=berry)
        r2 = 4 * S
        d.ellipse([X - 6 * S - r2, Y - 6 * S - r2, X - 6 * S + r2, Y - 6 * S + r2], fill=hi)
    d.polygon([(57 * S, 34 * S), (50 * S, 14 * S), (64 * S, 14 * S)], fill=(90, 190, 110, 255))
    img.resize((114, 114), Image.LANCZOS).save(os.path.join(OUT, "..", "icon.png"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    arrow("back.png", False)
    arrow("forward.png", True)
    reload_icon()
    stop_icon()
    home_icon()
    bookmark_add_icon()
    bookmarks_icon()
    settings_icon()
    share_icon()
    copy_icon()
    restart_icon()
    search_icon()
    select_all_icon()
    chevron("up.png", True)
    chevron("down.png", False)
    tabs_icon()
    new_tab_icon()
    history_icon()
    paste_icon()
    downloads_icon()
    app_icon()
