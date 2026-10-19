# Vendored font8x8

A monochrome 8x8 bitmap font covering the basic-Latin block (U+0000–U+007F),
used by Klest components to draw text without a font-rendering dependency.

- **Upstream:** https://github.com/dhepper/font8x8
- **Author:** Daniel Hepper, based on public-domain VGA fonts by Marcel Sondaar
  (International Business Machines).
- **License:** Public Domain.

Only the `font8x8_basic` table is vendored. The glyph bytes are verbatim from
upstream; the array declaration was changed from `char` to
`const unsigned char` and split into a `.c`/`.h` pair so it builds cleanly as an
isolated static library (warnings silenced, like `tomlc17`), keeping Klest's own
strict `-Werror` off third-party code.

Each glyph is 8 bytes, one per row (top to bottom). Within a byte, bit 0 (the
least-significant bit) is the leftmost column.
