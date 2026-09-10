# OpenMS CLI framework 1.0.0 experimental

Standalone shared library providing TOPPBase, parameter handling, adapter scaffolding, INIUpdater and ToolHandler. Link `OpenMS::CLI` and consume the exact installed core SDK recorded in `dependencies.lock.json`. Core no longer links or exports these classes. Historical include paths remain; the new `OPENMSCLI_DLLAPI` export belongs to the CLI library.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/sdk/core -DCMAKE_INSTALL_PREFIX=/sdk/cli
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build
```

`BUILD_TESTING=ON` requires the core SDK's optional TestSupport component. Tests own their fixtures and include the moved model-default compatibility assertion. Additional manifest parser/discovery C++ tests are authored; native execution remains pending an authorized build.

Products install `share/openms4/tools/<package>.tools.tsv` beneath their own installation prefix. Each non-comment row has exactly four tab-separated fields: executable name, category, independent product version, and binary path relative to that prefix. Categories `DesktopViewer` and `DesktopWorkflow` resolve executables but are omitted from the command-line parameter-discovery catalogue. Legacy `.ttd` internal tool discovery remains supported.

`OPENMS_TOOL_PREFIX_PATH` adds installation prefixes using the operating system's path-list separator. Discovery also probes the current executable's prefix, macOS app-bundle prefix, and the CLI library's prefix. Duplicate names, malformed fields and parent/root path traversal fail explicitly. A manifest-declared missing executable fails instead of falling through to an unrelated PATH version. Unregistered names retain sibling/PATH compatibility discovery. `findExecutable()` returns a usable absolute path or throws `Exception::FileNotFound`.

TOPPBase no longer rejects new products because they are absent from a compiled official-tool list. Registered products report their own version plus linked core provenance. The six historical TOPPBase parameter-tag constants alias the core's ParamTags definitions, avoiding a core-to-CLI dependency. Runtime data remains supplied by the versioned core compatibility bundle.
