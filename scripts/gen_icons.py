#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 The NØR Browser authors
"""Generates the NØR Browser UI icon set (SVG) into resources/icons/.

Design language: 24x24 viewBox, 2px round strokes, light purple-gray
(#C9C2D8) that reads well on the dark theme; purple (#8B5CF6) accents.
"""
import os
import textwrap

OUT = os.path.join(os.path.dirname(__file__), "..", "resources", "icons")

HEAD = ("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' "
        "viewBox='0 0 24 24' fill='none' stroke='{color}' stroke-width='{w}' "
        "stroke-linecap='round' stroke-linejoin='round'>\n")

ICONS = {
    # name: (color, stroke-width, body)
    "back": ("#c9c2d8", 2.2, "<path d='M15 5l-7 7 7 7'/>"),
    "forward": ("#c9c2d8", 2.2, "<path d='M9 5l7 7-7 7'/>"),
    "reload": ("#c9c2d8", 2.2,
               "<path d='M20 12a8 8 0 1 1-2.34-5.66'/>"
               "<path d='M20 4v5h-5' stroke='#8b5cf6'/>"),
    "stop": ("#c9c2d8", 2.2, "<path d='M6 6l12 12M18 6L6 18'/>"),
    "home": ("#c9c2d8", 2.2,
             "<path d='M4 11l8-7 8 7'/>"
             "<path d='M6 10v9h4v-5h4v5h4v-9'/>"),
    "star": ("#c9c2d8", 2,
             "<path d='M12 3.5l2.7 5.6 6.1.8-4.5 4.2 1.2 6-5.5-3-5.5 3 1.2-6L3.2 9.9l6.1-.8z'/>"),
    "star-filled": ("#8b5cf6", 1.8,
                    "<path d='M12 3.5l2.7 5.6 6.1.8-4.5 4.2 1.2 6-5.5-3-5.5 3 1.2-6L3.2 9.9l6.1-.8z' fill='#8b5cf6'/>"),
    "bookmarks": ("#c9c2d8", 2.2,
                  "<path d='M7 4h10v16l-5-4-5 4z'/>"),
    "downloads": ("#c9c2d8", 2.2,
                  "<path d='M12 4v10'/>"
                  "<path d='M8 11l4 4 4-4' stroke='#8b5cf6'/>"
                  "<path d='M5 19h14'/>"),
    "history": ("#c9c2d8", 2.2,
                "<circle cx='12' cy='12' r='8.5'/>"
                "<path d='M12 7v5l3.5 2.5' stroke='#8b5cf6'/>"),
    "settings": ("#c9c2d8", 2,
                 "<circle cx='12' cy='12' r='3.2'/>"
                 "<path d='M12 2.8v2.4M12 18.8v2.4M4.6 6.6l1.9 1.4M17.5 16l1.9 1.4M2.8 12h2.4M18.8 12h2.4M4.6 17.4l1.9-1.4M17.5 8l1.9-1.4'/>"),
    "shield": ("#8b5cf6", 2,
               "<path d='M12 3l7 2.8v5.4c0 4.6-3 7.8-7 9.8-4-2-7-5.2-7-9.8V5.8z' fill='none'/>"
               "<path d='M9 11.5l2 2 4-4'/>"),
    "shield-off": ("#6b6382", 2,
                   "<path d='M12 3l7 2.8v5.4c0 4.6-3 7.8-7 9.8-4-2-7-5.2-7-9.8V5.8z'/>"
                   "<path d='M9 12h6'/>"),
    "search": ("#c9c2d8", 2.2,
               "<circle cx='10.5' cy='10.5' r='6'/>"
               "<path d='M15 15l5 5' stroke='#8b5cf6'/>"),
    "plus": ("#c9c2d8", 2.4, "<path d='M12 5v14M5 12h14'/>"),
    "close": ("#c9c2d8", 2.4, "<path d='M6 6l12 12M18 6L6 18'/>"),
    "private": ("#c9c2d8", 2.2,
                "<path d='M3 8h18v4.5c0 3.6-4 6.5-9 6.5s-9-2.9-9-6.5z'/>"
                "<circle cx='8.5' cy='12.5' r='2' fill='#8b5cf6' stroke='none'/>"
                "<circle cx='15.5' cy='12.5' r='2' fill='#8b5cf6' stroke='none'/>"
                "<path d='M9 8c1-2.5 1.5-3.5 3-3.5S14 5.5 15 8'/>"),
    "menu": ("#c9c2d8", 2.4, "<path d='M4 7h16M4 12h16M4 17h16'/>"),
    "globe": ("#c9c2d8", 2,
              "<circle cx='12' cy='12' r='8.5'/>"
              "<path d='M3.5 12h17'/>"
              "<path d='M12 3.5c2.6 2.4 4 5.2 4 8.5s-1.4 6.1-4 8.5c-2.6-2.4-4-5.2-4-8.5s1.4-6.1 4-8.5z'/>"),
    "lock": ("#8b5cf6", 2,
             "<rect x='5.5' y='10.5' width='13' height='9' rx='2'/>"
             "<path d='M8.5 10.5V8a3.5 3.5 0 0 1 7 0v2.5'/>"
             "<circle cx='12' cy='15' r='1.4' fill='#8b5cf6' stroke='none'/>"),
    "warning": ("#e8b44a", 2,
                "<path d='M12 4l9 16H3z'/>"
                "<path d='M12 10v4.5'/>"
                "<circle cx='12' cy='17.4' r='.5' fill='#e8b44a' stroke='none'/>"),
    "find-previous": ("#c9c2d8", 2.2, "<path d='M8 12h8M12 8l-4 4 4 4'/>"),
    "find-next": ("#c9c2d8", 2.2, "<path d='M8 12h8M12 8l4 4-4 4'/>"),
    "tab-new": ("#c9c2d8", 2.2,
                "<path d='M4 18V6.5A1.5 1.5 0 0 1 5.5 5H11l1.5 2h6A1.5 1.5 0 0 1 20 8.5V18'/>"
                "<path d='M12 10.5v5M9.5 13h5' stroke='#8b5cf6'/>"),
    # Application icon: dark rounded square with an N + O-slash mark.
    "app": ("none", 0,
            "<rect width='24' height='24' rx='5.5' fill='#16121f'/>"
            "<rect x='0.8' y='0.8' width='22.4' height='22.4' rx='5' fill='none' stroke='#8b5cf6' stroke-width='1.6'/>"
            "<path d='M6.2 17.2V6.8l4 5.4V6.8' stroke='#e8e4f0' stroke-width='1.9' stroke-linecap='round' stroke-linejoin='round' fill='none'/>"
            "<circle cx='16.4' cy='12' r='5.2' fill='none' stroke='#8b5cf6' stroke-width='1.9'/>"
            "<path d='M12.7 15.7l7.4-7.4' stroke='#8b5cf6' stroke-width='1.9' stroke-linecap='round'/>"),
}


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    for name, (color, width, body) in ICONS.items():
        svg = HEAD.format(color=color, w=width) + textwrap.indent(body, "  ") + "\n</svg>\n"
        path = os.path.join(OUT, name + ".svg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(svg)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
