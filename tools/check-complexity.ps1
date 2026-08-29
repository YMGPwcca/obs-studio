[CmdletBinding()]
param(
    [ValidateSet('Baseline', 'After', 'Check')]
    [string] $Mode = 'Baseline',
    [string] $BaseRef = '',
    [string] $AcceptedRef = '3fc2e678d10809a4dca8b28107710534160803ab',
    [string] $InventoryPath = 'complexity-ownership-inventory.json',
    [string] $JsonPath = '',
    [string] $MarkdownPath = '',
    [string] $BeforePath = 'complexity-before.json',
    [string] $BaselinePath = 'complexity-after.json',
    [string] $AllowlistPath = 'complexity-exceptions.json',
    [string] $LizardPythonPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repoRoot

$cppExtensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx')
$scriptExtensions = @('.ps1', '.py', '.pyw', '.lua', '.sh', '.bat', '.cmd')
$knownOperatorEmails = @(
    '37042810+YMGPwcca@users.noreply.github.com',
    'ymgpwcca@proton.me'
)
$knownOperatorName = 'YMGPwcca'
$limitations = [System.Collections.Generic.List[object]]::new()

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Contents
    )

    $fullPath = if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $repoRoot $Path }
    $parent = Split-Path -Parent $fullPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [IO.File]::WriteAllText($fullPath, $Contents, [Text.UTF8Encoding]::new($false))
}

function Read-JsonFile {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $fullPath = if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $repoRoot $Path }
    return (Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json)
}

function Invoke-GitLines {
    param([Parameter(Mandatory = $true)] [string[]] $Arguments)

    $result = @(& git @Arguments)
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
    return $result
}

function Resolve-Commit {
    param([Parameter(Mandatory = $true)] [string] $Ref)

    $resolved = (@(Invoke-GitLines @('rev-parse', '--verify', "${Ref}^{commit}")) | Select-Object -First 1).Trim()
    if ($resolved -notmatch '^[0-9a-f]{40}$') {
        throw "Could not resolve commit ref '$Ref'."
    }
    return $resolved
}

function Normalize-RepoPath {
    param([Parameter(Mandatory = $true)] [string] $Path)

    return (($Path -replace '\\', '/') -replace '^\./', '')
}

function Get-PathLanguage {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $extension = [IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($cppExtensions -contains $extension) {
        return 'cpp'
    }
    if ($scriptExtensions -contains $extension) {
        return 'powershell-or-script'
    }
    return $null
}

function Test-PathAtRef {
    param(
        [Parameter(Mandatory = $true)] [string] $Ref,
        [Parameter(Mandatory = $true)] [string] $Path
    )

    & git cat-file -e "${Ref}:$Path" 2>$null
    return $LASTEXITCODE -eq 0
}

function Get-OperatorPredicate {
    param([Parameter(Mandatory = $true)] [hashtable] $EmailSet)

    return ({
        param($Commit)
        $email = ([string] $Commit.Email).ToLowerInvariant()
        $name = [string] $Commit.Name
        return $EmailSet.ContainsKey($email) -or $name -ceq $knownOperatorName
    }).GetNewClosure()
}

function Get-CommitRecords {
    param([Parameter(Mandatory = $true)] [string] $Range)

    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($line in (Invoke-GitLines @('log', $Range, '--format=%H%x09%an%x09%ae%x09%s', '--reverse'))) {
        $parts = $line -split "`t", 4
        if ($parts.Count -lt 4) {
            throw "Unexpected git log record: $line"
        }
        $records.Add([pscustomobject]@{
                Hash    = $parts[0]
                Name    = $parts[1]
                Email   = $parts[2]
                Subject = $parts[3]
            })
    }
    return @($records)
}

function Get-ChangedPathRecords {
    param([Parameter(Mandatory = $true)] [string] $Range)

    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($line in (Invoke-GitLines @('diff', '--name-status', '--find-renames', $Range))) {
        $parts = $line -split "`t"
        if ($parts.Count -lt 2) {
            continue
        }
        $records.Add([pscustomobject]@{
                Status = $parts[0]
                Path   = Normalize-RepoPath $parts[$parts.Count - 1]
            })
    }
    return @($records)
}

function Get-OperatorBlameLines {
    param(
        [Parameter(Mandatory = $true)] [string] $Ref,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [hashtable] $EmailSet
    )

    $lineSet = [System.Collections.Generic.HashSet[int]]::new()
    $blame = @(Invoke-GitLines @('blame', $Ref, '--line-porcelain', '--', $Path))
    $lineNumber = 0
    $remaining = 0
    $isOperator = $false
    foreach ($line in $blame) {
        if ($line -match '^([0-9a-f]{40})\s+\d+\s+(\d+)(?:\s+(\d+))?$') {
            $lineNumber = [int] $matches[2]
            $remaining = if ($matches[3]) { [int] $matches[3] } else { 1 }
            $isOperator = $false
            continue
        }
        if ($line.StartsWith('author-mail ')) {
            $email = $line.Substring('author-mail '.Length).Trim('<', '>').ToLowerInvariant()
            $isOperator = $EmailSet.ContainsKey($email)
            continue
        }
        if ($line.StartsWith("`t")) {
            if ($isOperator) {
                $null = $lineSet.Add($lineNumber)
            }
            $lineNumber++
            $remaining--
            if ($remaining -le 0) {
                $isOperator = $false
            }
        }
    }
    return $lineSet
}

function Test-LineIntersection {
    param(
        [Parameter(Mandatory = $true)] [System.Collections.Generic.HashSet[int]] $Lines,
        [Parameter(Mandatory = $true)] [int] $StartLine,
        [Parameter(Mandatory = $true)] [int] $EndLine
    )

    for ($line = $StartLine; $line -le $EndLine; $line++) {
        if ($Lines.Contains($line)) {
            return $true
        }
    }
    return $false
}

function Get-CandidateChangedLines {
    param(
        [Parameter(Mandatory = $true)] [string] $Ref,
        [Parameter(Mandatory = $true)] [string] $Path
    )

    $lineSet = [System.Collections.Generic.HashSet[int]]::new()
    $diff = @(Invoke-GitLines @('diff', '--unified=0', $Ref, '--', $Path))
    foreach ($line in $diff) {
        if ($line -match '^@@[^+]*\+(\d+)(?:,(\d+))?[^@]*@@') {
            $start = [int] $matches[1]
            $count = if ($matches[2]) { [int] $matches[2] } else { 1 }
            for ($lineNumber = $start; $lineNumber -lt ($start + $count); $lineNumber++) {
                $null = $lineSet.Add($lineNumber)
            }
        }
    }
    return $lineSet
}

function Get-PythonExecutable {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        return $py.Source
    }
    throw 'Python is required to invoke the temporary lizard analyzer.'
}

