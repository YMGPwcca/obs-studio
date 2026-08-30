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
    if ($Result.ExitCode -eq 0 -or $normalizedOutput -notmatch $Pattern) {
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
    Assert-CheckerPass (Invoke-ComplexityChecker $Scenario 'After') "$($Scenario.Name) accepted after"
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
    Assert-CheckerFailure (Invoke-ComplexityChecker $caseM 'Check') 'CASE M duplicate JSON property' 'duplicate property'
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

    Write-Output 'Complexity checker self-test: PASS (cases A-O)'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
