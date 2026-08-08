# Unity (vendored)

Unity Test Framework, vendored so the helix-xml test suite has no external
dependency beyond a C compiler, CMake, and the LVGL checkout that
`tests/CMakeLists.txt` fetches.

| | |
|---|---|
| Upstream | https://github.com/ThrowTheSwitch/Unity |
| Version pinned | **v2.7.0** (released 2026-07-16) |
| Files taken | `src/unity.c`, `src/unity.h`, `src/unity_internals.h` |
| Licence | MIT (headers left intact in each file) |

Fetched with:

```sh
for f in unity.c unity.h unity_internals.h; do
  curl -sSLO "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.7.0/src/$f"
done
```

## Updating

Re-run the command above with the new tag, update the table, and run the full
suite. Do not hand-edit these files — any local change would be silently lost
on the next update. If Unity needs configuring, do it with `UNITY_*` compile
definitions on the `helix_xml_test_unity` target in `tests/CMakeLists.txt`.
