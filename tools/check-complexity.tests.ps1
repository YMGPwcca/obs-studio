[CmdletBinding()]
param(
    [string] $LizardPythonPath = $env:LIZARD_PATH
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$checkerPath = Join-Path $repoRoot 'tools/check-complexity.ps1'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('libobs-complexity-selftest-' + [Guid]::NewGuid().ToString('N'))

function Write-FixtureText {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $Contents
    )

    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [IO.File]::WriteAllText($Path, $Contents, [Text.UTF8Encoding]::new($false))
}

function Trust-FixtureBaseline {
    param([Parameter(Mandatory = $true)] [object] $Scenario)

    $hash = ((Invoke-FixtureGit $Scenario.Directory @('hash-object', '--path', 'complexity-after.json', '--', 'complexity-after.json')) | Select-Object -First 1).ToString().Trim()
    $checkerPath = Join-Path $Scenario.Directory 'tools/check-complexity.ps1'
    $checker = Get-Content -LiteralPath $checkerPath -Raw
    $match = [regex]::Match($checker, '\$acceptedBaselineBlob = ''[0-9a-f]{40}''')
    if (-not $match.Success) {
        throw 'Fixture checker did not contain the accepted-baseline blob declaration.'
    }
    $replacement = "`$acceptedBaselineBlob = '$hash'"
    $updatedChecker = $checker.Replace($match.Value, $replacement)
    Write-FixtureText $checkerPath $updatedChecker
}

function Invoke-FixtureGit {
    param(
        [Parameter(Mandatory = $true)] [string] $Directory,
        [Parameter(Mandatory = $true)] [string[]] $Arguments
    )

    $output = @(& git -C $Directory @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "git $($Arguments -join ' ') failed in '$Directory' with exit code $exitCode.`n$($output -join "`n")"
    }
    return @($output)
}

function Get-FixtureHead {
    param([Parameter(Mandatory = $true)] [string] $Directory)

    return ((Invoke-FixtureGit $Directory @('rev-parse', 'HEAD') | Select-Object -First 1).ToString().Trim())
}

function Invoke-ComplexityChecker {
    param(
        [Parameter(Mandatory = $true)] [object] $Scenario,
        [Parameter(Mandatory = $true)] [ValidateSet('Baseline', 'After', 'Check')] [string] $Mode
    )

    $directory = [string] $Scenario.Directory
    $reportStem = $Mode.ToLowerInvariant()
    $arguments = @(
        '-NoProfile', '-File', (Join-Path $directory 'tools/check-complexity.ps1'),
        '-Mode', $Mode, '-AcceptedRef', ([string] $Scenario.AcceptedRef),
        '-BeforePath', (Join-Path $directory 'complexity-before.json'),
        '-BaselinePath', (Join-Path $directory 'complexity-after.json'),
        '-AllowlistPath', (Join-Path $directory 'complexity-exceptions.json'),
        '-InventoryPath', (Join-Path $directory 'complexity-ownership-inventory.json'),
        '-JsonPath', (Join-Path $directory ("complexity-$reportStem.json")),
        '-MarkdownPath', (Join-Path $directory ("complexity-$reportStem.md"))
    )
    if ($LizardPythonPath) {
        $arguments += @('-LizardPythonPath', $LizardPythonPath)
    }
    $output = @(& pwsh @arguments 2>&1)
    return [pscustomobject]@{
        ExitCode        = [int] $LASTEXITCODE
        Output          = ($output -join "`n")
        ReportDirectory = $directory
    }
}

