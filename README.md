# helix-xml

A declarative XML UI engine for [LVGL](https://lvgl.io) 9.5+. Loads components, screens, styles,
subjects and data bindings from XML at runtime — no code generation, no build step.

**License:** MIT. See [`LICENSE`](LICENSE).

## Provenance

helix-xml is a hard fork of the XML engine that used to ship inside LVGL core.

LVGL removed the engine from core in v9.5 ([`7c1e0684f`](https://github.com/lvgl/lvgl/pull/9565),
2026-01-27) and now sells it as part of LVGL Pro under a proprietary license. This fork was taken
from the last commit before that removal:

```
commit    a15dcbeb56db765db8853261df3013ba037b17fc
describe  v9.4.0-358-ga15dcbeb5
date      2026-01-26
```

LVGL's `LICENCE.txt` at that tree is plain MIT, and no separate license ever covered the XML
sources while they were in the repository. LVGL has confirmed this in public: *"Versions up to 9.4
keep their MIT XML engine forever. MIT can't be revoked, and we wouldn't try."*
([announcement](https://lvgl.io/blog/announcement-lv_xml-removal-from-v9-5), 2026-07-27)

So the code here is MIT, permanently, and modifications by 356C LLC are contributed under MIT to
keep it that way.

### File provenance

| Category | Count | Copyright |
|----------|-------|-----------|
| Unmodified from LVGL @ `a15dcbeb5` | 32 | LVGL Kft |
| Derived from LVGL, modified here | 43 | LVGL Kft + 356C LLC |
| Written for helix-xml | 7 | 356C LLC |

Every file carries SPDX headers recording which bucket it falls in. `src/libs/expat/` is
byte-identical to the copy LVGL vendored and keeps its own `LICENSE.txt`.

### There is no upstream

This fork does not track anything. Post-removal XML development happens in a proprietary LVGL
repository that we neither have access to nor may copy from. Every bug here is ours to fix.

**Clean-room rule.** If a feature is implemented that LVGL Pro also has, it must be built from
LVGL's *published documentation* (lvgl.io/docs/pro), blog posts, and the observable XML format —
never from LVGL Pro source, a decompiled Editor, or any post-`7c1e0684f` implementation. Commits
adding such a feature must name the documentation URL and the date it was read.

## Extensions beyond the LVGL engine

Features added here that upstream's engine does not have:

| Feature | What it does |
|---------|--------------|
| `cond=` expressions + `<subject_expr>` | Full expression language over subjects — arithmetic, comparison, boolean, word forms (`a or b gt c`). Upstream binds one subject with one comparison operator |
| `<repeat count=>` | Repeated fragments with `$i` / `${expr}` interpolation; reactive rebuild when `count` names a subject |
| `<if cond=>` / `<else/>` | Structural conditionals — only the matching branch is built |
| `parts="main,indicator,knob"` | Apply one style bind across several widget parts |
| `hidden_if_prop_eq` / `hidden_if_empty` | Prop-driven visibility without allocating a subject |
| Inline element text | `<label>Hello</label>` sets `text` and synthesizes a translation tag |
| `float` and `color` subjects | Upstream supports only `int` and `string` |
| `<subject name= type= value=>` | Single-tag subject declarations (upstream's tag-per-type form also parses) |

## Usage in HelixScreen

Built directly into the binary — not a submodule, not patched. Edit the sources in place.

- Pure C. Must not include or call app-layer C++.
- Excluded from clang-format; match surrounding style by hand.
- Include `helix_xml.h` after `lvgl.h`; `helix_xml_private.h` after `lvgl_private.h`.

Further reading, in the HelixScreen repository:

- [LVGL_XML_SITUATION.md](https://github.com/prestonbrown/helixscreen/blob/main/docs/devel/LVGL_XML_SITUATION.md) — licensing and upstream analysis
- [LVGL9_XML_GUIDE.md](https://github.com/prestonbrown/helixscreen/blob/main/docs/devel/LVGL9_XML_GUIDE.md) — syntax guide
- [LVGL9_XML_ATTRIBUTES_REFERENCE.md](https://github.com/prestonbrown/helixscreen/blob/main/docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md) — attribute reference
