# NeoQST

[![CI](https://github.com/vrifftech/NeoQST/actions/workflows/ci.yml/badge.svg)](https://github.com/vrifftech/NeoQST/actions/workflows/ci.yml)

NeoQST is a purpose-built editor for **Jade Empire quest files** (`.qst` and `.qst2`). It lets you edit a quest as a set of objectives and progression rules instead of working with the underlying GFF structure by hand.

In ordinary use, you only need to understand three things:

```text
Quest
|-- Tasks       Individual objectives or quest states
`-- Task groups Rules that decide when the quest moves forward
```

NeoQST builds against the separate sibling [NeoShared](https://github.com/vrifftech/NeoShared) repository.

## Contents

- [Quick start](#quick-start)
- [How a Jade Empire quest works](#how-a-jade-empire-quest-works)
- [How quest progression happens](#how-quest-progression-happens)
- [Worked example](#worked-example)
- [Using the editor](#using-the-editor)
- [Identifiers and list positions](#identifiers-and-list-positions)
- [Runtime-state fields](#runtime-state-fields)
- [QST and QST2](#qst-and-qst2)
- [Limits and validation](#limits-and-validation)
- [Shared game directories](#shared-game-directories)
- [Import, export, and patching](#import-export-and-patching)
- [Command-line utility](#command-line-utility)
- [Building NeoQST](#building-neoqst)
- [Raw field reference](#raw-field-reference)

## Quick start

1. Open a `.qst` or `.qst2` file.
2. Optionally load Jade Empire's TLK file so NeoQST can show the text behind each StrRef.
3. Edit the quest title and description.
4. Add or edit tasks on the **Tasks** tab.
5. Put those tasks into progression groups on the **Task groups** tab.
6. Choose what happens when each task completes its group.
7. Save the QST.

If the QST is stored inside a `.mod`, `.erf`, or another game archive, extract it with NeoERF first and place the edited file back into the archive afterward.

For a normal quest mod, leave **Store runtime quest state fields** turned off.

## How a Jade Empire quest works

A quest is not just a flat checklist. It is a small progression system.

### Quest metadata

The quest itself has:

- A quest type.
- A localized title.
- A localized overall description.
- A list of tasks.
- A list of task groups.

### Tasks

A **task** is an objective or a state within the quest, such as:

```text
Speak to the guard
Find the stolen key
Return to the merchant
```

Each task can have:

- A short name.
- Journal text.
- An optional summary shown before activation.
- An optional summary shown after completion.
- A unique numeric identifier for scripts and runtime lookups.
- A completed/not-completed state.
- Journal-notification settings.
- A rule saying what should happen when this task causes its group to complete.

### Task groups

A **task group** collects related tasks and decides when that part of the quest is finished.

A group can complete when:

```text
Any selected task completes
```

or:

```text
All selected tasks complete
```

A group can also run an optional script when it completes.

A task may belong to only one group. Jade's runtime stores one owning-group reference for each task, so assigning the same task to several groups would be ambiguous.

## How quest progression happens

The normal flow is:

```text
A task is completed
        v
Jade checks that task's group
        v
The group satisfies its Any/All completion rule
        v
The group's On-complete script runs, if one is set
        v
The completing task's Next action is applied
```

The task's **When group completes** choice can be:

```text
No transition
Complete and deactivate the quest
Activate another task group
```

This setting belongs to the task rather than the group because an **Any selected task** group can branch differently depending on which task completed it.

For an **All selected tasks** group, whichever task finishes last supplies the next transition. For predictable behavior, give every task in that group the same next action unless you deliberately want the last-completed task to control the branch.

## Worked example

Suppose the quest is **Missing Student**.

### Group 0: Investigate

Completion rule: **All selected tasks must complete**

```text
Task 0: Speak to the teacher
Task 1: Search the dormitory
```

Both tasks should use:

```text
When group completes: Activate Group 1
```

### Group 1: Confront the suspect

Completion rule: **Any selected task completes the group**

```text
Task 2: Arrest the suspect
    Next action: Complete and deactivate the quest

Task 3: Let the suspect escape
    Next action: Activate Group 2
```

### Group 2: Report the failure

Completion rule: **All selected tasks must complete**

```text
Task 4: Return to the teacher
    Next action: Complete and deactivate the quest
```

This creates a branch without exposing the underlying list indexes to the user.

## Using the editor

### Document area

#### QST file

Shows the file currently being edited. Use **Open**, **Save**, or **Save As** to manage the document.

#### Jade TLK

A QST normally stores text as a **StrRef**, which is a number pointing to a string in Jade Empire's TLK file. Loading the TLK lets NeoQST display the actual quest and task text beside each StrRef.

Loading a TLK is optional. You can still edit the numeric StrRefs without it.

### Quest metadata

#### Quest type

Jade uses two quest categories, stored as values `0` and `1`. The runtime executable does not contain reliable human-readable names for those categories, so NeoQST labels them **Type 0** and **Type 1** instead of guessing.

When editing an existing quest, preserve its current type unless you know the intended category.

#### Quest name StrRef

The TLK string number used for the quest title.

#### Resolved name

A read-only preview of that title from the loaded TLK.

#### Description StrRef

The TLK string number used for the overall quest description.

#### Resolved description

A read-only preview of the description from the loaded TLK.

#### Store runtime quest state fields

This is an advanced option for resources that contain the state of a quest instance from a running or saved game.

For ordinary authored quest definitions, leave it **off**.

Turning it on exposes fields such as:

- Whether the quest is active.
- Whether the quest is complete.
- Whether the journal was recently updated.
- The quest's resource reference.
- The engine's timestamp pair.

These values describe progress, not the quest's authored content. NeoQST preserves them automatically when an opened file already contains them.

### Tasks tab

The left side lists all tasks. The right side edits the selected task.

#### Identifier

A stable numeric ID used by scripts and runtime lookups.

Identifiers must be unique among tasks. They are allowed to range from `0` through `32767`.

An identifier is **not** the task's row number. Moving or deleting tasks may change their list positions without changing their identifiers.

#### Task name StrRef

The TLK string number for the task's short name.

#### Resolved task name

A read-only preview of the task name from the loaded TLK.

#### Task summary StrRef

The TLK string number for the task's main journal/objective text.

#### Resolved summary

A read-only preview of the task summary.

#### Summary before activation

An optional TLK reference for additional quest-summary text associated with the state before this task becomes active.

#### Summary after completion

An optional TLK reference for additional quest-summary text associated with the state after this task completes.

These two summary fields are optional. Their exact presentation depends on the quest flow and game UI.

#### Complete

Marks the task complete in the current file.

For a newly authored static quest, this is normally unchecked. It is more commonly meaningful in serialized quest-state data.

#### When group completes

Chooses the progression that occurs when this task is the task that completes its group:

- **No transition** - do not activate another group.
- **Complete and deactivate quest** - finish the entire quest.
- **Group N** - activate the selected task-group list position.

NeoQST shows the destination group's identifier beside its position to make the choice easier to recognize.

#### Journal notifications

- **On activation** - request a journal notification when the task becomes active.
- **On completion** - request a journal notification when the task completes.

### Task groups tab

The left side lists all groups. The right side edits the selected group.

#### Identifier

A stable numeric ID used by scripts and runtime lookups.

Group identifiers must be unique among groups and must be between `0` and `32767`.

#### Active

Marks the group active in the current file. An active group is eligible to track the completion of its member tasks.

For an ordinary quest definition, this can mark the group that should begin active. When editing serialized state, it records which stage is currently active.

#### Completion rule

Choose one:

- **Any selected task completes the group** - useful for alternatives or branches.
- **All selected tasks must complete** - useful for a checklist of required objectives.

#### On-complete script

An optional Jade script ResRef, up to 16 characters. Jade runs this script when the group completes.

#### Tasks in this group

Check the tasks that belong to the group.

NeoQST prevents the same task from being assigned to more than one group because Jade stores only one owning group for each task.

## Identifiers and list positions

Jade QST files use both **identifiers** and **list positions**. They are not interchangeable.

A simple analogy is:

```text
Identifier    A person's permanent employee number
List position Their current seat number in a row
```

### Identifiers

Task and group identifiers are stable values used by scripts and runtime lookup functions.

Example:

```text
Task at position 0 has Identifier 100
Task at position 1 has Identifier 900
```

Moving those tasks can change their positions, but their identifiers remain `100` and `900`.

### List positions

Group membership and next-group transitions use zero-based list positions.

Example:

```text
TaskGroupList position 0 has Identifier 50
TaskGroupList position 1 has Identifier 900
```

A task storing:

```text
NextTaskGroup = 1
```

means:

```text
Activate the group at list position 1
```

It does not mean:

```text
Activate the group whose Identifier is 1
```

NeoQST hides most raw list-position bookkeeping and repairs references when tasks or groups are deleted.

## Runtime-state fields

Most mod authors can ignore this section.

A normal QST is a **quest definition**: it describes what the quest is and how it progresses.

A serialized runtime-state resource additionally describes the current state of one quest instance:

| Raw field | Plain-language meaning |
|---|---|
| `QuestResRef` | Resource name of the quest instance |
| `QuestActive` | Whether the quest is active |
| `QuestComplete` | Whether the quest is complete |
| `QuestUpdated` | Whether its journal state was recently changed |
| `TimeHi` / `TimeLo` | Two halves of an engine timestamp/state value |

NeoQST does not add these fields to an ordinary static quest unless **Store runtime quest state fields** is enabled.

Recommended policy:

- Leave the option off for normal quest modding.
- Preserve it when editing a file that already contains runtime state.
- Do not manually change `TimeHi` or `TimeLo` unless you understand the saved-game state that produced them.

## QST and QST2

`.qst` and `.qst2` are filename-extension aliases for the same native format.

Both contain:

```text
QST V3.2
```

NeoQST:

- Opens either extension.
- Saves either extension.
- Preserves the extension of an opened file.
- Uses `.qst` for a new document by default.

There is no separate QST2 schema in the files examined. Two supplied same-quest `.qst` and `.qst2` files were byte-for-byte identical.

## Limits and validation

Jade narrows several QST values internally, so NeoQST enforces the safe runtime limits:

```text
Maximum tasks:             128
Maximum task groups:       128
Task/group Identifier:     0-32767
Next group position:       0-127
No transition sentinel:    -1
Complete quest sentinel:   -2
```

NeoQST also prevents:

- Duplicate task identifiers.
- Duplicate group identifiers.
- Invalid task references.
- Assigning one task to several groups.
- Saving a malformed QST structure.

Deleting a task repairs group membership positions. Deleting a group repairs task next-group positions. Changing an identifier does not rewrite list-position references.

### Common mistakes

- **Treating QST2 as a companion file.** It is only another extension for the same QST format.
- **Using a group identifier as a next-group number.** Choose the destination from NeoQST's dropdown; the file uses the group's list position internally.
- **Putting one task in several groups.** Jade stores only one owning group for each task.
- **Enabling runtime state for an ordinary mod resource.** Leave it off unless the file intentionally carries live or saved quest progress.
- **Checking Complete on a new task.** This makes the task begin in an already-completed state.
- **Changing a StringRef and expecting the TLK wording to change.** A StringRef only points to the talk table; edit the text itself in NeoTLK.

## Shared game directories

**File > Open Game Directory** and **Manage Game Directories...** show only saved Jade Empire installations.

Selecting an installation opens NeoQST's normal file dialog in that directory. The saved installation registry is shared with the other Neo tools, but NeoQST filters it to Jade Empire.

## Import, export, and patching

NeoQST works natively with `.qst` and `.qst2` and can also import or export:

```text
XML
JSON
```

The command-line utility can also generate TSLPatcher/HoloPatcher-oriented differences between an original and modified QST.

XML and JSON are useful for inspection, source control, or scripted transformations. Native QST is the format used by the game.

## Command-line utility

```text
neoqst-cli <quest.qst|quest.qst2> [dialog.tlk]
neoqst-cli --info <quest.qst|quest.qst2>
neoqst-cli --new <output.qst|output.qst2>
neoqst-cli --roundtrip <input.qst|qst2> <output.qst|qst2>
neoqst-cli --search <quest.qst|qst2> <term>
neoqst-cli --export <quest.qst|qst2> <xml|json> <output>
neoqst-cli --import <input.xml|json|qst|qst2> <output.qst|qst2> <xml|json|native|auto>
```

For the full patch-generation syntax:

```sh
neoqst-cli --help
```

## Building NeoQST

Clone NeoQST and NeoShared beside one another:

```text
workspace/
|-- NeoShared/
`-- NeoQST/
```

### Linux GUI build

Install CMake, a C++ compiler, and the wxWidgets development package. On Debian or Ubuntu, the wxWidgets package is normally `libwxgtk3.2-dev`.

```sh
bash ./scripts/build.sh \
  --wx ON \
  --require-wx ON \
  --jobs "$(nproc)" \
  --clean
```

### Linux CLI/core-only build

```sh
bash ./scripts/build.sh \
  --wx OFF \
  --jobs "$(nproc)" \
  --clean
```

### Windows GUI build

Install the pinned wxWidgets package from NeoShared, then build NeoQST:

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

## Raw field reference

This section maps NeoQST's user-facing controls to the field names stored in the QST GFF. Normal users do not need to edit these names directly.

### Quest

| User-facing concept | Raw QST field |
|---|---|
| Quest type | `QuestType` |
| Quest title | `QuestName` |
| Quest description | `QuestDescription` |
| Tasks | `TaskList` |
| Task groups | `TaskGroupList` |

### Task

| User-facing concept | Raw QST field |
|---|---|
| Task name | `TaskName` |
| Main journal text | `TaskSummary` |
| Summary before activation | `QuestSummaryPre` |
| Summary after completion | `QuestSummaryPost` |
| Stable task ID | `Identifier` |
| Completion state | `Complete` |
| Next progression | `NextTaskGroup` |
| Activation notification | `NotifyActive` |
| Completion notification | `NotifyComplete` |

### Task group

| User-facing concept | Raw QST field |
|---|---|
| Stable group ID | `Identifier` |
| Member-task positions | `TaskIndexList` |
| Active state | `Active` |
| Any/All completion rule | `ANDGroup` |
| Completion script | `OnComplete` |

### Optional runtime state

| User-facing concept | Raw QST field |
|---|---|
| Quest resource reference | `QuestResRef` |
| Active state | `QuestActive` |
| Complete state | `QuestComplete` |
| Recently updated state | `QuestUpdated` |
| Engine timestamp, high half | `TimeHi` |
| Engine timestamp, low half | `TimeLo` |