function Assert-CheckerPass {
    param(
        [Parameter(Mandatory = $true)] [object] $Result,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    if ($Result.ExitCode -ne 0) {
        throw "$Label unexpectedly failed (exit $($Result.ExitCode)).`n$($Result.Output)"
    }
    Write-Output "${Label}: PASS (exit 0)"
}

function Assert-CheckerFailure {
    param(
        [Parameter(Mandatory = $true)] [object] $Result,
        [Parameter(Mandatory = $true)] [string] $Label,
        [Parameter(Mandatory = $true)] [string] $Pattern
    )

    $normalizedOutput = $Result.Output -replace '\x1B\[[0-?]*[ -/]*[@-~]', ''
    $matchOutput = $normalizedOutput -replace '\s+', ' '
    if ($Result.ExitCode -eq 0 -or $matchOutput -notmatch $Pattern) {
        throw "$Label did not fail as expected (exit $($Result.ExitCode), pattern '$Pattern').`n$normalizedOutput"
    }
    $compactOutput = ($normalizedOutput -replace '\s+', ' ').Trim()
    $match = [regex]::Match($compactOutput, $Pattern)
    $detail = if ($match.Success) { $match.Value } else { $compactOutput }
    if ($detail.Length -gt 320) {
        $detail = $detail.Substring(0, 320) + '...'
    }
    Write-Output "${Label}: expected FAIL (exit $($Result.ExitCode)): $detail"
}

function Get-ReportFunctionMetric {
    param(
        [Parameter(Mandatory = $true)] [string] $Directory,
        [Parameter(Mandatory = $true)] [string] $ReportName,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Function
    )

    $reportPath = Join-Path $Directory $ReportName
    if (-not (Test-Path -LiteralPath $reportPath)) {
        throw "Report '$ReportName' was not produced in '$Directory'."
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $metric = @($report.functions | Where-Object {
            $_.scopeKind -eq 'function' -and $_.file -eq $Path -and $_.function -eq $Function
        }) | Select-Object -First 1
    if ($null -eq $metric) {
        throw "Function '$Path::$Function' was not present in '$ReportName'."
    }
    return $metric
}

function Assert-FunctionMeasured {
    param(
        [Parameter(Mandatory = $true)] [object] $Result,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Function,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    $metric = Get-ReportFunctionMetric ([string] $Result.ReportDirectory) 'complexity-check.json' $Path $Function
    Write-Output "${Label}: measured $Path::$Function CC $($metric.cyclomaticComplexity)"
}

function Get-ReportScriptBodyMetric {
    param(
        [Parameter(Mandatory = $true)] [string] $Directory,
        [Parameter(Mandatory = $true)] [string] $ReportName,
        [Parameter(Mandatory = $true)] [string] $Path
    )

    $reportPath = Join-Path $Directory $ReportName
    if (-not (Test-Path -LiteralPath $reportPath)) {
        throw "Report '$ReportName' was not produced in '$Directory'."
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    return @($report.functions | Where-Object {
            $_.scopeKind -eq 'script-body' -and $_.file -eq $Path
        }) | Select-Object -First 1
}

function Assert-ScriptBodyMeasured {
    param(
        [Parameter(Mandatory = $true)] [object] $Result,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    $reportPath = Join-Path ([string] $Result.ReportDirectory) 'complexity-check.json'
    if (-not (Test-Path -LiteralPath $reportPath)) {
        throw "${Label}: script-body report was not produced.`n$($Result.Output)"
    }
    $metric = Get-ReportScriptBodyMetric ([string] $Result.ReportDirectory) 'complexity-check.json' $Path
    if ($null -eq $metric) {
        throw "${Label}: script-body '$Path' was not measured.`n$($Result.Output)"
    }
    Write-Output "${Label}: measured $Path::<script-body> CC $($metric.cyclomaticComplexity)"
}

function New-IfChain {
    param(
        [Parameter(Mandatory = $true)] [string] $Name,
        [Parameter(Mandatory = $true)] [string] $Prefix,
        [Parameter(Mandatory = $true)] [int] $Count
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("${Prefix} $Name(int value)")
    $lines.Add('{')
    $lines.Add('    int result = value;')
    for ($index = 0; $index -lt $Count; $index++) {
        $lines.Add("    if (value > $index) { result++; }")
    }
    $lines.Add('    return result;')
    $lines.Add('}')
    return $lines -join "`n"
}

function New-PowerShellTopLevelIfChain {
    param([Parameter(Mandatory = $true)] [int] $Count)

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('$value = 0')
    for ($index = 0; $index -lt $Count; $index++) {
        $lines.Add("if (`$value -eq $index) { `$value++ }")
    }
    $lines.Add('$value')
    return $lines -join "`n"
}

function New-WorkflowDocument {
    param(
        [Parameter(Mandatory = $true)] [string] $Shell,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $Run
    )

    $indentedRun = @($Run -split "`r?`n" | ForEach-Object { '          ' + $_ }) -join "`n"
    return @"
name: Fixture workflow
on:
  workflow_dispatch:
jobs:
  fixture:
    runs-on: ubuntu-latest
    steps:
      - name: Fixture executable block
        shell: $Shell
        run: |
$indentedRun
"@
}

function New-Fixture {
    param(
        [Parameter(Mandatory = $true)] [string] $Name,
        [string] $AcceptedPath = 'src/accepted.cpp',
        [string] $AcceptedContent = "int accepted_function(int value) {`n    if (value > 0) { return value; }`n    return 0;`n}`n",
        [string] $BasePath = '',
        [string] $BaseContent = ''
    )

    $directory = Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path (Join-Path $directory 'tools') -Force | Out-Null
    Copy-Item -LiteralPath $checkerPath -Destination (Join-Path $directory 'tools/check-complexity.ps1')
    Write-FixtureText (Join-Path $directory 'README.md') "fixture $Name`n"
    Write-FixtureText (Join-Path $directory 'complexity-exceptions.json') "[]`n"
    Write-FixtureText (Join-Path $directory 'complexity-identity-migrations.json') "[]`n"
    if ($BasePath) {
        Write-FixtureText (Join-Path $directory $BasePath) $BaseContent
    }
    Invoke-FixtureGit $directory @('init', '-b', 'master') | Out-Null
    Invoke-FixtureGit $directory @('config', 'user.name', 'Fixture Upstream') | Out-Null
    Invoke-FixtureGit $directory @('config', 'user.email', 'fixture-upstream@example.invalid') | Out-Null
    Invoke-FixtureGit $directory @('add', '--all') | Out-Null
    Invoke-FixtureGit $directory @('commit', '-m', 'fixture upstream base') | Out-Null
    $baseRef = Get-FixtureHead $directory
    Invoke-FixtureGit $directory @('update-ref', 'refs/remotes/origin/master', $baseRef) | Out-Null
    Invoke-FixtureGit $directory @('checkout', '-b', 'fixture-candidate') | Out-Null
    Invoke-FixtureGit $directory @('config', 'user.name', 'YMGPwcca') | Out-Null
    Invoke-FixtureGit $directory @('config', 'user.email', '37042810+YMGPwcca@users.noreply.github.com') | Out-Null
    $acceptedFullPath = Join-Path $directory $AcceptedPath
    Write-FixtureText $acceptedFullPath $AcceptedContent
    Invoke-FixtureGit $directory @('add', '--', $AcceptedPath) | Out-Null
    Invoke-FixtureGit $directory @('commit', '-m', 'fixture accepted project source') | Out-Null
    return [pscustomobject]@{
        Name        = $Name
        Directory   = $directory
        AcceptedRef = Get-FixtureHead $directory
    }
}

function Prepare-FixtureBaseline {
    param([Parameter(Mandatory = $true)] [object] $Scenario)

    Assert-CheckerPass (Invoke-ComplexityChecker $Scenario 'Baseline') "$($Scenario.Name) baseline"
    $afterResult = Invoke-ComplexityChecker $Scenario 'After'
    Assert-CheckerPass $afterResult "$($Scenario.Name) accepted after"
    Trust-FixtureBaseline $Scenario
}

function Commit-FixtureFile {
    param(
        [Parameter(Mandatory = $true)] [object] $Scenario,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $Contents,
        [Parameter(Mandatory = $true)] [string] $Message
    )

    Write-FixtureText (Join-Path $Scenario.Directory $Path) $Contents
    Invoke-FixtureGit $Scenario.Directory @('add', '--', $Path) | Out-Null
    Invoke-FixtureGit $Scenario.Directory @('commit', '-m', $Message) | Out-Null
}

function Commit-FixtureRename {
    param(
        [Parameter(Mandatory = $true)] [object] $Scenario,
        [Parameter(Mandatory = $true)] [string] $OldPath,
        [Parameter(Mandatory = $true)] [string] $NewPath,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $Contents,
        [Parameter(Mandatory = $true)] [string] $Message
    )

    Invoke-FixtureGit $Scenario.Directory @('mv', '--', $OldPath, $NewPath) | Out-Null
    Write-FixtureText (Join-Path $Scenario.Directory $NewPath) $Contents
    Invoke-FixtureGit $Scenario.Directory @('add', '--all') | Out-Null
    Invoke-FixtureGit $Scenario.Directory @('commit', '-m', $Message) | Out-Null
}

function New-MigrationRecord {
    param(
        [Parameter(Mandatory = $true)] [object] $BaselineMetric,
        [Parameter(Mandatory = $true)] [object] $CurrentMetric
    )

    return [pscustomobject]@{
        baselineKey = [string] $BaselineMetric.key
        current     = [pscustomobject]@{
            language  = [string] $CurrentMetric.language
            scopeKind = [string] $CurrentMetric.scopeKind
            file      = [string] $CurrentMetric.file
            function  = [string] $CurrentMetric.function
            signature = [string] $CurrentMetric.signature
        }
    }
}

function New-MigrationRecordValues {
    param(
        [Parameter(Mandatory = $true)] [string] $BaselineKey,
        [Parameter(Mandatory = $true)] [string] $Language,
        [Parameter(Mandatory = $true)] [string] $ScopeKind,
        [Parameter(Mandatory = $true)] [string] $File,
        [Parameter(Mandatory = $true)] [string] $Function,
        [Parameter(Mandatory = $true)] [string] $Signature
    )

    return [pscustomobject]@{
        baselineKey = $BaselineKey
        current     = [pscustomobject]@{
            language  = $Language
            scopeKind = $ScopeKind
            file      = $File
            function  = $Function
            signature = $Signature
        }
    }
}

function Write-MigrationDocument {
    param(
        [Parameter(Mandatory = $true)] [object] $Scenario,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Entries
    )

    $items = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in $Entries) {
        $items.Add(($entry | ConvertTo-Json -Depth 5))
    }
    $json = "[`n" + ($items -join ",`n") + "`n]`n"
    Write-FixtureText (Join-Path $Scenario.Directory 'complexity-identity-migrations.json') $json
}

function New-DuplicateMigrationDocument {
    param(
        [Parameter(Mandatory = $true)] [string] $BaselineKey,
        [Parameter(Mandatory = $true)] [object] $CurrentMetric
    )

    return @"
[
  {
    "baselineKey": "$BaselineKey",
    "baselineKey": "$BaselineKey",
    "current": {
      "language": "$($CurrentMetric.language)",
      "scopeKind": "$($CurrentMetric.scopeKind)",
      "file": "$($CurrentMetric.file)",
      "function": "$($CurrentMetric.function)",
      "signature": "$($CurrentMetric.signature)"
    }
  }
]
"@
}

try {
    New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

    $caseA = New-Fixture 'case-a-new-cpp-bad'
    Prepare-FixtureBaseline $caseA
    Commit-FixtureFile $caseA 'engine/runtime_scene_v2.cpp' (New-IfChain 'runtime_scene_bad' 'int' 11) 'fixture add bad cpp'
    $caseAResult = Invoke-ComplexityChecker $caseA 'Check'
    Assert-FunctionMeasured $caseAResult 'engine/runtime_scene_v2.cpp' 'runtime_scene_bad' 'CASE A new C++ file'
    Assert-CheckerFailure $caseAResult 'CASE A new C++ file CC > 10' '(?s)(?:CC > 10:.*runtime_scene_bad|New function exceeds CC 10:.*runtime_scene_bad)'

    $caseB = New-Fixture 'case-b-new-cpp-good'
    Prepare-FixtureBaseline $caseB
    Commit-FixtureFile $caseB 'engine/runtime_scene_v2.cpp' "int runtime_scene_good(int value) {`n    if (value > 0) { return value; }`n    return 0;`n}`n" 'fixture add good cpp'
    $caseBResult = Invoke-ComplexityChecker $caseB 'Check'
    Assert-FunctionMeasured $caseBResult 'engine/runtime_scene_v2.cpp' 'runtime_scene_good' 'CASE B new C++ file'
    Assert-CheckerPass $caseBResult 'CASE B new C++ file CC <= 10'

    $caseC = New-Fixture 'case-c-new-header-bad'
    Prepare-FixtureBaseline $caseC
    $header = "#pragma once`n`n" + (New-IfChain 'header_bad' 'inline int' 11) + "`n"
    Commit-FixtureFile $caseC 'engine/new_runtime_scene.hpp' $header 'fixture add bad header implementation'
    $caseCResult = Invoke-ComplexityChecker $caseC 'Check'
    Assert-FunctionMeasured $caseCResult 'engine/new_runtime_scene.hpp' 'header_bad' 'CASE C new header implementation'
    Assert-CheckerFailure $caseCResult 'CASE C new header implementation CC > 10' '(?s)(?:CC > 10:.*header_bad|New function exceeds CC 10:.*header_bad)'

    $caseD = New-Fixture 'case-d-baseline-regression'
    Prepare-FixtureBaseline $caseD
    Commit-FixtureFile $caseD 'src/accepted.cpp' "int accepted_function(int value) {`n    if (value > 0) { return value; }`n    if (value > 1) { return value + 1; }`n    return 0;`n}`n" 'fixture regress accepted function'
    $caseDResult = Invoke-ComplexityChecker $caseD 'Check'
    Assert-FunctionMeasured $caseDResult 'src/accepted.cpp' 'accepted_function' 'CASE D existing hardened function'
    Assert-CheckerFailure $caseDResult 'CASE D existing hardened function baseline regression' '(?s)Complexity increased:.*accepted_function'

    $exceptionSource = New-IfChain 'obs_source_destroy_defer' 'void' 12
    $caseE = New-Fixture 'case-e-exact-exception' 'libobs/obs-source.c' $exceptionSource
    Prepare-FixtureBaseline $caseE
    $afterPath = Join-Path $caseE.Directory 'complexity-after.json'
    $exceptionMetric = @(Get-Content -LiteralPath $afterPath -Raw | ConvertFrom-Json).functions |
        Where-Object { $_.file -eq 'libobs/obs-source.c' -and $_.function -eq 'obs_source_destroy_defer' } |
        Select-Object -First 1
    if ($null -eq $exceptionMetric) {
        throw 'CASE E fixture did not produce the recorded exception function metric.'
    }
    $exception = [ordered]@{
        language    = $exceptionMetric.language
        scopeKind   = $exceptionMetric.scopeKind
        file        = $exceptionMetric.file
        function    = $exceptionMetric.function
        signature   = $exceptionMetric.signature
        baselineKey = $exceptionMetric.key
        measuredCC  = [int] $exceptionMetric.cyclomaticComplexity
        reason      = 'deterministic self-test exact exception'
        dateTask    = 'self-test'
        reviewerNote = 'exact identity only'
    }
    Write-FixtureText (Join-Path $caseE.Directory 'complexity-exceptions.json') ($exception | ConvertTo-Json -Depth 5)
    $caseEExactResult = Invoke-ComplexityChecker $caseE 'Check'
    Assert-FunctionMeasured $caseEExactResult 'libobs/obs-source.c' 'obs_source_destroy_defer' 'CASE E exact exception'
    Assert-CheckerPass $caseEExactResult 'CASE E exact exception identity and CC'
    $similar = New-IfChain 'obs_source_destroy_defer_extra' 'int' 12
    Commit-FixtureFile $caseE 'libobs/obs-source.c' ($exceptionSource + "`n" + $similar + "`n") 'fixture add similarly named unrelated function'
    $caseESimilarResult = Invoke-ComplexityChecker $caseE 'Check'
    Assert-FunctionMeasured $caseESimilarResult 'libobs/obs-source.c' 'obs_source_destroy_defer_extra' 'CASE E unrelated similarly named function'
    Assert-CheckerFailure $caseESimilarResult 'CASE E unrelated similarly named function cannot inherit exception' '(?s)(?:CC > 10:.*obs_source_destroy_defer_extra|New function exceeds CC 10:.*obs_source_destroy_defer_extra)'

    $caseF = New-Fixture 'case-f-new-powershell-bad'
    Prepare-FixtureBaseline $caseF
    $powerShellBad = "function New-PowerShellBad {`n    param([int]`$value)`n    `$result = `$value`n" + (1..11 | ForEach-Object { "    if (`$value -gt $_) { `$result++ }" } | Out-String) + "    return `$result`n}`n"
    Commit-FixtureFile $caseF 'tools/new-complexity-helper.ps1' $powerShellBad 'fixture add bad powershell function'
    $caseFResult = Invoke-ComplexityChecker $caseF 'Check'
    Assert-FunctionMeasured $caseFResult 'tools/new-complexity-helper.ps1' 'New-PowerShellBad' 'CASE F new PowerShell function'
    Assert-CheckerFailure $caseFResult 'CASE F new PowerShell function CC > 10' '(?s)(?:CC > 10:.*New-PowerShellBad|New function exceeds CC 10:.*New-PowerShellBad)'

    $caseH = New-Fixture 'case-h-renamed-file-regression'
    Prepare-FixtureBaseline $caseH
    $renamedContent = "int accepted_function(int value) {`n    if (value > 0) { return value; }`n    if (value > 1) { return value + 1; }`n    return 0;`n}`n"
    Commit-FixtureRename $caseH 'src/accepted.cpp' 'src/renamed.cpp' $renamedContent 'fixture rename and regress function'
    $caseHResult = Invoke-ComplexityChecker $caseH 'Check'
    Assert-FunctionMeasured $caseHResult 'src/renamed.cpp' 'accepted_function' 'CASE H renamed file baseline identity'
    Assert-CheckerFailure $caseHResult 'CASE H renamed file baseline regression' '(?s)Complexity increased:.*accepted_function'

    $caseG = New-Fixture 'case-g-unsupported-script'
    Prepare-FixtureBaseline $caseG
    Commit-FixtureFile $caseG 'tools/new_helper.py' "def new_helper(value):`n    return value`n" 'fixture add unsupported python'
    $caseGResult = Invoke-ComplexityChecker $caseG 'Check'
    if ($caseGResult.Output -match 'PowerShell parser') {
        throw 'CASE G incorrectly fed unsupported Python into the PowerShell parser.'
    }
    Assert-CheckerFailure $caseGResult 'CASE G unsupported executable language fails closed' 'new_helper\.py'

    $caseI = New-Fixture 'case-i-function-rename' 'src/identity.cpp' (New-IfChain 'old_name' 'int' 1)
    Prepare-FixtureBaseline $caseI
    Commit-FixtureFile $caseI 'src/identity.cpp' (New-IfChain 'new_name' 'int' 2) 'fixture rename function without migration'
    $caseINoMigration = Invoke-ComplexityChecker $caseI 'Check'
    Assert-FunctionMeasured $caseINoMigration 'src/identity.cpp' 'new_name' 'CASE I function rename without migration'
    Assert-CheckerFailure $caseINoMigration 'CASE I function rename without migration' 'old_name'
    $oldMetricI = Get-ReportFunctionMetric $caseI.Directory 'complexity-after.json' 'src/identity.cpp' 'old_name'
    $newMetricI = Get-ReportFunctionMetric $caseI.Directory 'complexity-check.json' 'src/identity.cpp' 'new_name'
    Write-MigrationDocument $caseI @(New-MigrationRecord $oldMetricI $newMetricI)
    $caseIWithMigration = Invoke-ComplexityChecker $caseI 'Check'
    Assert-CheckerFailure $caseIWithMigration 'CASE I migrated rename still uses old CC budget' 'baseline 2'
    Commit-FixtureFile $caseI 'src/identity.cpp' (New-IfChain 'new_name' 'int' 1) 'fixture lower migrated rename complexity'
    $caseIPass = Invoke-ComplexityChecker $caseI 'Check'
    Assert-FunctionMeasured $caseIPass 'src/identity.cpp' 'new_name' 'CASE I migrated rename at baseline CC'
    Assert-CheckerPass $caseIPass 'CASE I migrated rename at baseline CC'

    $caseJ = New-Fixture 'case-j-signature-change' 'src/signature.cpp' (New-IfChain 'calculate' 'int' 1)
    Prepare-FixtureBaseline $caseJ
    $signatureChange = "int calculate(int value, bool mode) {`n    if (value > 0) { return value; }`n    if (mode) { return value + 1; }`n    return 0;`n}`n"
    Commit-FixtureFile $caseJ 'src/signature.cpp' $signatureChange 'fixture change function signature'
    $caseJResult = Invoke-ComplexityChecker $caseJ 'Check'
    Assert-FunctionMeasured $caseJResult 'src/signature.cpp' 'calculate' 'CASE J signature change'
    Assert-CheckerFailure $caseJResult 'CASE J unique-name signature continuity uses baseline CC' 'baseline 2'
    $signatureBaseline = "int calculate(int value, bool mode) {`n    if (mode) { return value; }`n    return 0;`n}`n"
    Commit-FixtureFile $caseJ 'src/signature.cpp' $signatureBaseline 'fixture restore signature-change baseline complexity'
    $caseJPass = Invoke-ComplexityChecker $caseJ 'Check'
    Assert-FunctionMeasured $caseJPass 'src/signature.cpp' 'calculate' 'CASE J signature change at baseline CC'
    Assert-CheckerPass $caseJPass 'CASE J signature change at baseline CC'

    $upstreamOverload = "int calculate(double value) {`n    if (value > 0) { return 1; }`n    return 0;`n}`n"
    $projectOverloads = "int calculate(int value) {`n    if (value > 0) { return value; }`n    return 0;`n}`n" + "int calculate(bool mode) {`n    if (mode) { return 1; }`n    return 0;`n}`n"
    $overloads = $upstreamOverload + $projectOverloads
    $caseK = New-Fixture 'case-k-overload-ambiguity' 'src/overloads.cpp' $overloads 'src/overloads.cpp' $upstreamOverload
    Prepare-FixtureBaseline $caseK
    $ambiguousOverload = "int calculate(long value) {`n    if (value > 0) { return value; }`n    if (value > 1) { return value + 1; }`n    return 0;`n}`n"
    Commit-FixtureFile $caseK 'src/overloads.cpp' ($overloads + $ambiguousOverload) 'fixture create ambiguous overload continuity'
    $caseKResult = Invoke-ComplexityChecker $caseK 'Check'
    Assert-CheckerFailure $caseKResult 'CASE K overload ambiguity fails closed' 'Ambiguous function identity'

    $caseL = New-Fixture 'case-l-genuinely-new-function'
    Prepare-FixtureBaseline $caseL
    $newUnrelated = (New-IfChain 'genuinely_new' 'int' 1)
    Commit-FixtureFile $caseL 'src/accepted.cpp' ((New-IfChain 'accepted_function' 'int' 1) + "`n" + $newUnrelated) 'fixture add unrelated low-complexity function'
    $caseLResult = Invoke-ComplexityChecker $caseL 'Check'
    Assert-FunctionMeasured $caseLResult 'src/accepted.cpp' 'genuinely_new' 'CASE L genuinely new function'
    Assert-CheckerPass $caseLResult 'CASE L genuinely new CC <= 10 function'

    $caseMContent = (New-IfChain 'old_name' 'int' 1) + "`n" + (New-IfChain 'other_name' 'int' 1)
    $caseM = New-Fixture 'case-m-migration-validation' 'src/migrations.cpp' $caseMContent
    Prepare-FixtureBaseline $caseM
    $caseMCandidate = (New-IfChain 'new_name' 'int' 1) + "`n" + (New-IfChain 'other_new' 'int' 1)
    Commit-FixtureFile $caseM 'src/migrations.cpp' $caseMCandidate 'fixture create migration targets'
    $caseMInitial = Invoke-ComplexityChecker $caseM 'Check'
    Assert-FunctionMeasured $caseMInitial 'src/migrations.cpp' 'new_name' 'CASE M migration target fixture'
    Assert-CheckerFailure $caseMInitial 'CASE M initial unmigrated replacement' 'old_name'
    $oldMetricM = Get-ReportFunctionMetric $caseM.Directory 'complexity-after.json' 'src/migrations.cpp' 'old_name'
    $otherMetricM = Get-ReportFunctionMetric $caseM.Directory 'complexity-after.json' 'src/migrations.cpp' 'other_name'
    $newMetricM = Get-ReportFunctionMetric $caseM.Directory 'complexity-check.json' 'src/migrations.cpp' 'new_name'
    $otherNewMetricM = Get-ReportFunctionMetric $caseM.Directory 'complexity-check.json' 'src/migrations.cpp' 'other_new'
    $migrationNew = New-MigrationRecord $oldMetricM $newMetricM
    $migrationOther = New-MigrationRecord $otherMetricM $otherNewMetricM
    $invalidBaseline = New-MigrationRecordValues 'not-an-accepted-baseline' 'cpp' 'function' 'src/migrations.cpp' 'new_name' $newMetricM.signature
    Write-MigrationDocument $caseM @($invalidBaseline)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M nonexistent baselineKey' 'baselineKey.*not-an-accepted-baseline'
    $invalidTarget = New-MigrationRecordValues $oldMetricM.key 'cpp' 'function' 'src/missing.cpp' 'new_name' $newMetricM.signature
    Write-MigrationDocument $caseM @($invalidTarget)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M nonexistent current identity' 'missing\.cpp'
    $duplicateOld = New-MigrationRecord $oldMetricM $otherNewMetricM
    Write-MigrationDocument $caseM @($migrationNew, $duplicateOld)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M duplicate old mapping' 'Duplicate identity migration baselineKey'
    $duplicateNew = New-MigrationRecord $otherMetricM $newMetricM
    Write-MigrationDocument $caseM @($migrationNew, $duplicateNew)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M duplicate new mapping' 'Duplicate identity migration target'
    $wrongSignature = New-MigrationRecordValues $oldMetricM.key 'cpp' 'function' 'src/migrations.cpp' 'new_name' 'new_name( int wrong_signature)'
    Write-MigrationDocument $caseM @($wrongSignature)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M wrong signature' 'wrong_signature'
    $wrongPath = New-MigrationRecordValues $oldMetricM.key 'cpp' 'function' 'src/wrong.cpp' 'new_name' $newMetricM.signature
    Write-MigrationDocument $caseM @($wrongPath)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M wrong path' 'src/wrong\.cpp'
    Write-FixtureText (Join-Path $caseM.Directory 'complexity-identity-migrations.json') "null`n"
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M non-array migration document' 'Identity migration document'
    Write-FixtureText (Join-Path $caseM.Directory 'complexity-identity-migrations.json') ($migrationNew | ConvertTo-Json -Depth 5)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M object migration document' 'Identity migration document'
    Write-FixtureText (Join-Path $caseM.Directory 'complexity-identity-migrations.json') (New-DuplicateMigrationDocument $oldMetricM.key $newMetricM)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M duplicate JSON property' "'baselineKey'"
    $wildcardPath = New-MigrationRecordValues $oldMetricM.key 'cpp' 'function' 'src/*.cpp' 'new_name' $newMetricM.signature
    Write-MigrationDocument $caseM @($wildcardPath)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M wildcard path' 'src/\*\.cpp'
    Commit-FixtureFile $caseM 'src/migrations.cpp' ($caseMCandidate + "`n" + (New-IfChain 'old_name' 'int' 1)) 'fixture restore stale old identity'
    Write-MigrationDocument $caseM @($migrationNew)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M stale mapping with old identity present' 'stale'

    $caseN = New-Fixture 'case-n-file-and-function-rename' 'src/a.cpp' (New-IfChain 'old_name' 'int' 1)
    Prepare-FixtureBaseline $caseN
    Commit-FixtureRename $caseN 'src/a.cpp' 'src/b.cpp' (New-IfChain 'new_name' 'int' 2) 'fixture rename file and function'
    $caseNNoMigration = Invoke-ComplexityChecker $caseN 'Check'
    Assert-FunctionMeasured $caseNNoMigration 'src/b.cpp' 'new_name' 'CASE N file/function rename without migration'
    Assert-CheckerFailure $caseNNoMigration 'CASE N file/function rename without migration' 'old_name'
    $oldMetricN = Get-ReportFunctionMetric $caseN.Directory 'complexity-after.json' 'src/a.cpp' 'old_name'
    $newMetricN = Get-ReportFunctionMetric $caseN.Directory 'complexity-check.json' 'src/b.cpp' 'new_name'
    Write-MigrationDocument $caseN @(New-MigrationRecord $oldMetricN $newMetricN)
    $caseNWithMigration = Invoke-ComplexityChecker $caseN 'Check'
    Assert-CheckerFailure $caseNWithMigration 'CASE N file/function migrated rename uses old CC budget' 'baseline 2'
    Commit-FixtureFile $caseN 'src/b.cpp' (New-IfChain 'new_name' 'int' 1) 'fixture lower migrated file/function rename complexity'
    $caseNPass = Invoke-ComplexityChecker $caseN 'Check'
    Assert-FunctionMeasured $caseNPass 'src/b.cpp' 'new_name' 'CASE N migrated file/function rename at baseline CC'
    Assert-CheckerPass $caseNPass 'CASE N migrated file/function rename at baseline CC'

    $caseO = New-Fixture 'case-o-recreated-file' 'src/recreated.cpp' (New-IfChain 'calculate' 'int' 1)
    Prepare-FixtureBaseline $caseO
    Invoke-FixtureGit $caseO.Directory @('rm', '--', 'src/recreated.cpp') | Out-Null
    Invoke-FixtureGit $caseO.Directory @('commit', '-m', 'fixture delete source identity') | Out-Null
    $recreatedCandidate = New-IfChain 'calculate' 'int' 2
    Commit-FixtureFile $caseO 'src/recreated.cpp' $recreatedCandidate 'fixture recreate source with signature change'
    $caseONoMigration = Invoke-ComplexityChecker $caseO 'Check'
    Assert-FunctionMeasured $caseONoMigration 'src/recreated.cpp' 'calculate' 'CASE O recreated file without migration'
    Assert-CheckerFailure $caseONoMigration 'CASE O recreated file requires explicit migration' 'calculate\( int value\)'
    $oldMetricO = Get-ReportFunctionMetric $caseO.Directory 'complexity-after.json' 'src/recreated.cpp' 'calculate'
    $newMetricO = Get-ReportFunctionMetric $caseO.Directory 'complexity-check.json' 'src/recreated.cpp' 'calculate'
    Write-MigrationDocument $caseO @(New-MigrationRecord $oldMetricO $newMetricO)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseO 'Check') 'CASE O same-key recreated migration fails closed' 'must change'

    $replacementSignature = "int calculate(int value, bool mode) {`n    if (mode) { return value; }`n    return value;`n}`n"
    $caseP = New-Fixture 'case-p-rename-into-deleted-path' 'src/destination.cpp' (New-IfChain 'calculate' 'int' 1)
    Prepare-FixtureBaseline $caseP
    Commit-FixtureFile $caseP 'src/source.cpp' $replacementSignature 'fixture add replacement source'
    Invoke-FixtureGit $caseP.Directory @('rm', '--', 'src/destination.cpp') | Out-Null
    Invoke-FixtureGit $caseP.Directory @('commit', '-m', 'fixture delete destination path') | Out-Null
    Commit-FixtureRename $caseP 'src/source.cpp' 'src/destination.cpp' $replacementSignature 'fixture rename into deleted destination'
    $casePNoMigration = Invoke-ComplexityChecker $caseP 'Check'
    Assert-FunctionMeasured $casePNoMigration 'src/destination.cpp' 'calculate' 'CASE P rename into deleted path'
    Assert-CheckerFailure $casePNoMigration 'CASE P recreated destination requires migration' 'calculate\( int value\)'
    $oldMetricP = Get-ReportFunctionMetric $caseP.Directory 'complexity-after.json' 'src/destination.cpp' 'calculate'
    $newMetricP = Get-ReportFunctionMetric $caseP.Directory 'complexity-check.json' 'src/destination.cpp' 'calculate'
    Write-MigrationDocument $caseP @(New-MigrationRecord $oldMetricP $newMetricP)
    Assert-CheckerPass (Invoke-ComplexityChecker $caseP 'Check') 'CASE P explicit migration across recreated destination'

    $stableQContent = (1..20 | ForEach-Object { "// stable lineage marker $_" }) -join "`n"
    $caseQContent = $stableQContent + "`n" + (New-IfChain 'old_name' 'int' 1) + "`n" + (New-IfChain 'other_name' 'int' 1)
    $caseQ = New-Fixture 'case-q-lineage-migration-collision' 'src/a.cpp' $caseQContent
    Prepare-FixtureBaseline $caseQ
    Commit-FixtureRename $caseQ 'src/a.cpp' 'src/b.cpp' ($stableQContent + "`n" + (New-IfChain 'old_name' 'int' 1)) 'fixture rename with second baseline removed'
    $caseQNoMigration = Invoke-ComplexityChecker $caseQ 'Check'
    Assert-FunctionMeasured $caseQNoMigration 'src/b.cpp' 'old_name' 'CASE Q lineage target fixture'
    $oldMetricQ = Get-ReportFunctionMetric $caseQ.Directory 'complexity-after.json' 'src/a.cpp' 'other_name'
    $newMetricQ = Get-ReportFunctionMetric $caseQ.Directory 'complexity-check.json' 'src/b.cpp' 'old_name'
    Write-MigrationDocument $caseQ @(New-MigrationRecord $oldMetricQ $newMetricQ)
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseQ 'Check') 'CASE Q migration target conflicts with accepted baseline' 'accepted baseline'

    $caseR = New-Fixture 'case-r-provenance-through-nonoperator-rename'
    Prepare-FixtureBaseline $caseR
    $operatorOwnedBad = New-IfChain 'operator_owned_bad' 'int' 11
    Commit-FixtureFile $caseR 'src/operator_owned.cpp' $operatorOwnedBad 'fixture operator adds executable source'
    Invoke-FixtureGit $caseR.Directory @('config', 'user.name', 'Fixture Upstream') | Out-Null
    Invoke-FixtureGit $caseR.Directory @('config', 'user.email', 'fixture-upstream@example.invalid') | Out-Null
    Commit-FixtureRename $caseR 'src/operator_owned.cpp' 'src/nonoperator_renamed.cpp' $operatorOwnedBad 'fixture nonoperator renames project source'
    $caseRResult = Invoke-ComplexityChecker $caseR 'Check'
    Assert-FunctionMeasured $caseRResult 'src/nonoperator_renamed.cpp' 'operator_owned_bad' 'CASE R operator provenance after nonoperator rename'
    Assert-CheckerFailure $caseRResult 'CASE R renamed operator source remains measured' '(?s)operator_owned_bad'

    $caseS = New-Fixture 'case-s-baseline-integrity'
    Prepare-FixtureBaseline $caseS
    $trustedBaseline = Get-Content -LiteralPath (Join-Path $caseS.Directory 'complexity-after.json') -Raw
    Write-FixtureText (Join-Path $caseS.Directory 'complexity-after.json') "{} `n"
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseS 'Check') 'CASE S baseline integrity validation' 'integrity'
    Write-FixtureText (Join-Path $caseS.Directory 'complexity-after.json') $trustedBaseline

    $caseT = New-Fixture 'case-t-working-tree-recreated' 'src/recreated.cpp' (New-IfChain 'calculate' 'int' 1)
    Prepare-FixtureBaseline $caseT
    Invoke-FixtureGit $caseT.Directory @('rm', '--', 'src/recreated.cpp') | Out-Null
    Write-FixtureText (Join-Path $caseT.Directory 'src/recreated.cpp') (New-IfChain 'calculate' 'int' 1)
    $caseTResult = Invoke-ComplexityChecker $caseT 'Check'
    Assert-FunctionMeasured $caseTResult 'src/recreated.cpp' 'calculate' 'CASE T staged working-tree recreation'
    Assert-CheckerFailure $caseTResult 'CASE T staged recreation requires continuity migration' 'calculate\( int value\)'

    $caseU = New-Fixture 'case-u-frozen-post-hardening-function'
    Prepare-FixtureBaseline $caseU
    Commit-FixtureFile $caseU 'src/accepted.cpp' (New-IfChain 'post_hardening_function' 'int' 1) 'fixture add post-hardening function'
    Assert-CheckerPass (Invoke-ComplexityChecker $caseU 'After') 'CASE U freeze after snapshot'
    Trust-FixtureBaseline $caseU
    Commit-FixtureFile $caseU 'src/accepted.cpp' (New-IfChain 'post_hardening_function' 'int' 2) 'fixture regress frozen post-hardening function'
    $caseURegressed = Invoke-ComplexityChecker $caseU 'Check'
    Assert-FunctionMeasured $caseURegressed 'src/accepted.cpp' 'post_hardening_function' 'CASE U frozen function regression'
    Assert-CheckerFailure $caseURegressed 'CASE U frozen CC 2 to CC 3 regression' 'baseline 2'
    Commit-FixtureFile $caseU 'src/accepted.cpp' (New-IfChain 'post_hardening_function' 'int' 1) 'fixture restore frozen post-hardening function'
    $caseUPass = Invoke-ComplexityChecker $caseU 'Check'
    Assert-FunctionMeasured $caseUPass 'src/accepted.cpp' 'post_hardening_function' 'CASE U frozen function at baseline'
    Assert-CheckerPass $caseUPass 'CASE U frozen CC 2 remains passing'

    $caseV = New-Fixture 'case-v-new-script-body-bad'
    Prepare-FixtureBaseline $caseV
    Commit-FixtureFile $caseV 'tools/new-script-body-bad.ps1' (New-PowerShellTopLevelIfChain 11) 'fixture add bad script body'
    $caseVResult = Invoke-ComplexityChecker $caseV 'Check'
    Assert-ScriptBodyMeasured $caseVResult 'tools/new-script-body-bad.ps1' 'CASE V new PowerShell script-body'
    Assert-CheckerFailure $caseVResult 'CASE V new script-body CC > 10' '(?s)script-body'

    $caseW = New-Fixture 'case-w-new-script-body-good'
    Prepare-FixtureBaseline $caseW
    Commit-FixtureFile $caseW 'tools/new-script-body-good.ps1' (New-PowerShellTopLevelIfChain 4) 'fixture add acceptable script body'
    $caseWResult = Invoke-ComplexityChecker $caseW 'Check'
    Assert-ScriptBodyMeasured $caseWResult 'tools/new-script-body-good.ps1' 'CASE W new PowerShell script-body'
    Assert-CheckerPass $caseWResult 'CASE W new script-body CC <= 5'

    $caseX = New-Fixture 'case-x-frozen-script-body' 'tools/frozen-script.ps1' (New-PowerShellTopLevelIfChain 1)
    Prepare-FixtureBaseline $caseX
    Commit-FixtureFile $caseX 'tools/frozen-script.ps1' (New-PowerShellTopLevelIfChain 2) 'fixture regress frozen script body'
    $caseXRegressed = Invoke-ComplexityChecker $caseX 'Check'
    Assert-ScriptBodyMeasured $caseXRegressed 'tools/frozen-script.ps1' 'CASE X frozen script-body regression'
    Assert-CheckerFailure $caseXRegressed 'CASE X frozen script-body CC 2 to CC 3 regression' 'baseline 2'
    Commit-FixtureFile $caseX 'tools/frozen-script.ps1' (New-PowerShellTopLevelIfChain 1) 'fixture restore frozen script body'
    $caseXPass = Invoke-ComplexityChecker $caseX 'Check'
    Assert-ScriptBodyMeasured $caseXPass 'tools/frozen-script.ps1' 'CASE X frozen script-body at baseline'
    Assert-CheckerPass $caseXPass 'CASE X frozen script-body CC 2 remains passing'

    $caseY = New-Fixture 'case-y-script-file-rename' 'tools/old-script.ps1' (New-PowerShellTopLevelIfChain 1)
    Prepare-FixtureBaseline $caseY
    Commit-FixtureRename $caseY 'tools/old-script.ps1' 'tools/new-script.ps1' (New-PowerShellTopLevelIfChain 2) 'fixture rename script body and regress'
    $caseYRegressed = Invoke-ComplexityChecker $caseY 'Check'
    Assert-ScriptBodyMeasured $caseYRegressed 'tools/new-script.ps1' 'CASE Y renamed script-body regression'
    Assert-CheckerFailure $caseYRegressed 'CASE Y renamed script-body inherits baseline' 'baseline 2'
    Commit-FixtureFile $caseY 'tools/new-script.ps1' (New-PowerShellTopLevelIfChain 1) 'fixture restore renamed script body'
    $caseYPass = Invoke-ComplexityChecker $caseY 'Check'
    Assert-ScriptBodyMeasured $caseYPass 'tools/new-script.ps1' 'CASE Y renamed script-body at baseline'
    Assert-CheckerPass $caseYPass 'CASE Y renamed script-body CC 2 remains passing'

    $caseZ = New-Fixture 'case-z-recreated-script-body' 'tools/recreated-script.ps1' (New-PowerShellTopLevelIfChain 1)
    Prepare-FixtureBaseline $caseZ
    Invoke-FixtureGit $caseZ.Directory @('rm', '--', 'tools/recreated-script.ps1') | Out-Null
    Invoke-FixtureGit $caseZ.Directory @('commit', '-m', 'fixture delete script body') | Out-Null
    Commit-FixtureFile $caseZ 'tools/recreated-script.ps1' (New-PowerShellTopLevelIfChain 4) 'fixture recreate script body'
    $caseZResult = Invoke-ComplexityChecker $caseZ 'Check'
    Assert-ScriptBodyMeasured $caseZResult 'tools/recreated-script.ps1' 'CASE Z recreated script-body'
    Assert-CheckerFailure $caseZResult 'CASE Z recreated script-body requires continuity' 'script-body'

    $caseAA = New-Fixture 'case-aa-inline-powershell-control-flow'
    Prepare-FixtureBaseline $caseAA
    Commit-FixtureFile $caseAA '.github/workflows/inline-powershell.yaml' (New-WorkflowDocument 'pwsh' (New-PowerShellTopLevelIfChain 11)) 'fixture add inline PowerShell control flow'
    $caseAAResult = Invoke-ComplexityChecker $caseAA 'Check'
    Assert-CheckerFailure $caseAAResult 'CASE AA inline PowerShell requires extraction' '(?s)non-trivial inline PowerShell.*extract it to a measured.*\.ps1'

    $caseAB = New-Fixture 'case-ab-inline-powershell-function'
    Prepare-FixtureBaseline $caseAB
    $inlineFunction = "function Foo { return 1 }`nFoo`n"
    Commit-FixtureFile $caseAB '.github/workflows/inline-powershell-function.yaml' (New-WorkflowDocument 'pwsh' $inlineFunction) 'fixture add inline PowerShell function'
    $caseABResult = Invoke-ComplexityChecker $caseAB 'Check'
    Assert-CheckerFailure $caseABResult 'CASE AB inline PowerShell function requires extraction' '(?s)non-trivial inline PowerShell.*extract it to a measured.*\.ps1'

    $caseAC = New-Fixture 'case-ac-trivial-powershell-wrapper'
    Prepare-FixtureBaseline $caseAC
    Write-FixtureText (Join-Path $caseAC.Directory '.github/scripts/foo.ps1') "Write-Output 'fixture wrapper target'`n"
    Commit-FixtureFile $caseAC '.github/workflows/trivial-powershell.yaml' (New-WorkflowDocument 'pwsh' '& .github/scripts/foo.ps1') 'fixture add trivial PowerShell wrapper'
    $caseACResult = Invoke-ComplexityChecker $caseAC 'Check'
    Assert-CheckerPass $caseACResult 'CASE AC trivial PowerShell wrapper'

    $caseAD = New-Fixture 'case-ad-trivial-bash-wrapper'
    Prepare-FixtureBaseline $caseAD
    Commit-FixtureFile $caseAD '.github/workflows/trivial-bash.yaml' (New-WorkflowDocument 'bash' 'cmake --build build') 'fixture add trivial Bash wrapper'
    $caseADResult = Invoke-ComplexityChecker $caseAD 'Check'
    Assert-CheckerPass $caseADResult 'CASE AD trivial Bash wrapper'

    $caseAE = New-Fixture 'case-ae-bash-control-flow'
    Prepare-FixtureBaseline $caseAE
    $bashControl = "if test -f build/ready; then`n  echo ready`nfi`n"
    Commit-FixtureFile $caseAE '.github/workflows/inline-bash-control.yaml' (New-WorkflowDocument 'bash' $bashControl) 'fixture add Bash control flow'
    $caseAEResult = Invoke-ComplexityChecker $caseAE 'Check'
    Assert-CheckerFailure $caseAEResult 'CASE AE Bash control flow fails closed' 'unsupported inline bash'

    $caseAF = New-Fixture 'case-af-inline-python'
    Prepare-FixtureBaseline $caseAF
    $pythonControl = "for value in range(12):`n    print(value)`n"
    Commit-FixtureFile $caseAF '.github/workflows/inline-python.yaml' (New-WorkflowDocument 'python3' $pythonControl) 'fixture add inline Python logic'
    $caseAFResult = Invoke-ComplexityChecker $caseAF 'Check'
    Assert-CheckerFailure $caseAFResult 'CASE AF unsupported inline Python fails closed' 'unsupported inline python'

    $caseAG = New-Fixture 'case-ag-malformed-inline-powershell'
    Prepare-FixtureBaseline $caseAG
    Commit-FixtureFile $caseAG '.github/workflows/malformed-powershell.yaml' (New-WorkflowDocument 'pwsh' 'if (') 'fixture add malformed inline PowerShell'
    $caseAGResult = Invoke-ComplexityChecker $caseAG 'Check'
    Assert-CheckerFailure $caseAGResult 'CASE AG malformed PowerShell fails closed' 'invalid inline PowerShell'

    $caseAH = New-Fixture 'case-ah-workflow-rename-provenance'
    Prepare-FixtureBaseline $caseAH
    $renameWorkflowBody = New-WorkflowDocument 'pwsh' (New-PowerShellTopLevelIfChain 11)
    Commit-FixtureFile $caseAH '.github/workflows/old-inline.yaml' $renameWorkflowBody 'fixture add owned workflow for rename'
    $caseAHOldResult = Invoke-ComplexityChecker $caseAH 'Check'
    Assert-CheckerFailure $caseAHOldResult 'CASE AH original workflow executable policy' '(?s)non-trivial inline PowerShell.*extract it to a measured.*\.ps1'
    Commit-FixtureRename $caseAH '.github/workflows/old-inline.yaml' '.github/workflows/new-inline.yaml' $renameWorkflowBody 'fixture rename owned workflow'
    $caseAHNewResult = Invoke-ComplexityChecker $caseAH 'Check'
    Assert-CheckerFailure $caseAHNewResult 'CASE AH renamed workflow executable policy' '(?s)non-trivial inline PowerShell.*extract it to a measured.*\.ps1'

    $caseAI = New-Fixture 'case-ai-untracked-workflow'
    Prepare-FixtureBaseline $caseAI
    Write-FixtureText (Join-Path $caseAI.Directory '.github/workflows/untracked-inline.yaml') (New-WorkflowDocument 'pwsh' (New-PowerShellTopLevelIfChain 11))
    $caseAIResult = Invoke-ComplexityChecker $caseAI 'Check'
    Assert-CheckerFailure $caseAIResult 'CASE AI untracked workflow executable policy' '(?s)non-trivial inline PowerShell.*extract it to a measured.*\.ps1'

    Write-Output 'Complexity checker self-test: PASS (cases A-AI)'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
