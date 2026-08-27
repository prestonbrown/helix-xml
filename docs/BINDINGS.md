# Bindings and expressions

<!-- SPDX-License-Identifier: MIT -->

Reference for the reactive side of the engine: subjects, the binding elements
that observe them, and the expression language those bindings accept.

Everything here is engine behaviour and is pinned by `tests/cases/` — each
section names the file that covers it. Examples use plain LVGL widgets
(`lv_obj`, `lv_label`) so they hold regardless of the widget set an application
registers on top.

## Subjects

A subject is an observable value. Declare them in a component's `<subjects>`
block:

```xml
<component>
  <subjects>
    <subject name="temp"      type="int"    value="0"/>
    <subject name="label"     type="string" value=""/>
    <subject name="ratio"     type="float"  value="0.0"/>
    <subject name="accent"    type="color"  value="0xff0000"/>
  </subjects>
  …
</component>
```

`int` and `string` are upstream LVGL's set; `float` and `color` are additions
here. A single-tag `<subject name= type= value=>` form is the documented one;
upstream's tag-per-type spelling (`<int name=…/>`) also parses.

Subjects declared in a component live in **that component's scope**. A subject
the host application registers before any component loads is global and visible
everywhere. Covered by `test_subject_decl.c`, `test_subject_provenance.c`.

### String subjects need a caller-owned buffer

A string subject does not allocate. Whatever the host passes at registration is
the storage, and it must outlive every binding that reads it — a stack buffer
is a use-after-free waiting for the next observer notification.

## Binding elements

All of these are children of the widget they act on.

| Element | Acts on | Comparison form | Expression form |
|---|---|---|---|
| `bind_flag_if_eq`, `…_not_eq`, `…_gt`, `…_ge`, `…_lt`, `…_le` | an `lv_obj` flag | ✅ | — |
| `bind_flag_if` | an `lv_obj` flag | — | ✅ `cond=` |
| `bind_state_if_eq` … | an `lv_state_t` | ✅ | — |
| `bind_state_if` | an `lv_state_t` | — | ✅ `cond=` |
| `bind_style_if_eq` … | a named style | ✅ | — |
| `bind_style_if` | a named style | — | ✅ `cond=` |

The not-equal suffix is spelled **`_not_eq`**, not `_ne` — `_ne` is the *expression*
operator (`a ne b`), and the two are not interchangeable. An element named
`bind_flag_if_ne` does not exist and is silently not recognised.

`invert="true"` on the expression forms flips the mapping — apply when the
condition is **false**. That is the shape most `flag="hidden"` bindings want,
since the markup usually wants to read as "show when …".

```xml
<lv_obj>
  <bind_flag_if cond="error or temp gt threshold" flag="hidden" invert="true"/>
  <lv_label text="ALARM"/>
</lv_obj>
```

Covered by `test_cond_binds.c`.

### The flag binds are two-way

This is the rule that surprises people, so it is stated plainly:

> A binding that does not match does not abstain. It applies the **opposite**
> outcome.

`subject="can" flag="hidden" ref_value="0"` reads like "hide when `can` is 0".
It is not a one-way rule: when `can` is anything else the binding actively
**removes** `hidden` — including a `hidden="true"` the markup itself asked for.

Two consequences:

**Do not put two bindings for the same flag on one widget.** They do not AND.
Each asserts both outcomes, so they overwrite each other and whichever runs last
owns the flag:

```xml
<!-- WRONG. Reads as "show when dirty, never when can is 0".
     Does the opposite: with can != 0 the second bind clears the flag the
     first one just set, so the widget shows even when dirty is 0. -->
<bind_flag_if cond="dirty" flag="hidden" invert="true"/>
<bind_flag_if_eq subject="can" flag="hidden" ref_value="0"/>

<!-- RIGHT. One binding, both conditions. -->
<bind_flag_if cond="can and dirty" flag="hidden" invert="true"/>
```

Two bindings for one flag are safe only on **different** widgets — a wrapper and
the child inside it — which is why the broken form can look correct in markup
that happens to be nested that way.

Both behaviours are pinned:
`test_a_non_matching_flag_bind_removes_the_flag_rather_than_abstaining` and
`test_two_flag_binds_for_one_flag_do_not_and_together` in `test_cond_binds.c`.

## The expression language

Available to every `cond=` and to `<subject_expr>`.

Operands are subject names and integer literals. Operators, loosest first:

| Precedence | Operators | Word forms |
|---|---|---|
| 1 | `or` | |
| 2 | `and` | |
| 3 | `==` `!=` `<` `<=` `>` `>=` | `eq` `ne` `lt` `le` `gt` `ge` |
| 4 | `+` `-` | |
| 5 | `*` `/` `%` | |
| 6 | `not`, unary `-` | |

Parentheses group. A bare subject name is truthy when non-zero.

