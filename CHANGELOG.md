# Changelog

Releases are tagged `vMAJOR.MINOR.PATCH`; the version lives in
`include/sg/Version.hpp`. Projects pin a tag, so every entry that can break a
project says so.

While the major version is 0, a minor bump may break the API.

## v0.1.0

First tagged release, and the first one meant to be consumed by other projects.

- **Laws on live data** (`sg/core/Laws.hpp`): identity, associativity,
  composition, functoriality, lens put-get / put-put / settles and declared
  diagrams, each reported as a concrete counterexample. `sg::verify`,
  `sg::enforce`.
- **Typed handles** (`sg/core/Typed.hpp`): ill-typed composition of arrows,
  functors, lenses and diagrams does not compile.
- **Fixed:** the identity functor dropped objects created after it was built;
  composing an arrow with a loop registered the wrong type; two loops could not
  compose.
- **Packaging:** `stategine::stategine` alias; examples, tests and the GLFW
  download are built only when stategine is the top-level project;
  `SG_BUILD_EXAMPLES`, `SG_BUILD_TESTS`, `SG_BUILD_GL` options; `SG_VERSION_*`
  macros.
- **Breaking:** the library target no longer adds `-Wall -Wextra` / `/W4` or
  MinGW `-static` link flags to projects that use it. Link
  `stategine::warnings` and `stategine::static_runtime` to keep them.
