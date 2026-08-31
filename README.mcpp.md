# Pango, for mcpp

Pango 1.56.1 as three mcpp packages, with `upstream/` untouched and everything
this fork adds under `mcpp/`.

```toml
[dependencies]
gnome.pangocairo = "1.56.1"   # the usual entry point: layout + rendering
# gnome.pango and gnome.pangoft2 arrive transitively — do NOT name them
```

| member | what it is | module exports |
|---|---|---|
| `gnome.pango` | itemisation, bidi, line breaking, markup | 851 |
| `gnome.pangoft2` | fontconfig picks the file, FreeType rasterises | 88 |
| `gnome.pangocairo` | the cairo backend — `pango_cairo_show_layout` | 284 |

Two ways to consume it, and **you pick one**:

```cpp
import gnome.pangocairo;      // re-exports gnome.pango, gnome.pangoft2, cairo
```
```cpp
#include <pango/pangocairo.h>
```

They do not compose: a TU that does both reaches `<stdio.h>` twice — once
through the module's global fragment, once directly — and the same
`struct _IO_FILE` becomes two entities. Which route to take is decided by
macros, which a module cannot carry: code using `PANGO_TYPE_*` or `G_OBJECT`
takes the header route, code using the function API imports and includes
nothing. **Each member ships a test for each route**, so both stay working.

## Why a fork

**Generators, not line count** — the criterion that made cairo (104k lines) a
plain descriptor and libdisplay-info (2k) a fork. Pango has three, and this
fork adds a fourth:

| upstream | here |
|---|---|
| `configure_file` → `pango-features.h` | `gen_features()` |
| `configure_file` → `config.h` (**no input template**) | `gen_config()` |
| `gobject/glib-mkenums` (816 lines of Python) | `write_enumtypes()`, 27 GTypes |
| *(none — this fork's own)* | `gen_module()` → three `.cppm` |

**There is no `sh` and no `python` in the build.** `build.mcpp` is a compiled
C++ program.

⚠️ `config.h` is the odd one: upstream writes
`configure_file(output: 'config.h', configuration: pango_conf)` with **no
`input:`**, so meson emits a `#define` per key and there is nothing in the tree
to substitute into. The file has to be *written*, and every value in it is a
decision this fork makes. Two of them are arithmetic rather than choice:

```
PANGO_BINARY_AGE    = minor * 100 + micro     # 5601
PANGO_INTERFACE_AGE = minor odd ? 0 : micro   # 1
```

`pango_version_check()` reads `PANGO_BINARY_AGE`, so a wrong value makes a
correct version comparison answer wrongly **at run time** rather than failing
to build. The test asserts both directions.

## ⭐ The one test that produces pixels

`mcpp/pangocairo/tests/pangocairo.cpp` renders `"Hello 世界"` to an ARGB32
surface and counts non-transparent pixels. Reaching that costs seven packages:

```
gnome.pango        itemisation, bidi, line breaking
gnome.pangoft2     fontconfig picks the file, FreeType rasterises
gnome.gio          PangoFontMap is a GListModel
compat.harfbuzz    shaping
compat.fribidi     the bidi algorithm
freedesktop.cairo  the surface the glyphs land on
```

A blank image means one of them is not doing its job.

### ⚠️ Two ways this assertion was wrong before it was right

**`drawn > 100`.** Passed here — 184 font families, 216 pixels — and failed on
a CI runner with four families and 72. A threshold calibrated to the
developer's font set is an assertion about the *machine*. Ink is ink: `> 0`.

**`families > 0` as the guard.** Also not enough, and this one took two CI
rounds to see. That same runner reported **four families** and then measured
the layout at `80x858545` in one process and `80x346398` in another — nonsense
heights on a 100px surface, because four families there means fontconfig has
*entries* with no usable font behind them. The glyphs land off the surface and
the ink count becomes arbitrary: **0 in one test and 116 in its twin, from
byte-identical drawing code.**

So the guard is now the layout's **own metrics** — `w > 0 && h > 0 && h <= 100`
— which tests the machine's suitability explicitly instead of inferring it. When
that fails the run **says so**, because "the check was skipped" and "the text
rendered" must not look alike.

## ⚠️ A silent degradation this fork walked into

`HAVE_CAIRO_FREETYPE` was dropped from `config.h` by an editing slip. Nothing
failed to build. `pangocairo-fontmap.c` simply registered **no backends**, and
the program died at run time with

```
Pango-CRITICAL: Unknown $PANGOCAIRO_BACKEND value.
  Available backends are:            <- an empty list
```

followed by a segfault. The test now reads the font map's *font type* and
requires `CAIRO_FONT_TYPE_FT`, which is the assertion that would have caught it
immediately.

## ✅ `cairo`'s module was not sufficient on its own — and this fork found it

Measured on the `1.18.2-mcpp2` asset: it exported 470 names, **zero
enumerators** (no `CAIRO_FORMAT_ARGB32`, no `CAIRO_FONT_TYPE_FT`) and no
`cairo_t`. The index's own cairo example did not notice, because it wrote
**both** `#include <cairo.h>` and `import freedesktop.cairo;` — the header
supplied what the module lacked, so that import had never been asked to stand
on its own.

It surfaced *here*, because pangocairo cannot mix the two routes
(`pangocairo.h` reaches glib and therefore `<stdio.h>`) and so had to ask the
module for `cairo_t` and got nothing. This fork carried a workaround for one
release — scanning `cairo.h` itself — and **that workaround is now gone**:
`1.18.2-mcpp3` exports 697 names, `cairo_t` and the 192 enumerators included,
and the plain re-export is sufficient.

The module is also named `cairo` now, not `freedesktop.cairo`: freedesktop.org
hosts the git and does not own the interface.

## ⚠️ Name `pangocairo` alone

`gnome.pango` and `gnome.pangoft2` are workspace **path** dependencies of
`gnome.pangocairo`, and `gnome.gio` is a path dependency of `gnome.pango`.
mcpp rejects a package requested both ways:

```
error: dependency 'gnome.gobject' is requested as both a version dep
       (by 'pango') and a path dep (by 'gnome.gio@2.82.5'). Pick one.
```

That error is how this fork learned it, on the first build.

## Layout

```
upstream/              pango 1.56.1, byte for byte (CI diffs it)
mcpp/common/
  enums.h              glib-mkenums, ported from the glib fork
  prelude.h            gen_config / gen_features / gen_pango_enumtypes
  modules.h            gen_module, the .cppm wrappers
mcpp/pango/            + pango-features.h, pango-enum-types.{h,c}
mcpp/pangoft2/         + HAVE_FREETYPE
mcpp/pangocairo/       + HAVE_CAIRO, HAVE_CAIRO_FREETYPE
```