**Prefer the word forms in XML.** `<`, `>` and `&&` need escaping as `&lt;`,
`&gt;` and `&amp;&amp;`, which is legal but unreadable:

```xml
<bind_flag_if cond="error or temp gt threshold" flag="hidden" invert="true"/>
<bind_flag_if cond="error &amp;&amp; temp &gt; threshold" flag="hidden"/>
```

`and` / `or` short-circuit. Covered by `test_expr.c`.

## `<subject_expr>` — a derived subject

```xml
<subjects>
  <subject name="error"     type="int" value="0"/>
  <subject name="temp"      type="int" value="0"/>
  <subject name="threshold" type="int" value="70"/>
  <subject_expr name="alarm" expr="error or temp gt threshold"/>
</subjects>
```

`alarm` is a real int subject that recomputes whenever any operand changes, and
anything that can bind a subject can bind it. It is registered in the scope of
the component that declares it.

### When each is resolved — the trap

`cond=` and `<subject_expr>` accept the same language, so choosing between them
looks like style. It is lifecycle, and the wrong choice fails **silently**:

| Construct | Resolved | Sees |
|---|---|---|
| `<subject_expr>` | when the **component is registered** | only subjects that exist at registration time |
| `cond=` | when the **view is created** | those, plus anything registered before the widget is built |

`<subject_expr>` resolves its operands once, at registration. An operand that
does not exist yet — typically a subject the host creates in a later
initialization pass — does not compile, and the derived subject is **never
registered**. Nothing errors; bindings that reference it simply never fire.

So:

- Operand registered before components load → either construct works.
- Operand registered after → **`cond=` only**, repeated at each site.
- View created before the operand is registered → neither works. Register the
  subject earlier; do not reach for a different binding.

Pinned by `test_a_forward_reference_to_a_later_subject_does_not_register` and
`test_subject_expr_with_an_uncompilable_expr_registers_nothing` in
`test_subject_expr.c`.

Reuse across several sites is **not** on its own a reason to pick
`<subject_expr>` — check when the operands are registered first.

## `<if>` / `<else>` — structural conditionals

```xml
<if cond="compact">
  <lv_label text="Short"/>
<else/>
  <lv_obj>…an expensive subtree…</lv_obj>
</if>
```

Only the matching branch is **built**. This is the difference from
`bind_flag_if … flag="hidden"`, which builds both and toggles visibility: cheap
for a light subtree, wasteful for an expensive one.

A cond that mentions a subject stays **live** — changing it tears down the built
branch and replays the other one, repeatedly, not once. So the choice against a
flag binding is about cost, not about reactivity:

- `<if>` — the branch is expensive to build (a whole card, a chart, an
  alternate layout), and it changes rarely. You pay a rebuild per flip.
- `bind_flag_if … flag="hidden"` — the subtree is cheap and the condition
  changes often. You pay for both branches once and then only toggle.

`<else/>` is optional: a false cond with no else builds nothing and still loads.
Covered by `test_if_else.c`, including
`test_reactive_cond_flips_the_built_branch_both_ways`.

## `<repeat>` — repeated fragments

```xml
<repeat count="4">
  <lv_label text="Slot $i"/>
</repeat>
```

`$i` is the zero-based index; `${…}` interpolates an expression over it. When
`count` names a subject rather than a literal, the fragment **rebuilds
reactively** as that subject changes.

`<repeat>` replaces the widget-creation loop only. Measured layout and data
population still belong in host code. Covered by `test_repeat.c`.

## Prop-driven visibility

`hidden_if_prop_eq`, `hidden_if_prop_not_eq` and `hidden_if_empty` hide a widget
based on a component **prop** rather than a subject, resolved at parse time. Use
them for structure that depends on how a component was instantiated — no subject
need be allocated for something that never changes after creation. An unset prop
falls back to its declared default.

Unlike the subject binds above, a prop hide is **decided once and never
reasserted** — it is not two-way, so it will not fight a later flag change the
way two subject binds fight each other. Covered by `test_prop_conditionals.c`
(`test_a_prop_pipe_hide_is_decided_once_and_never_reasserted`).

## Styles across parts

`parts="main,indicator,knob"` applies one style binding to several widget parts
in a single element, instead of one binding per part. Covered by
`test_style.c`.

## Failure modes

The engine prefers to skip a construct rather than abort a load, so most
mistakes surface as *nothing happening*:

| Symptom | Usual cause |
|---|---|
| A `<subject_expr>` subject does not exist | an operand was not registered when the component was registered — see the trap above |
| A binding never fires | it references a subject name that does not resolve in this scope |
| A widget is visible when it should not be | two bindings for one flag on one widget |
| `cond=` does not compile | an operand name is unknown at view-creation time |

Warnings are emitted through LVGL's log; raise its level when a binding appears
inert.
