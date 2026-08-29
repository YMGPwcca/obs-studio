from pathlib import Path
import subprocess

Q = "4c8b616ca2115970af3e1e4000b162416be32dac"


def git_show(path: str) -> str:
    return subprocess.check_output(["git", "show", f"{Q}:{path}"], text=True, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one reviewed anchor, found {count}")
    p.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


# Quarantine protocol has the Task-11 namespace wiring. Relative to the accepted
# Task-10 protocol, the corrective Task-10 delta is only the pre-lock callback
# retirement barrier, which is restored immediately below.
Path("engine/protocol_v2.cpp").write_text(git_show("engine/protocol_v2.cpp"), encoding="utf-8", newline="\n")
replace_once(
    "engine/protocol_v2.cpp",
    "\tif (method_is_runtime(method) && method_is_mutating(method))\n\t\tcapture.emplace(engine, runtime_result);\n\tif (method_is_mutating(method))\n\t\tmutation_guard.emplace(revisions.lock_mutation());\n",
    "\tif (method_is_runtime(method) && method_is_mutating(method))\n\t\tcapture.emplace(engine, runtime_result);\n\tif (capture)\n\t\tengine.v2_wait_for_event_capture_callbacks();\n\tif (method_is_mutating(method))\n\t\tmutation_guard.emplace(revisions.lock_mutation());\n",
)

# Reuse the deterministic Task-11 integration driver, then extend it to prove
# source.duplicate makes copied filters visible at the source command revision.
Path(".github/scripts/engine-protocol-v2-task11.ps1").write_text(
    git_show(".github/scripts/engine-protocol-v2-task11.ps1"), encoding="utf-8", newline="\n"
)
replace_once(
    ".github/scripts/engine-protocol-v2-task11.ps1",
    """    $SourceRemove = Send-V2Request @{ op = 'request'; id = 'task11.sourceRemove'; method = 'source.remove'; ifRevision = 14; params = @{ source = '1' } }\n    Assert-Ok $SourceRemove 15 'source.remove with filters'\n    Read-Event 'filter.removed' 15 '5' '1' | Out-Null\n    Read-Event 'filter.removed' 15 '2' '1' | Out-Null\n    Read-Event 'filter.removed' 15 '4' '1' | Out-Null\n    Read-Event 'source.removed' 15 '' '1' | Out-Null\n\n    $StaleFilter = Send-V2Request @{ op = 'request'; id = 'task11.staleFilter'; method = 'filter.get'; params = @{ filter = '2' } }\n    Assert-Error $StaleFilter 'not_found' 15 'removed filter handle'\n\n    $Close = Send-V2Request @{ op = 'request'; id = 'task11.close'; method = 'session.close'; ifRevision = 15; params = @{} }\n    Assert-Ok $Close 16 'session.close'\n""",
    """    $SourceDuplicate = Send-V2Request @{\n        op = 'request'; id = 'task11.sourceDuplicate'; method = 'source.duplicate'; ifRevision = 14\n        params = @{ source = '1'; name = 'task11-parent-copy' }\n    }\n    Assert-Ok $SourceDuplicate 15 'source.duplicate with filters'\n    $DuplicateSource = [string]$SourceDuplicate.data.source\n    if (-not $DuplicateSource -or $DuplicateSource -eq '1') {\n        Fail 'source.duplicate did not return a new source handle.'\n    }\n    Read-Event 'source.created' 15 '' $DuplicateSource | Out-Null\n    $CopiedFilterHandles = [System.Collections.Generic.List[string]]::new()\n    for ($Index = 0; $Index -lt 3; $Index++) {\n        $Copied = Read-Event 'filter.created' 15 '' $DuplicateSource\n        $CopiedFilterHandles.Add([string]$Copied.data.filter)\n    }\n    if (($CopiedFilterHandles | Select-Object -Unique).Count -ne 3) {\n        Fail 'source.duplicate did not expose three unique copied filter handles.'\n    }\n    $DuplicateList = Send-V2Request @{ op = 'request'; id = 'task11.duplicateList'; method = 'filter.list'; params = @{ source = $DuplicateSource } }\n    Assert-Ok $DuplicateList 15 'filter.list on duplicated source'\n    if ([int]$DuplicateList.data.count -ne 3) {\n        Fail 'duplicated source did not retain exactly three filters.'\n    }\n    foreach ($CopiedHandle in $CopiedFilterHandles) {\n        if (@($DuplicateList.data.filters | Where-Object { [string]$_.filter -eq $CopiedHandle }).Count -ne 1) {\n            Fail \"copied filter handle $CopiedHandle was not stable in filter.list.\"\n        }\n    }\n\n    $SourceRemove = Send-V2Request @{ op = 'request'; id = 'task11.sourceRemove'; method = 'source.remove'; ifRevision = 15; params = @{ source = '1' } }\n    Assert-Ok $SourceRemove 16 'source.remove with filters'\n    Read-Event 'filter.removed' 16 '5' '1' | Out-Null\n    Read-Event 'filter.removed' 16 '2' '1' | Out-Null\n    Read-Event 'filter.removed' 16 '4' '1' | Out-Null\n    Read-Event 'source.removed' 16 '' '1' | Out-Null\n\n    $StaleFilter = Send-V2Request @{ op = 'request'; id = 'task11.staleFilter'; method = 'filter.get'; params = @{ filter = '2' } }\n    Assert-Error $StaleFilter 'not_found' 16 'removed filter handle'\n\n    $DuplicateSourceRemove = Send-V2Request @{\n        op = 'request'; id = 'task11.duplicateSourceRemove'; method = 'source.remove'; ifRevision = 16\n        params = @{ source = $DuplicateSource }\n    }\n    Assert-Ok $DuplicateSourceRemove 17 'remove duplicated source with copied filters'\n    foreach ($CopiedHandle in $CopiedFilterHandles) {\n        Read-Event 'filter.removed' 17 $CopiedHandle $DuplicateSource | Out-Null\n    }\n    Read-Event 'source.removed' 17 '' $DuplicateSource | Out-Null\n\n    $Close = Send-V2Request @{ op = 'request'; id = 'task11.close'; method = 'session.close'; ifRevision = 17; params = @{} }\n    Assert-Ok $Close 18 'session.close'\n""",
)

# source.duplicate in the quarantined implementation registered copied filter
# handles but did not expose their lifecycle. Register them into the active
# command result so source.created remains first and copied filter.created events
# follow at the same revision.
replace_once(
    "engine/runtime_source_v2.cpp",
    "v2_filter_register_source_filters(duplicate_handle, duplicate, nullptr, handle);",
    "v2_filter_register_source_filters(duplicate_handle, duplicate, &result);",
)

# Permanent observer generation is the only request-settlement wakeup. Do not
# race-read weak-ref state from a second mutex domain in the wait predicate.
replace_once(
    "engine/runtime_filter_v2.cpp",
    """\t\t\tif (!observer->update_cv.wait_until(lock, deadline, [&] {\n\t\t\t\t    return observer->update_generation != observed_generation || !observer->weak;\n\t\t\t    }))\n\t\t\t\tbreak;\n\t\t\tif (!observer->weak)\n\t\t\t\tbreak;\n\t\t\tobserved_generation = observer->update_generation;\n""",
    """\t\t\tif (!observer->update_cv.wait_until(\n\t\t\t\t    lock, deadline, [&] { return observer->update_generation != observed_generation; }))\n\t\t\t\tbreak;\n\t\t\tobserved_generation = observer->update_generation;\n""",
)

# Remove temporary copied-filter helper scaffolding now that the normal registry
# writes copied lifecycle directly into RuntimeV2Result.
replace_once(
    "engine/runtime.hpp",
    """\tvoid v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source, RuntimeV2Result *result = nullptr);\n\tvoid v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source, RuntimeV2Result *result,\n\t\t\t\t\t       uint64_t duplicate_of);\n\tvoid v2_filter_emit_source_created_filters(RuntimeV2Result &result);\n""",
    "\tvoid v2_filter_register_source_filters(uint64_t source_id, obs_source_t *source, RuntimeV2Result *result = nullptr);\n",
)
replace_once(
    "engine/CMakeLists.txt",
    "    runtime_filter_source_duplicate_v2.cpp\n",
    "",
)

for path in (
    "engine/runtime_filter_source_duplicate_v2.cpp",
    "engine/TASK11_INTEGRATION_NOTE.tmp",
    ".github/TASK11_STAGING_DO_NOT_USE.tmp",
    ".github/workflows/task11-one-shot-integrator.yaml",
    ".github/scripts/task11-integrate.py",
):
    Path(path).unlink(missing_ok=True)
