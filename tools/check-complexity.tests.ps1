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

    if ($Result.ExitCode -eq 0 -or $Result.Output -notmatch $Pattern) {
        throw "$Label did not fail as expected (exit $($Result.ExitCode), pattern '$Pattern').`n$($Result.Output)"
    }
    $compactOutput = ($Result.Output -replace '\s+', ' ').Trim()
    $match = [regex]::Match($compactOutput, $Pattern)
    $detail = if ($match.Success) { $match.Value } else { $compactOutput }
    if ($detail.Length -gt 320) {
        $detail = $detail.Substring(0, 320) + '...'
    }
    Write-Output "${Label}: expected FAIL (exit $($Result.ExitCode)): $detail"
}

function Assert-FunctionMeasured {
    param(
        [Parameter(Mandatory = $true)] [object] $Result,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Function,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    $reportPath = Join-Path ([string] $Result.ReportDirectory) 'complexity-check.json'
    if (-not (Test-Path -LiteralPath $reportPath)) {
        throw "$Label did not produce a diagnostic check report."
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $metric = @($report.functions | Where-Object {
            $_.scopeKind -eq 'function' -and $_.file -eq $Path -and $_.function -eq $Function
        }) | Select-Object -First 1
    if ($null -eq $metric) {
        throw "$Label was not present in the measured function universe."
    }
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
        [string] $AcceptedContent = "int accepted_function(int value) {`n    if (value > 0) { return value; }`n    return 0;`n}`n"
    )

    $directory = Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path (Join-Path $directory 'tools') -Force | Out-Null
    Copy-Item -LiteralPath $checkerPath -Destination (Join-Path $directory 'tools/check-complexity.ps1')
    Write-FixtureText (Join-Path $directory 'README.md') "fixture $Name`n"
    Write-FixtureText (Join-Path $directory 'complexity-exceptions.json') "[]`n"
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
    Assert-CheckerFailure $caseGResult 'CASE G unsupported executable language fails closed' '(?s)Unsupported or unclassified executable path.*new_helper.py'

    Write-Output 'Complexity checker self-test: PASS (cases A-H)'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
