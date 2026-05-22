# External Dependencies

Mnemos uses pinned CMake `FetchContent` entries for approved third-party
dependencies. Do not vendor source here unless an ADR explicitly approves it.

Current M0 dependency:

- Catch2 `v3.8.1` for tests, fetched by `cmake/modules/MnemosTesting.cmake`.
