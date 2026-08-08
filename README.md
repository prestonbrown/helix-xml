# helix-xml

A declarative XML UI engine for [LVGL](https://lvgl.io) 9.5+. Loads components, screens, styles,
subjects and data bindings from XML at runtime — no code generation, no build step.

**License:** MIT. See [`LICENSE`](LICENSE).
**Home:** https://github.com/prestonbrown/helix-xml — issues and roadmap live there.

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

## Building it into a project

helix-xml is source you compile alongside LVGL — there is no build system here, deliberately.
It needs LVGL **9.5 or newer** (9.5 is the first release with no XML of its own, so nothing
collides).

There is nothing to configure, nothing to install, and no CMake package or exported target to
find. The one CMake file in the repository is `tests/CMakeLists.txt`, and it exists solely to
build the test suite (see [Tests](#tests)) - it is not a way to consume the library. To use
helix-xml, add the sources below to whatever build you already have.

**Sources to compile:**

```
src/xml/*.c
src/xml/parsers/*.c
src/libs/expat/*.c
```

**Include paths:**

| Path | For |
|------|-----|
| `<lvgl-root>` | `lvgl.h` |
| `<lvgl-root>/src` | `<misc/lv_types.h>`, `<core/lv_observer.h>`, `<lv_conf_internal.h>` … |
| `<helix-xml-root>` | `helix_xml.h` |
| `<helix-xml-root>/src` | internal `xml/*.h` headers |
| wherever your `lv_conf.h` lives | LVGL configuration |

`<lvgl-root>/src` must be visible to **anything that includes `helix_xml.h`**, not just to
helix-xml's own translation units — the public headers include LVGL that way. In CMake, put it
on the `lvgl` target as `PUBLIC` rather than making it `PRIVATE` to helix-xml.

**Configuration:** set `LV_USE_XML 1` in your `lv_conf.h`. Every source here is wrapped in
`#if LV_USE_XML`, so with it off the whole library compiles away to nothing.

**Use:** include `helix_xml.h` after `lvgl.h`, and `helix_xml_private.h` after `lvgl_private.h`.
Then register components and build a view:

```c
#include <lvgl.h>
#include "helix_xml.h"

lv_xml_init();
lv_xml_register_component_from_file("A:ui/my_panel.xml");
lv_obj_t * panel = lv_xml_create(lv_screen_active(), "my_panel", NULL);
```

Loading from files needs an LVGL filesystem driver; `lv_xml_register_component_from_data()`
takes a string instead if you would rather embed the markup.

> The engine used to reach LVGL by relative path (`"../misc/lv_types.h"`), which only worked
> because `src/` carried symlinks faking LVGL's directory layout — so the tree only built when
> planted at `<x>/lib/helix-xml` beside `<x>/lib/lvgl`. Those are gone; the include path above
> is the whole contract now.

## Tests

The suite lives in `tests/` and builds standalone. It does not need - and must not be pointed
at - the build of whatever application consumes helix-xml. From a bare clone:

```
cmake -S tests -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

That is the whole setup. CMake fetches LVGL **v9.5.0** with `FetchContent` (the tag is
`HELIX_LVGL_TAG` in `tests/CMakeLists.txt`), compiles helix-xml against it as a static
library, and builds one executable per file in `tests/cases/`: **402 tests across 21
executables**, a couple of seconds end to end.

`tests/lv_conf.h` is the pinned configuration for the test build, derived from LVGL 9.5.0's
`lv_conf_template.h` with the switches the XML engine needs turned on. It is deliberately not
synced with any consumer's `lv_conf.h` - the tests must not inherit whatever the consuming
application happens to configure.

### Bring your own LVGL

For an offline build, or to bisect an LVGL change:

```
cmake -S tests -B build -DLVGL_DIR=/path/to/lvgl
```

`LVGL_DIR` must be a **pristine** upstream checkout, and a patched one is rejected at
configure time with an explanation. This is not fussiness: an application that vendors LVGL
as a submodule typically has its own patches applied to it, and such patches can call
app-side symbols that only exist when linking that application. Configure would succeed, the
compile would succeed, and then every test executable would die at link time with a wall of
undefined symbols that says nothing about the real cause. A configure-time probe turns that
into one readable message instead.

If you want a checkout on disk, clone upstream yourself:

```
git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git /tmp/lvgl-pristine
cmake -S tests -B build -DLVGL_DIR=/tmp/lvgl-pristine
```

### What the tests assert

**Structure, never pixels.** Widget tree shape, names and parent/child relationships, child
counts, label text, flags and states, style property values the XML under test declared,
subject wiring, and captured log output. There is no screenshot or pixel comparison anywhere,
and by rule no assertion may depend on measured geometry or font metrics. That rule is what
lets `tests/lv_conf.h` differ freely from any consumer's config - colour depth, default font,
enabled widget set and theme are chosen for the test build and are not what any real device
runs, so a geometry assertion would encode the test config and break for reasons unrelated to
the XML engine.

Isolation is total: every test gets a full `lv_init()` / `lv_deinit()` cycle. Nothing carries
across a test boundary - not LVGL's heap, not the display or the active screen, not the XML
engine's component, style and subject registries. Test order does not matter.

Helpers, in `tests/helpers/`:

| Header | For |
|--------|-----|
| `helix_test_env.h` | Per-test setup and teardown: `lv_init()`, a headless 800x480 display and draw buffer, a blank active screen, `lv_xml_init()`, and the reverse on the way out (in an order that is load-bearing) |
| `xml_assert.h` | Assertions over an XML-built tree: `ASSERT_XML_REGISTERS`, `XML_CREATE`, `ASSERT_NAMED`, `ASSERT_NO_NAMED`, `ASSERT_CHILD_COUNT`, `ASSERT_LABEL_TEXT`, `ASSERT_FLAG`, `ASSERT_NO_FLAG`, `ASSERT_STATE`, `ASSERT_STYLE_INT`. There is no `ASSERT_WIDTH`, and none may be added |
| `helix_test_pump.h` | Advance LVGL's tick and run its timer handler. Reactive `<repeat count="subject">` and `<if cond=>` defer their rebuild onto an async path, so the order is always mutate, pump, assert - asserting straight after `lv_subject_set_int()` reads the old tree |
| `helix_log_capture.h` | Capture LVGL log output so a test can assert on it. Most failure paths in this engine warn and then return success, so the emitted warning is often the only observable difference between "handled and reported" and "silently wrong" - and for the `_silent` API variants, the absence of a warning is itself the contract |

### Adding a test

Drop a `.c` file into `tests/cases/`. The glob picks it up and it becomes its own executable
and its own ctest entry - no `CMakeLists.txt` edit. A case file looks like this:

```c
#include "helpers/helix_test_env.h"

void setUp(void)    { helix_test_env_setup(); }
void tearDown(void) { helix_test_env_teardown(); }

static void test_something(void) { /* ... */ }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_something);
    return UNITY_END();
}
```

The runner is [Unity](https://github.com/ThrowTheSwitch/Unity), vendored in `tests/unity/`.
Anything a test reads off disk lives under `tests/assets/` and is reached through the
`HELIX_TEST_ASSET_DIR` compile definition, so no test depends on the shell's working
directory.

### CI

`.github/workflows/ci.yml` runs three jobs on every push and pull request:

| Job | What it does |
|-----|--------------|
| `test` | The suite on both **gcc** and **clang**, `CMAKE_BUILD_TYPE=Debug` |
| `sanitize` | The same suite under **ASan + UBSan**, with `detect_leaks=1`, `abort_on_error=1` and UBSan's `halt_on_error=1`, so undefined behaviour cannot report itself and let the test pass anyway |
| `conf-guards` | Compile-checks the library's `#if` guards against generated variants of `tests/lv_conf.h`. `LV_USE_XML 0` must collapse every source here to an empty translation unit and still produce a linkable archive. `LV_USE_TRANSLATION 0` must drop `lv_xml_translation.c` and leave every call site of it guarded. `LV_USE_OBJ_NAME 0` is a negative check: the build must **fail**, with `lv_xml.c`'s `#error "LV_USE_OBJ_NAME is required to use XMLs"` - names are how the engine addresses everything it builds, so a silent build without them would be worse than no build |

## Notes for contributors

- Pure C. It must not include or call app-layer C++.
- Excluded from clang-format; match surrounding style by hand.
- `src/libs/expat/` is LVGL's vendored expat, carrying LVGL's `#include <lv_conf_internal.h>` /
  `#if LV_USE_XML` wrapper. `add_lvgl_if.sh` reapplies that wrapper when re-vendoring.

Further reading, in the HelixScreen repository:

- [LVGL_XML_SITUATION.md](https://github.com/prestonbrown/helixscreen/blob/main/docs/devel/LVGL_XML_SITUATION.md) — licensing and upstream analysis
- [LVGL9_XML_GUIDE.md](https://github.com/prestonbrown/helixscreen/blob/main/docs/devel/LVGL9_XML_GUIDE.md) — syntax guide
- [LVGL9_XML_ATTRIBUTES_REFERENCE.md](https://github.com/prestonbrown/helixscreen/blob/main/docs/devel/LVGL9_XML_ATTRIBUTES_REFERENCE.md) — attribute reference
