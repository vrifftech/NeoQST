# NeoQST

[![CI](https://github.com/vrifftech/NeoQST/actions/workflows/ci.yml/badge.svg)](https://github.com/vrifftech/NeoQST/actions/workflows/ci.yml)

NeoQST is a purpose-aware editor for Jade Empire `QST V3.2` quest resources. It is an independent repository and builds against the separate sibling `NeoShared` repository.

## QST and QST2

`.qst` and `.qst2` are treated as filename-extension aliases for the same native payload. Both contain a GFF V3.2 document whose internal file type is `QST `; there is no separate `QST2` header or second document schema.

NeoQST:

- opens and saves both extensions;
- preserves the extension of an opened file;
- defaults new documents to `.qst`;
- uses the same validation and editor for both.

Observed `.qst2` resources use the same internal `QST V3.2` header and schema as `.qst`. NeoQST therefore treats `.qst2` as an accepted filename alias rather than inventing a second document format. The canonical Jade archive resource extension remains `.qst`.

## Quest model

NeoQST exposes the Jade-specific QST structure rather than presenting it as a generic GFF tree:

- quest name and description Jade StringRefs;
- task name, summary, optional pre/post quest summaries, notifications, and next-group transition;
- task groups and their task membership;
- optional Jade TLK lookup for resolved text previews;
- XML and JSON import/export;
- native QST/QST2 validation and round trips.

QST has two different notions of identity:

- `Identifier` is the visible task or group identifier. The first item may omit it, in which case its list index is the effective identifier.
- `TaskIndexList.Task` and `NextTaskGroup` are **list positions**, not `Identifier` values.

NeoQST therefore repairs positional references when deleting a task or task group. Changing an `Identifier` does not rewrite positional references.

## Repository layout

Clone NeoQST and NeoShared as siblings:

```text
workspace/
  neoshared/
  NeoQST/
```

CMake uses `../neoshared` by default. A different location can be supplied with:

```text
-DNEOSHARED_ROOT=/path/to/NeoShared
```

The build wrappers expose the same setting as `--neoshared-root` on Linux and `-NeoSharedRoot` on Windows.

## Build

Linux GUI build:

```sh
bash ./scripts/build.sh \
  --wx ON \
  --require-wx ON \
  --jobs "$(nproc)" \
  --clean
```

Linux CLI/core-only build:

```sh
bash ./scripts/build.sh \
  --wx OFF \
  --jobs "$(nproc)" \
  --clean
```

Windows GUI build:

```powershell
& ..\neoshared\scripts\install-wxwidgets.ps1 `
  -VcpkgRoot C:\vcpkg `
  -Triplet x64-windows-static `
  -CleanAfterBuild

.\scripts\build.ps1 `
  -Clean `
  -Wx ON `
  -RequireWx ON `
  -VcpkgRoot C:\vcpkg `
  -VcpkgTriplet x64-windows-static `
  -Parallel ([Environment]::ProcessorCount)
```

## CLI

```text
neoqst-cli quest.qst [dialog.tlk]
neoqst-cli --info quest.qst2
neoqst-cli --new output.qst
neoqst-cli --roundtrip input.qst output.qst
neoqst-cli --search quest.qst term
neoqst-cli --export quest.qst xml output.xml
neoqst-cli --export quest.qst json output.json
neoqst-cli --import input.xml output.qst xml
```

Run `neoqst-cli --help` for patch-generation options and the complete syntax.

## Linux runner artifact

The Linux workflow publishes `NeoQST-linux-x86_64.tar.gz`. Keep its directory structure intact and launch `NeoQST/bin/NeoQST`; the launcher loads the exact wxWidgets shared libraries bundled by the runner before starting the real executable.

## Shared game directories

**File > Open Game Directory** lists installations saved by the Neo tools. Selecting Jade Empire opens NeoQST's QST/QST2 file picker at that installation directory. **Manage Game Directories...** edits the shared installation registry.

## Tests

The regression test covers:

- new QST creation;
- `.qst2` native save/reopen;
- task and group insertion;
- unique identifiers;
- list-position reference validation;
- task-reference repair after deletion;
- next-group repair after group deletion;
- Jade StringRef fields.
