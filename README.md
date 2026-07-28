# NeoQST

[![CI](https://github.com/vrifftech/NeoQST/actions/workflows/ci.yml/badge.svg)](https://github.com/vrifftech/NeoQST/actions/workflows/ci.yml)

NeoQST is a purpose-aware editor for Jade Empire `QST V3.2` quest resources. It builds against the separate sibling NeoShared repository.

## QST and QST2

`.qst` and `.qst2` are filename-extension aliases for the same native payload. Both contain a GFF V3.2 document with internal file type `QST `. NeoQST opens and saves either extension, preserves the extension of an opened document, and defaults new documents to `.qst`.

## Runtime-backed quest model

NeoQST exposes the fields loaded and saved by Jade Empire's journal runtime.

Quest fields include:

```text
QuestType
QuestName
QuestDescription
TaskList
TaskGroupList
```

Serialized runtime-state resources may also contain:

```text
QuestResRef
QuestActive
QuestComplete
QuestUpdated
TimeHi
TimeLo
```

Those state fields remain absent from ordinary static QST definitions unless **Store runtime quest state fields** is enabled.

Task fields include:

```text
TaskName
TaskSummary
QuestSummaryPre
QuestSummaryPost
Identifier
Complete
NextTaskGroup
NotifyActive
NotifyComplete
```

Task-group fields include:

```text
Identifier
TaskIndexList
Active
ANDGroup
OnComplete
```

`ANDGroup = 0` completes the group when any member task completes. `ANDGroup = 1` requires all member tasks. `OnComplete` is an optional script run when the group completes.

## Identifiers and list references

Jade uses two different kinds of number:

- Task and group `Identifier` values are signed 16-bit runtime identifiers. NeoQST permits `0` through `32767` and requires uniqueness within the task list or group list. A missing `Identifier` has runtime value `0`.
- `TaskIndexList.Task` and nonnegative `NextTaskGroup` values are signed-byte **list positions**, not Identifier values. A QST is therefore limited to 128 tasks and 128 task groups.

`NextTaskGroup` supports:

```text
-1  no transition
-2  complete and deactivate the quest
0–127  activate the corresponding task-group list position
```

Jade stores one owning task-group pointer in each task. NeoQST therefore prevents the same task list position from being assigned to multiple groups.

Deleting a task repairs every positional task reference. Deleting a group repairs every `NextTaskGroup` reference. Changing an Identifier does not rewrite positional references.

## Build

Clone NeoQST and NeoShared as siblings:

```text
workspace/
  NeoShared/
  NeoQST/
```

Linux GUI build:

```sh
bash ./scripts/build.sh --wx ON --require-wx ON --jobs "$(nproc)" --clean
```

Linux CLI/core-only build:

```sh
bash ./scripts/build.sh --wx OFF --jobs "$(nproc)" --clean
```

Windows GUI build:

```powershell
& ..\NeoShared\scripts\install-wxwidgets.ps1 `
  -VcpkgRoot C:\vcpkg `
  -Triplet x64-windows-static `
  -CleanAfterBuild

.\scripts\build.ps1 `
  -Clean `
  -Wx ON `
  -RequireWx ON `
  -NeoSharedRoot ..\NeoShared `
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

## Shared game directories

**File > Open Game Directory** and **Manage Game Directories...** show only Jade Empire installations. The underlying installation registry remains shared with the other Neo tools.