function Get-LizardRows {
    param(
        [Parameter(Mandatory = $true)] [string[]] $Files,
        [Parameter(Mandatory = $true)] [string] $Python
    )

    if ($Files.Count -eq 0) {
        return @()
    }

    $oldPythonPath = $env:PYTHONPATH
    if ($LizardPythonPath) {
        $env:PYTHONPATH = if ($oldPythonPath) { "$LizardPythonPath;$oldPythonPath" } else { $LizardPythonPath }
    }
    try {
        $output = @(& $Python -m lizard --csv -l cpp --no-gitignore @Files 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $env:PYTHONPATH = $oldPythonPath
    }
    if ($exitCode -ne 0) {
        throw "lizard failed with exit code $exitCode.`n$($output -join "`n")"
    }

    $rows = [System.Collections.Generic.List[object]]::new()
    $csvLines = @($output | ForEach-Object { [string] $_ } | Where-Object { $_ -match '^\s*\d+,' })
    foreach ($line in $csvLines) {
        $rows.Add(($line | ConvertFrom-Csv -Header @(
                    'NLOC', 'CCN', 'Tokens', 'Parameters', 'Length', 'Location',
                    'File', 'Function', 'Signature', 'StartLine', 'EndLine'
                )))
    }
    return @($rows)
}

function Get-LizardVersion {
    param([Parameter(Mandatory = $true)] [string] $Python)

    $oldPythonPath = $env:PYTHONPATH
    if ($LizardPythonPath) {
        $env:PYTHONPATH = if ($oldPythonPath) { "$LizardPythonPath;$oldPythonPath" } else { $LizardPythonPath }
    }
    try {
        $version = (@(& $Python -m lizard --version 2>&1) | Select-Object -First 1).ToString().Trim()
    } finally {
        $env:PYTHONPATH = $oldPythonPath
    }
    if (-not $version) {
        throw 'Could not determine lizard version.'
    }
    return $version
}

function Get-FunctionKey {
    param(
        [Parameter(Mandatory = $true)] [string] $Language,
        [Parameter(Mandatory = $true)] [string] $File,
        [Parameter(Mandatory = $true)] [string] $Function,
        [Parameter(Mandatory = $true)] [string] $Signature,
        [Parameter(Mandatory = $true)] [string] $ScopeKind
    )

    return "$Language|$ScopeKind|$File|$Function|$Signature"
}

function Get-NonBlankLineCount {
    param(
        [string[]] $Lines,
        [Parameter(Mandatory = $true)] [int] $StartLine,
        [Parameter(Mandatory = $true)] [int] $EndLine,
        [object[]] $ExcludedLines
    )

    $count = 0
    $start = [Math]::Max(1, $StartLine)
    $end = [Math]::Min($Lines.Count, $EndLine)
    for ($lineNumber = $start; $lineNumber -le $end; $lineNumber++) {
        if ($ExcludedLines) {
            $excluded = $false
            if ($ExcludedLines -is [System.Collections.Generic.HashSet[int]]) {
                $excluded = $ExcludedLines.Contains($lineNumber)
            } else {
                foreach ($range in $ExcludedLines) {
                    if ($lineNumber -ge $range.StartLine -and $lineNumber -le $range.EndLine) {
                        $excluded = $true
                        break
                    }
                }
            }
            if ($excluded) {
                continue
            }
        }
        $trimmed = $Lines[$lineNumber - 1].Trim()
        if ($trimmed -and -not $trimmed.StartsWith('#')) {
            $count++
        }
    }
    return $count
}

function Get-AstTypeMap {
    $assembly = [System.Management.Automation.Language.Ast].Assembly
    $names = @(
        'FunctionDefinitionAst', 'IfStatementAst', 'ForStatementAst',
        'ForEachStatementAst', 'WhileStatementAst', 'DoWhileStatementAst',
        'DoUntilStatementAst', 'SwitchStatementAst', 'CatchClauseAst',
        'TernaryExpressionAst', 'BinaryExpressionAst', 'TrapStatementAst'
    )
    $types = @{}
    foreach ($name in $names) {
        $type = $assembly.GetType("System.Management.Automation.Language.$name")
        if ($type) {
            $types[$name] = $type
        }
    }
    return $types
}

function Test-AstType {
    param(
        [Parameter(Mandatory = $true)] $Node,
        [Parameter(Mandatory = $true)] [hashtable] $Types,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    return $Types.ContainsKey($Name) -and $Types[$Name].IsInstanceOfType($Node)
}

function Test-AstInsideRange {
    param(
        [Parameter(Mandatory = $true)] $Node,
        [object[]] $Ranges
    )

    foreach ($range in $Ranges) {
        if ($Node.Extent.StartOffset -ge $range.StartOffset -and
            $Node.Extent.EndOffset -le $range.EndOffset) {
            return $true
        }
    }
    return $false
}

function Get-AstCyclomaticComplexity {
    param(
        [Parameter(Mandatory = $true)] $Root,
        [object[]] $ExcludedFunctionRanges,
        [Parameter(Mandatory = $true)] [hashtable] $Types
    )

    $complexity = 1
    foreach ($node in @($Root.FindAll({ param($candidate) $true }, $true))) {
        if (Test-AstType $node $Types 'FunctionDefinitionAst') {
            continue
        }
        if (Test-AstInsideRange $node $ExcludedFunctionRanges) {
            continue
        }
        if (Test-AstType $node $Types 'IfStatementAst') {
            $complexity += [Math]::Max(1, @($node.Clauses).Count)
        } elseif ((Test-AstType $node $Types 'ForStatementAst') -or
            (Test-AstType $node $Types 'ForEachStatementAst') -or
            (Test-AstType $node $Types 'WhileStatementAst') -or
            (Test-AstType $node $Types 'DoWhileStatementAst') -or
            (Test-AstType $node $Types 'DoUntilStatementAst')) {
            $complexity++
        } elseif (Test-AstType $node $Types 'SwitchStatementAst') {
            $complexity += @($node.Clauses).Count
        } elseif ((Test-AstType $node $Types 'CatchClauseAst') -or
            (Test-AstType $node $Types 'TernaryExpressionAst') -or
            (Test-AstType $node $Types 'TrapStatementAst')) {
            $complexity++
        } elseif ((Test-AstType $node $Types 'BinaryExpressionAst') -and
            ([string] $node.Operator) -match 'And|Or') {
            $complexity++
        }
    }
    return $complexity
}

function Get-PowerShellMetrics {
    param(
        [Parameter(Mandatory = $true)] [string[]] $Files,
        [Parameter(Mandatory = $true)] [hashtable] $FileLines,
        [Parameter(Mandatory = $true)] [string] $Mode
    )

    $metrics = [System.Collections.Generic.List[object]]::new()
    $types = Get-AstTypeMap
    if (-not $types.ContainsKey('FunctionDefinitionAst')) {
        throw 'PowerShell FunctionDefinitionAst is unavailable; cannot measure PowerShell scripts.'
    }

    foreach ($path in $Files) {
        $fullPath = Join-Path $repoRoot $path
        $sourceLines = @(Get-Content -LiteralPath $fullPath)
        $tokens = $null
        $errors = $null
        $ast = [System.Management.Automation.Language.Parser]::ParseFile($fullPath, [ref] $tokens, [ref] $errors)
        if (@($errors).Count -gt 0) {
            $limitations.Add([pscustomobject]@{
                    file    = $path
                    reason  = 'PowerShell parser reported errors; metrics are unavailable for this file.'
                    errors  = @($errors | ForEach-Object { $_.Message })
                })
            continue
        }

        $functions = @($ast.FindAll({
                    param($candidate)
                    Test-AstType $candidate $types 'FunctionDefinitionAst'
                }, $true))
        $functionRanges = @($functions | ForEach-Object {
                [pscustomobject]@{
                    StartOffset = $_.Extent.StartOffset
                    EndOffset   = $_.Extent.EndOffset
                    StartLine   = $_.Extent.StartLineNumber
                    EndLine     = $_.Extent.EndLineNumber
                }
            })
        $operatorLines = $FileLines[$path]
        foreach ($function in $functions) {
            $start = [int] $function.Extent.StartLineNumber
            $end = [int] $function.Extent.EndLineNumber
            if ($Mode -eq 'Baseline' -and -not (Test-LineIntersection $operatorLines $start $end)) {
                continue
            }
            $nestedRanges = @($functions | Where-Object {
                    $_.Extent.StartOffset -gt $function.Body.Extent.StartOffset -and
                    $_.Extent.EndOffset -le $function.Body.Extent.EndOffset
                } | ForEach-Object {
                    [pscustomobject]@{
                        StartOffset = $_.Extent.StartOffset
                        EndOffset   = $_.Extent.EndOffset
                    }
                })
            $name = [string] $function.Name
            $key = Get-FunctionKey 'powershell' $path $name $name 'function'
            $metrics.Add([pscustomobject]@{
                    key                   = $key
                    language              = 'powershell'
                    scopeKind             = 'function'
                    file                  = $path
                    function              = $name
                    signature             = $name
                    startLine             = $start
                    endLine               = $end
                    nloc                  = Get-NonBlankLineCount $sourceLines $start $end
                    cyclomaticComplexity  = Get-AstCyclomaticComplexity $function.Body $nestedRanges $types
                    parameterCount        = @($function.Parameters).Count
                    analyzer              = 'PowerShell Language.Parser AST'
                    nlocMethod            = 'nonblank non-comment source lines'
                    ccMethod             = 'base 1 plus AST if/elseif, loop, switch-clause, catch, trap, ternary, and logical-and/or nodes'
                })
        }

        $outsideOperatorLines = [System.Collections.Generic.HashSet[int]]::new()
        foreach ($lineNumber in $operatorLines) {
            $inside = $false
            foreach ($function in $functions) {
                if ($lineNumber -ge $function.Extent.StartLineNumber -and
                    $lineNumber -le $function.Extent.EndLineNumber) {
                    $inside = $true
                    break
                }
            }
            if (-not $inside) {
                $null = $outsideOperatorLines.Add($lineNumber)
            }
        }
        if ($Mode -ne 'Baseline' -or $outsideOperatorLines.Count -gt 0) {
            $scriptKey = Get-FunctionKey 'powershell' $path '<script-body>' '<script-body>' 'script-body'
            $metrics.Add([pscustomobject]@{
                    key                   = $scriptKey
                    language              = 'powershell'
                    scopeKind             = 'script-body'
                    file                  = $path
                    function              = '<script-body>'
                    signature             = '<script-body>'
                    startLine             = 1
                    endLine               = $sourceLines.Count
                    nloc                  = Get-NonBlankLineCount $sourceLines 1 $sourceLines.Count $functionRanges
                    cyclomaticComplexity  = Get-AstCyclomaticComplexity $ast $functionRanges $types
                    parameterCount        = 0
                    analyzer              = 'PowerShell Language.Parser AST'
                    nlocMethod            = 'nonblank non-comment source lines outside function definitions'
                    ccMethod             = 'base 1 plus AST if/elseif, loop, switch-clause, catch, trap, ternary, and logical-and/or nodes outside functions'
                })
        }
    }
    return @($metrics)
}

function Get-ScopedCppMetrics {
    param(
        [Parameter(Mandatory = $true)] [string[]] $Files,
        [Parameter(Mandatory = $true)] [hashtable] $FileLines,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineKeys,
        [Parameter(Mandatory = $true)] [hashtable] $CandidateLines,
        [Parameter(Mandatory = $true)] [string] $Python
    )

    $version = Get-LizardVersion $Python
    $rows = @(Get-LizardRows $Files $Python)
    $metrics = [System.Collections.Generic.List[object]]::new()
    foreach ($row in $rows) {
        $file = Normalize-RepoPath ([string] $row.File)
        if (-not $FileLines.ContainsKey($file)) {
            continue
        }
        $start = [int] $row.StartLine
        $end = [int] $row.EndLine
        $include = if ($Mode -eq 'Baseline') {
            Test-LineIntersection $FileLines[$file] $start $end
        } else {
            $functionName = [string] $row.Function
            $signature = [string] $row.Signature
            $exactKey = Get-FunctionKey 'cpp' $file $functionName $signature 'function'
            $fallback = @($BaselineKeys.Keys | Where-Object { $_ -like "cpp|function|$file|$functionName|*" })
            $BaselineKeys.ContainsKey($exactKey) -or
            $fallback.Count -eq 1 -or
            ($CandidateLines.ContainsKey($file) -and (Test-LineIntersection $CandidateLines[$file] $start $end))
        }
        if (-not $include) {
            continue
        }
        $metrics.Add([pscustomobject]@{
                key                  = Get-FunctionKey 'cpp' $file ([string] $row.Function) ([string] $row.Signature) 'function'
                language             = 'cpp'
                scopeKind            = 'function'
                file                 = $file
                function             = [string] $row.Function
                signature            = [string] $row.Signature
                startLine            = $start
                endLine              = $end
                nloc                 = [int] $row.NLOC
                cyclomaticComplexity = [int] $row.CCN
                parameterCount       = [int] $row.Parameters
                analyzer             = "lizard $version"
                nlocMethod           = 'lizard NLOC'
                ccMethod             = 'lizard default CCN (switch cases counted individually)'
            })
    }
    return @($metrics)
}

function Get-Statistics {
    param([Parameter(Mandatory = $true)] [object[]] $Metrics)

    $values = @($Metrics | Where-Object { $_.scopeKind -eq 'function' } |
        ForEach-Object { [int] $_.cyclomaticComplexity } | Sort-Object)
    if ($values.Count -eq 0) {
        return [ordered]@{
            scopedFunctionCount = 0
            averageCC           = 0
            medianCC            = 0
            p90CC               = 0
            maximumCC           = 0
            countCCGreater5     = 0
            countCCGreater7     = 0
            countCCGreater10    = 0
        }
    }
    $middle = [int] [Math]::Floor($values.Count / 2)
    $median = if ($values.Count % 2 -eq 0) {
        ($values[$middle - 1] + $values[$middle]) / 2
    } else {
        $values[$middle]
    }
    $p90Rank = [int] [Math]::Ceiling($values.Count * 0.9)
    $p90 = $values[[Math]::Max(0, $p90Rank - 1)]
    return [ordered]@{
        scopedFunctionCount = $values.Count
        averageCC           = [Math]::Round((($values | Measure-Object -Average).Average), 3)
        medianCC            = [Math]::Round($median, 3)
        p90CC               = $p90
        maximumCC           = $values[-1]
        countCCGreater5     = @($values | Where-Object { $_ -gt 5 }).Count
        countCCGreater7     = @($values | Where-Object { $_ -gt 7 }).Count
        countCCGreater10    = @($values | Where-Object { $_ -gt 10 }).Count
    }
}

function Get-SortedFunctions {
    param([Parameter(Mandatory = $true)] [object[]] $Metrics)

    return @($Metrics | Where-Object { $_.scopeKind -eq 'function' } |
        Sort-Object @{ Expression = { [int] $_.cyclomaticComplexity }; Descending = $true },
        @{ Expression = { [int] $_.nloc }; Descending = $true },
        @{ Expression = { [string] $_.file }; Descending = $false },
        @{ Expression = { [int] $_.startLine }; Descending = $false })
}

function Format-StatTable {
    param([Parameter(Mandatory = $true)] [object] $Statistics)

    return @(
        '| Measure | Value |',
        '|---|---:|',
        "| Scoped functions | $($Statistics.scopedFunctionCount) |",
        "| Average CC | $($Statistics.averageCC) |",
        "| Median CC | $($Statistics.medianCC) |",
        "| 90th percentile CC (nearest rank) | $($Statistics.p90CC) |",
        "| Maximum CC | $($Statistics.maximumCC) |",
        "| Functions with CC > 5 | $($Statistics.countCCGreater5) |",
        "| Functions with CC > 7 | $($Statistics.countCCGreater7) |",
        "| Functions with CC > 10 | $($Statistics.countCCGreater10) |"
    ) -join "`n"
}

function Format-FunctionList {
    param(
        [Parameter(Mandatory = $true)] [object[]] $Functions,
        [Parameter(Mandatory = $true)] [int] $Threshold
    )

    $selected = @($Functions | Where-Object { [int] $_.cyclomaticComplexity -gt $Threshold })
    if ($selected.Count -eq 0) {
        return '_None._'
    }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($function in $selected) {
        $lines.Add("- ``$($function.function)`` — ``$($function.file):$($function.startLine)-$($function.endLine)``, CC $($function.cyclomaticComplexity), NLOC $($function.nloc), params $($function.parameterCount)")
    }
    return $lines -join "`n"
}

function New-BaselineMarkdown {
    param(
        [Parameter(Mandatory = $true)] [object] $Report,
        [Parameter(Mandatory = $true)] [object] $Inventory
    )

    $functions = @(Get-SortedFunctions $Report.functions)
    $top = @($functions | Select-Object -First 25)
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('# Phase-1 Cyclomatic Complexity Baseline')
    $lines.Add('')
    $lines.Add("Accepted production checkpoint: ``$($Report.scope.acceptedRef)``")
    $lines.Add("Ownership base: ``$($Report.scope.baseRef)`` (merge-base of ``origin/master`` and accepted HEAD)")
    $lines.Add('')
    $lines.Add('## Ownership and measurement scope')
    $lines.Add('')
    $lines.Add("Operator-authored commits in lineage: **$($Inventory.operatorAuthoredCommitCountInLineage)**")
    $lines.Add("Operator-authored commits touching scoped executable code: **$($Inventory.operatorAuthoredCodeCommitCountInScope)**")
    $lines.Add("Current executable files in scope: **$($Inventory.cyclomaticTargetFiles.Count)**")
    $lines.Add('')
    $lines.Add('The complete authorship and file inventory is in `complexity-ownership-inventory.json` and `.md`. Scope was derived from Git history and current `git blame`; it was not inferred from filenames alone.')
    $lines.Add('')
    $lines.Add('## Analyzer')
    $lines.Add('')
    $lines.Add("- C/C++: $($Report.analyzer.cpp)")
    $lines.Add("- PowerShell: $($Report.analyzer.powershell)")
    $lines.Add('- CMake/YAML/JSON/Markdown/license files are recorded for review but are not included in function-level CC statistics.')
    $lines.Add('')
    $lines.Add('## Baseline summary')
    $lines.Add('')
    $lines.Add((Format-StatTable $Report.summary))
    $lines.Add('')
    $lines.Add('The p90 uses nearest-rank: `ceil(0.90 * N)`. Statistics exclude PowerShell top-level script bodies, which are reported separately because cyclomatic complexity is a function-level metric.')
    $lines.Add('')
    $lines.Add('## Functions with CC > 5')
    $lines.Add('')
    $lines.Add((Format-FunctionList $functions 5))
    $lines.Add('')
    $lines.Add('## Functions with CC > 7')
    $lines.Add('')
    $lines.Add((Format-FunctionList $functions 7))
    $lines.Add('')
    $lines.Add('## Functions with CC > 10')
    $lines.Add('')
    $lines.Add((Format-FunctionList $functions 10))
    $lines.Add('')
    $lines.Add('## Top 25 scoped functions')
    $lines.Add('')
    $lines.Add('| Rank | Function | File | Lines | NLOC | CC | Params |')
    $lines.Add('|---:|---|---|---:|---:|---:|---:|')
    $rank = 1
    foreach ($function in $top) {
        $lines.Add("| $rank | ``$($function.function)`` | ``$($function.file)`` | $($function.startLine)-$($function.endLine) | $($function.nloc) | $($function.cyclomaticComplexity) | $($function.parameterCount) |")
        $rank++
    }
    $lines.Add('')
    $scriptBodies = @($Report.functions | Where-Object { $_.scopeKind -eq 'script-body' })
    if ($scriptBodies.Count -gt 0) {
        $lines.Add('## PowerShell top-level script bodies (reported separately)')
        $lines.Add('')
        $lines.Add('| File | NLOC | CC |')
        $lines.Add('|---|---:|---:|')
        foreach ($body in $scriptBodies) {
            $lines.Add("| ``$($body.file)`` | $($body.nloc) | $($body.cyclomaticComplexity) |")
        }
        $lines.Add('')
    }
    if (@($Report.limitations).Count -gt 0) {
        $lines.Add('## Measurement limitations')
        $lines.Add('')
        foreach ($limitation in $Report.limitations) {
            $lines.Add("- ``$($limitation.file)``: $($limitation.reason)")
        }
        $lines.Add('')
    }
    return $lines -join "`n"
}

function New-InventoryMarkdown {
    param([Parameter(Mandatory = $true)] [object] $Inventory)

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('# Phase-1 Complexity Ownership Inventory')
    $lines.Add('')
    $lines.Add("Accepted HEAD: ``$($Inventory.acceptedRef)``")
    $lines.Add("Ownership base: ``$($Inventory.baseRef)``")
    $lines.Add('')
    $lines.Add('## Discovery')
    $lines.Add('')
    $lines.Add('Ownership was derived from `git log BASE..accepted` author metadata, then file/function attribution was checked with current accepted-HEAD `git blame`. Archived WIP branches were not traversed.')
    $lines.Add('')
    $lines.Add("- GitHub account: **$($Inventory.githubAccount)**")
    $lines.Add("- Author-authored commits in accepted lineage: **$($Inventory.operatorAuthoredCommitCountInLineage)**")
    $lines.Add("- Author-authored commits touching current executable scope: **$($Inventory.operatorAuthoredCodeCommitCountInScope)**")
    $lines.Add('')
    $lines.Add('| Author name | Author email |')
    $lines.Add('|---|---|')
    foreach ($alias in $Inventory.operatorIdentityAliases) {
        $lines.Add("| $($alias.name) | ``$($alias.email)`` |")
    }
    $lines.Add('')
    $lines.Add('## Current executable files')
    $lines.Add('')
    $lines.Add('| File | Language | Status | Operator commits | Blame lines | Scoped functions |')
    $lines.Add('|---|---|---|---:|---:|---:|')
    foreach ($file in $Inventory.cyclomaticTargetFiles) {
        $lines.Add("| ``$($file.path)`` | $($file.language) | $($file.status) | $($file.operatorCommitCount) | $($file.operatorBlameLineCount) | $($file.scopedFunctionCount) |")
    }
    $lines.Add('')
    $lines.Add('## Non-CC changed paths')
    $lines.Add('')
    $lines.Add('These paths remain part of the authorship audit but are not function-level cyclomatic targets:')
    $lines.Add('')
    foreach ($file in $Inventory.nonCyclomaticChangedFiles) {
        $lines.Add("- ``$($file.path)`` — $($file.category)")
    }
    $lines.Add('')
    $lines.Add('## Scope rules')
    $lines.Add('')
    foreach ($rule in $Inventory.scopeRules) {
        $lines.Add("- $rule")
    }
    $lines.Add('')
    return $lines -join "`n"
}

function Find-BeforeMetric {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [hashtable] $Exact,
        [Parameter(Mandatory = $true)] [hashtable] $ByName
    )

    if ($Exact.ContainsKey([string] $Metric.key)) {
        return $Exact[[string] $Metric.key]
    }
    $fallback = @($ByName[[string] "$($Metric.language)|$($Metric.scopeKind)|$($Metric.file)|$($Metric.function)"])
    if ($fallback.Count -eq 1) {
        return $fallback[0]
    }
    return $null
}

function New-ComparisonMarkdown {
    param(
        [Parameter(Mandatory = $true)] [object] $Before,
        [Parameter(Mandatory = $true)] [object] $After,
        [Parameter(Mandatory = $true)] [object] $Inventory,
        [Parameter(Mandatory = $true)] [object[]] $Allowlist
    )

    $beforeExact = @{}
    $beforeByName = @{}
    foreach ($metric in $Before.functions) {
        $beforeExact[[string] $metric.key] = $metric
        $nameKey = "$($metric.language)|$($metric.scopeKind)|$($metric.file)|$($metric.function)"
        if (-not $beforeByName.ContainsKey($nameKey)) {
            $beforeByName[$nameKey] = [System.Collections.Generic.List[object]]::new()
        }
        $beforeByName[$nameKey].Add($metric)
    }
    $afterFunctions = @(Get-SortedFunctions $After.functions)
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('# Phase-1 Complexity Hardening Report')
    $lines.Add('')
    $lines.Add("Accepted starting SHA: ``$($Before.scope.acceptedRef)``")
    $lines.Add("Candidate measurement HEAD: ``$($After.scope.measurementHead)``")
    $lines.Add('')
    $lines.Add('## Before/after summary')
    $lines.Add('')
    $lines.Add('| Measure | Before | After |')
    $lines.Add('|---|---:|---:|')
    foreach ($name in @('scopedFunctionCount', 'averageCC', 'medianCC', 'p90CC', 'maximumCC', 'countCCGreater5', 'countCCGreater7', 'countCCGreater10')) {
        $label = switch ($name) {
            'scopedFunctionCount' { 'Scoped functions' }
            'averageCC' { 'Average CC' }
            'medianCC' { 'Median CC' }
            'p90CC' { '90th percentile CC' }
            'maximumCC' { 'Maximum CC' }
            'countCCGreater5' { 'Functions with CC > 5' }
            'countCCGreater7' { 'Functions with CC > 7' }
            'countCCGreater10' { 'Functions with CC > 10' }
        }
        $lines.Add("| $label | $($Before.summary.$name) | $($After.summary.$name) |")
    }
    $lines.Add('')
    $lines.Add('The p90 is nearest-rank `ceil(0.90 * N)`. Function statistics exclude PowerShell top-level script bodies.')
    $lines.Add('')
    $lines.Add('## Function-by-function comparison')
    $lines.Add('')
    $lines.Add('| Function | File | Before CC | After CC | Before NLOC | After NLOC | Notes |')
    $lines.Add('|---|---|---:|---:|---:|---:|---|')
    foreach ($afterMetric in $afterFunctions) {
        $beforeMetric = Find-BeforeMetric $afterMetric $beforeExact $beforeByName
        if ($beforeMetric) {
            $delta = [int] $afterMetric.cyclomaticComplexity - [int] $beforeMetric.cyclomaticComplexity
            $note = if ($delta -lt 0) { "reduced by $(-$delta)" } elseif ($delta -gt 0) { "increased by $delta" } else { 'unchanged' }
            $beforeCC = $beforeMetric.cyclomaticComplexity
            $beforeNloc = $beforeMetric.nloc
        } else {
            $note = 'new cohesive helper/function in scoped file'
            $beforeCC = '—'
            $beforeNloc = '—'
        }
        $lines.Add("| ``$($afterMetric.function)`` | ``$($afterMetric.file)`` | $beforeCC | $($afterMetric.cyclomaticComplexity) | $beforeNloc | $($afterMetric.nloc) | $note |")
    }
    $lines.Add('')
    $lines.Add('## Top remaining functions')
    $lines.Add('')
    $lines.Add((Format-FunctionList $afterFunctions 5))
    $lines.Add('')
    $lines.Add('## Remaining functions with CC > 7')
    $lines.Add('')
    $lines.Add((Format-FunctionList $afterFunctions 7))
    $lines.Add('')
    $lines.Add('## Remaining functions with CC > 10')
    $lines.Add('')
    $lines.Add((Format-FunctionList $afterFunctions 10))
    $lines.Add('')
    $lines.Add('## Intentional exceptions')
    $lines.Add('')
    if ($Allowlist.Count -eq 0) {
        $lines.Add('_Empty._')
    } else {
        $lines.Add('| File | Function | Measured CC | Reason | Date/task | Reviewer note |')
        $lines.Add('|---|---|---:|---|---|---|')
        foreach ($exception in $Allowlist) {
            $lines.Add("| ``$($exception.file)`` | ``$($exception.function)`` | $($exception.measuredCC) | $($exception.reason) | $($exception.dateTask) | $($exception.reviewerNote) |")
        }
    }
    $lines.Add('')
    if (@($After.limitations).Count -gt 0) {
        $lines.Add('## Measurement limitations')
        $lines.Add('')
        foreach ($limitation in $After.limitations) {
            $lines.Add("- ``$($limitation.file)``: $($limitation.reason)")
        }
        $lines.Add('')
    }
    $lines.Add('See `complexity-ownership-inventory.md` for the complete Git-derived attribution scope.')
    return $lines -join "`n"
}

function New-ReportObject {
    param(
        [Parameter(Mandatory = $true)] [object[]] $Metrics,
        [Parameter(Mandatory = $true)] [string] $ReportKind,
        [Parameter(Mandatory = $true)] [string] $Base,
        [Parameter(Mandatory = $true)] [string] $Accepted,
        [Parameter(Mandatory = $true)] [string] $MeasurementHead,
        [Parameter(Mandatory = $true)] [int] $LineageCommitCount,
        [Parameter(Mandatory = $true)] [string] $LizardVersion
    )

    return [ordered]@{
        schemaVersion = 1
        reportKind    = $ReportKind
        scope         = [ordered]@{
            baseRef          = $Base
            acceptedRef      = $Accepted
            measurementHead  = $MeasurementHead
            currentScopeRule = 'C/C++ functions in current files introduced or materially modified by operator commits; PowerShell functions in current operator-authored test scripts; complete functions selected by Git blame/diff attribution.'
        }
        analyzer      = [ordered]@{
            cpp        = "lizard $LizardVersion (C/C++ parser, CSV output, default CCN)"
            powershell = "PowerShell $($PSVersionTable.PSVersion) System.Management.Automation.Language.Parser AST"
            nloc       = 'lizard NLOC for C/C++; nonblank non-comment line count for PowerShell'
            p90        = 'nearest-rank ceil(0.90 * N)'
        }
        summary       = Get-Statistics $Metrics
        functions    = @($Metrics | Sort-Object @{ Expression = { [string] $_.file } }, @{ Expression = { [int] $_.startLine } }, @{ Expression = { [string] $_.function } })
        limitations  = @($limitations)
        lineage      = [ordered]@{ operatorAuthoredCommitCount = $LineageCommitCount }
    }
}

$acceptedResolved = Resolve-Commit $AcceptedRef
if (-not $BaseRef) {
    $BaseRef = (@(Invoke-GitLines @('merge-base', 'origin/master', $acceptedResolved)) | Select-Object -First 1).Trim()
}
$baseResolved = Resolve-Commit $BaseRef
$range = "$baseResolved..$acceptedResolved"
$measurementHead = (@(Invoke-GitLines @('rev-parse', 'HEAD')) | Select-Object -First 1).Trim()

$history = @(Get-CommitRecords $range)
$discoveredAliases = @($history | Where-Object {
        ([string] $_.Email) -match '(?i)ymgpwcca' -or [string] $_.Name -ceq $knownOperatorName
    } | Select-Object Name, Email -Unique)
$operatorEmails = @($knownOperatorEmails + @($discoveredAliases | ForEach-Object { $_.Email })) |
    ForEach-Object { ([string] $_).ToLowerInvariant() } | Sort-Object -Unique
$operatorEmailSet = @{}
foreach ($email in $operatorEmails) {
    $operatorEmailSet[$email] = $true
}
$operatorPredicate = Get-OperatorPredicate $operatorEmailSet
$operatorCommits = @($history | Where-Object { & $operatorPredicate $_ })
if ($operatorCommits.Count -eq 0) {
    throw 'No operator-authored commits were found in the accepted production lineage.'
}

$changedPaths = @(Get-ChangedPathRecords $range)
$currentChanged = @($changedPaths | Where-Object { Test-PathAtRef $acceptedResolved $_.Path })
$codePaths = @($currentChanged | Where-Object { Get-PathLanguage $_.Path })
$cppPaths = @($codePaths | Where-Object { (Get-PathLanguage $_.Path) -eq 'cpp' } | ForEach-Object { $_.Path } | Sort-Object -Unique)
$scriptPaths = @($codePaths | Where-Object { (Get-PathLanguage $_.Path) -eq 'powershell-or-script' } | ForEach-Object { $_.Path } | Sort-Object -Unique)

$operatorCodeCommitRecords = [System.Collections.Generic.List[object]]::new()
$codePathSet = @{}
foreach ($path in $cppPaths + $scriptPaths) {
    $codePathSet[$path] = $true
}
foreach ($commit in $operatorCommits) {
    $commitPaths = @(Invoke-GitLines @('diff-tree', '--no-commit-id', '--name-only', '-r', $commit.Hash) |
        ForEach-Object { Normalize-RepoPath $_ })
    $hits = @($commitPaths | Where-Object { $codePathSet.ContainsKey($_) } | Sort-Object -Unique)
    if ($hits.Count -gt 0) {
        $operatorCodeCommitRecords.Add([pscustomobject]@{
                hash    = $commit.Hash
                name    = $commit.Name
                email   = $commit.Email
                subject = $commit.Subject
                files   = $hits
            })
    }
}

$fileLines = @{}
foreach ($path in $cppPaths + $scriptPaths) {
    $fileLines[$path] = Get-OperatorBlameLines $acceptedResolved $path $operatorEmailSet
}

$baselineKeys = @{}
$beforePathResolved = if (Test-Path -LiteralPath (Join-Path $repoRoot $BeforePath)) {
    try { Read-JsonFile $BeforePath } catch { $null }
} else { $null }
if ($beforePathResolved) {
    foreach ($metric in $beforePathResolved.functions) {
        $baselineKeys[[string] $metric.key] = $true
    }
}

$candidateLines = @{}
foreach ($path in $cppPaths) {
    $candidateLines[$path] = Get-CandidateChangedLines $acceptedResolved $path
}

$python = Get-PythonExecutable
$cppMetrics = @(Get-ScopedCppMetrics $cppPaths $fileLines $Mode $baselineKeys $candidateLines $python)
$scriptMetrics = @(Get-PowerShellMetrics $scriptPaths $fileLines $Mode)
$metrics = @($cppMetrics + $scriptMetrics)
if ($metrics.Count -eq 0) {
    throw 'No scoped functions were measured.'
}
$lizardVersion = Get-LizardVersion $python

$inventoryFiles = [System.Collections.Generic.List[object]]::new()
foreach ($pathRecord in ($codePaths | Sort-Object Path)) {
    $path = $pathRecord.Path
    $language = Get-PathLanguage $path
    $operatorFileCommits = @($operatorCodeCommitRecords | Where-Object { $_.files -contains $path })
    $scopedCount = @($metrics | Where-Object { $_.file -eq $path -and $_.scopeKind -eq 'function' }).Count
    $inventoryFiles.Add([ordered]@{
            path                     = $path
            language                 = if ($language -eq 'cpp') { 'C/C++' } else { 'PowerShell' }
            status                   = $pathRecord.Status
            operatorCommitCount      = $operatorFileCommits.Count
            operatorCommits          = @($operatorFileCommits | ForEach-Object { $_.hash })
            operatorBlameLineCount   = $fileLines[$path].Count
            scopedFunctionCount      = $scopedCount
            currentFunctionKeys      = @($metrics | Where-Object { $_.file -eq $path -and $_.scopeKind -eq 'function' } | ForEach-Object { $_.key })
        })
}

$nonCyclomatic = [System.Collections.Generic.List[object]]::new()
foreach ($pathRecord in ($currentChanged | Where-Object { -not ($codePathSet.ContainsKey($_.Path)) } | Sort-Object Path)) {
    $path = $pathRecord.Path
    $extension = [IO.Path]::GetExtension($path).ToLowerInvariant()
    $category = if ([IO.Path]::GetFileName($path) -eq 'CMakeLists.txt' -or $extension -eq '.cmake') {
        'CMake/control script; reviewed separately because lizard is not the chosen analyzer.'
    } elseif ($extension -in @('.yaml', '.yml')) {
        'YAML workflow/declaration; excluded from cyclomatic targets.'
    } elseif ($extension -eq '.json') {
        'Static JSON/configuration; excluded from cyclomatic targets.'
    } elseif ($extension -eq '.md') {
        'Markdown/project documentation; excluded from cyclomatic targets.'
    } elseif ($path -eq 'COPYING' -or $extension -eq '.txt') {
        'License/plain text; excluded from cyclomatic targets.'
    } else {
        'Non-target changed path; excluded from cyclomatic targets by language rule.'
    }
    $nonCyclomatic.Add([ordered]@{ path = $path; status = $pathRecord.Status; category = $category })
}

$aliases = @($history | Where-Object { & $operatorPredicate $_ } |
    Select-Object @{ Name = 'name'; Expression = { $_.Name } }, @{ Name = 'email'; Expression = { $_.Email } } -Unique)
$inventory = [ordered]@{
    schemaVersion                         = 1
    githubAccount                         = 'YMGPwcca'
    baseRef                               = $baseResolved
    acceptedRef                           = $acceptedResolved
    discoveryMethod                       = 'git log BASE..accepted author metadata plus accepted-HEAD git blame; no filename ownership inference; archived WIP branches excluded'
    operatorIdentityAliases               = @($aliases)
    operatorAuthoredCommitCountInLineage = $operatorCommits.Count
    operatorAuthoredCodeCommitCountInScope = $operatorCodeCommitRecords.Count
    operatorAuthoredCommits               = @($operatorCommits | ForEach-Object {
            [ordered]@{ hash = $_.Hash; name = $_.Name; email = $_.Email; subject = $_.Subject }
        })
    cyclomaticTargetFiles                 = @($inventoryFiles)
    nonCyclomaticChangedFiles             = @($nonCyclomatic)
    scopeRules                            = @(
        'C/C++ scope is the complete current function when accepted-HEAD blame attributes at least one line in the function to an operator commit; newly added files are fully scoped.',
        'PowerShell scope includes all current functions in the five operator-authored integration scripts; top-level script bodies are measured separately with the AST parser.',
        'CMake/control files are included in the ownership inventory and reviewed separately, not treated as function-level CC targets.',
        'Markdown, YAML, static JSON, license text, and other declarations are recorded but excluded from CC metrics.',
        'Current source is measured at the accepted checkpoint for Baseline and at the candidate working tree/HEAD for After and Check.'
    )
}

$reportKind = switch ($Mode) {
    'Baseline' { 'baseline' }
    'After' { 'after' }
    'Check' { 'check' }
}
$report = New-ReportObject $metrics $reportKind $baseResolved $acceptedResolved $measurementHead $operatorCommits.Count $lizardVersion

if ($Mode -eq 'Baseline') {
    $jsonText = $report | ConvertTo-Json -Depth 30
    Write-Utf8File $InventoryPath ($inventory | ConvertTo-Json -Depth 30)
    Write-Utf8File ($InventoryPath -replace '\.json$', '.md') (New-InventoryMarkdown $inventory)
    $baselineOutput = if ($JsonPath) { $JsonPath } else { 'complexity-baseline.json' }
    $baselineMarkdown = if ($MarkdownPath) { $MarkdownPath } else { 'complexity-baseline.md' }
    Write-Utf8File $baselineOutput $jsonText
    Write-Utf8File $baselineMarkdown (New-BaselineMarkdown $report $inventory)
    Write-Utf8File $BeforePath $jsonText
    Write-Output "Baseline written: $baselineOutput"
    Write-Output "Baseline markdown written: $baselineMarkdown"
    Write-Output "Ownership inventory written: $InventoryPath"
    Write-Output ((Format-StatTable $report.summary))
    exit 0
}

if ($Mode -eq 'After') {
    $before = if (Test-Path -LiteralPath (Join-Path $repoRoot $BeforePath)) { Read-JsonFile $BeforePath } else { throw "Before report '$BeforePath' was not found." }
    $allowlist = if (Test-Path -LiteralPath (Join-Path $repoRoot $AllowlistPath)) {
        @((Read-JsonFile $AllowlistPath))
    } else { @() }
    $jsonText = $report | ConvertTo-Json -Depth 30
    $afterOutput = if ($JsonPath) { $JsonPath } else { 'complexity-after.json' }
    $afterMarkdown = if ($MarkdownPath) { $MarkdownPath } else { 'complexity-report.md' }
    Write-Utf8File $afterOutput $jsonText
    Write-Utf8File $afterMarkdown (New-ComparisonMarkdown $before $report $inventory $allowlist)
    Write-Output "After report written: $afterOutput"
    Write-Output "Comparison report written: $afterMarkdown"
    Write-Output ((Format-StatTable $report.summary))
    exit 0
}

$baseline = if (Test-Path -LiteralPath (Join-Path $repoRoot $BaselinePath)) {
    Read-JsonFile $BaselinePath
} else {
    throw "Accepted complexity baseline '$BaselinePath' was not found. Run After first or pass -BaselinePath."
}
$allowlist = if (Test-Path -LiteralPath (Join-Path $repoRoot $AllowlistPath)) {
    @((Read-JsonFile $AllowlistPath))
} else { @() }
$baselineByKey = @{}
foreach ($metric in $baseline.functions) {
    $baselineByKey[[string] $metric.key] = $metric
}
$allowByFunction = @{}
foreach ($exception in $allowlist) {
    $allowByFunction["$($exception.file)|$($exception.function)"] = $exception
}
$violations = [System.Collections.Generic.List[string]]::new()
foreach ($metric in @($report.functions | Where-Object { $_.scopeKind -eq 'function' })) {
    $baselineMetric = if ($baselineByKey.ContainsKey([string] $metric.key)) { $baselineByKey[[string] $metric.key] } else { $null }
    $exceptionKey = "$($metric.file)|$($metric.function)"
    $exception = if ($allowByFunction.ContainsKey($exceptionKey)) { $allowByFunction[$exceptionKey] } else { $null }
    $isException = $false
    if ($exception) {
        $isException = [int] $exception.measuredCC -eq [int] $metric.cyclomaticComplexity
    }
    if ([int] $metric.cyclomaticComplexity -gt 10 -and -not $isException) {
        $violations.Add("CC > 10: $($metric.file):$($metric.startLine) $($metric.function) measured $($metric.cyclomaticComplexity).")
    }
    if ($baselineMetric -and [int] $metric.cyclomaticComplexity -gt [int] $baselineMetric.cyclomaticComplexity -and -not $isException) {
        $violations.Add("Complexity increased: $($metric.file):$($metric.startLine) $($metric.function) baseline $($baselineMetric.cyclomaticComplexity), current $($metric.cyclomaticComplexity).")
    }
    if (-not $baselineMetric -and [int] $metric.cyclomaticComplexity -gt 10 -and -not $isException) {
        $violations.Add("New function exceeds CC 10: $($metric.file):$($metric.startLine) $($metric.function) measured $($metric.cyclomaticComplexity).")
    }
}
Write-Output ((Format-StatTable $report.summary))
if ($violations.Count -gt 0) {
    throw "Complexity regression gate failed:`n$($violations -join "`n")"
}
Write-Output 'Complexity regression gate: PASS'
