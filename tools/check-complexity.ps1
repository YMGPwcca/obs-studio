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
    [string] $IdentityMigrationsPath = 'complexity-identity-migrations.json',
    [string] $LizardPythonPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repoRoot

$cppExtensions = @(
    '.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx',
    '.inc', '.inl', '.ipp', '.tcc', '.cppm', '.ixx'
)
$powershellExtensions = @('.ps1', '.psm1')
$unsupportedExecutableExtensions = @(
    '.py', '.pyw', '.lua', '.sh', '.bash', '.zsh', '.fish', '.bat', '.cmd',
    '.js', '.mjs', '.cjs', '.ts', '.tsx', '.rb', '.pl', '.pm', '.php',
    '.go', '.rs', '.java', '.kt', '.kts', '.swift', '.m', '.mm'
)
$nonExecutableExtensions = @(
    '.cmake', '.css', '.csv', '.diff', '.html', '.ini', '.json', '.md',
    '.patch', '.plist', '.rc', '.rst', '.scss', '.svg', '.template',
    '.toml', '.txt', '.xml', '.yaml', '.yml'
)
$nonExecutableNames = @(
    '.clang-format', '.editorconfig', '.gitignore', 'CMakeLists.txt',
    'Dockerfile', 'LICENSE', 'COPYING', 'Makefile'
)
$knownOperatorEmails = @(
    '37042810+YMGPwcca@users.noreply.github.com',
    'ymgpwcca@proton.me'
)
$knownOperatorName = 'YMGPwcca'
$acceptedBaselineBlob = '6484fa71b27c917bffdbabd09fa170a2e8a4820b'
$limitations = [System.Collections.Generic.List[object]]::new()

function Get-RepoFilePath {
    param([Parameter(Mandatory = $true)] [string] $Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function New-ExactMap {
    return [System.Collections.Hashtable]::new([StringComparer]::Ordinal)
}

function Test-ExactStringInList {
    param(
        [Parameter(Mandatory = $true)] [string] $Value,
        [AllowEmptyCollection()] [string[]] $Values
    )

    foreach ($candidate in $Values) {
        if ($candidate -ceq $Value) {
            return $true
        }
    }
    return $false
}

function Write-Utf8File {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Contents
    )

    $fullPath = Get-RepoFilePath $Path
    $parent = Split-Path -Parent $fullPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [IO.File]::WriteAllText($fullPath, $Contents, [Text.UTF8Encoding]::new($false))
}

function Read-JsonFile {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $fullPath = Get-RepoFilePath $Path
    return (Get-Content -LiteralPath $fullPath -Raw | ConvertFrom-Json)
}

function Assert-TrustedBaselineBlob {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $FullPath
    )

    $actualBlob = (@(Invoke-GitLines @('hash-object', '--path', $Path, $FullPath)) | Select-Object -First 1).Trim()
    if ($actualBlob -cne $acceptedBaselineBlob) {
        throw "Accepted complexity baseline '$Path' failed integrity validation: Git blob $actualBlob does not match the pinned accepted artifact."
    }
}

function Read-TrustedBaselineDocument {
    param([Parameter(Mandatory = $true)] [string] $Path)

    try {
        $document = Read-JsonFile $Path
    } catch {
        throw "Accepted complexity baseline '$Path' is not valid JSON: $($_.Exception.Message)"
    }
    if ($null -eq $document -or $document -is [Array]) {
        throw "Accepted complexity baseline '$Path' must be a JSON object."
    }
    return $document
}

function Assert-TrustedBaselineReportShape {
    param(
        [Parameter(Mandatory = $true)] [object] $Document,
        [Parameter(Mandatory = $true)] [string] $Path
    )

    $schema = $document.PSObject.Properties['schemaVersion']
    $kind = $document.PSObject.Properties['reportKind']
    $scopeProperty = $document.PSObject.Properties['scope']
    $functions = $document.PSObject.Properties['functions']
    if ($null -eq $schema -or $null -eq $kind -or $null -eq $scopeProperty -or $null -eq $functions) {
        throw "Accepted complexity baseline '$Path' is missing required report fields."
    }
    if ([int] $schema.Value -ne 1 -or [string] $kind.Value -cne 'after') {
        throw "Accepted complexity baseline '$Path' has an unsupported schema or report kind."
    }
    if ($functions.Value -isnot [Array] -or @($functions.Value).Count -eq 0) {
        throw "Accepted complexity baseline '$Path' must contain a non-empty functions array."
    }
}

function Assert-TrustedBaselineScope {
    param(
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $BaseRef,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef
    )

    if ($null -eq $scope -or $scope -is [Array]) {
        throw "Accepted complexity baseline '$Path' has an invalid scope object."
    }
    $scopeBase = $scope.PSObject.Properties['baseRef']
    $scopeAccepted = $scope.PSObject.Properties['acceptedRef']
    $scopeMeasurement = $scope.PSObject.Properties['measurementHead']
    if ($null -eq $scopeBase -or $null -eq $scopeAccepted -or $null -eq $scopeMeasurement) {
        throw "Accepted complexity baseline '$Path' has incomplete scope provenance."
    }
    if ([string] $scopeBase.Value -cne $BaseRef -or [string] $scopeAccepted.Value -cne $AcceptedRef) {
        throw "Accepted complexity baseline '$Path' is anchored to a different ownership or accepted reference."
    }
    try {
        $null = Resolve-Commit ([string] $scopeMeasurement.Value)
    } catch {
        throw "Accepted complexity baseline '$Path' has an invalid measurement provenance: $($_.Exception.Message)"
    }
}

function Read-TrustedAcceptedBaseline {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $BaseRef,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef
    )

    $fullPath = Get-RepoFilePath $Path
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Accepted complexity baseline '$Path' was not found."
    }
    Assert-TrustedBaselineBlob $Path $fullPath
    $document = Read-TrustedBaselineDocument $Path
    Assert-TrustedBaselineReportShape $document $Path
    $scope = $document.PSObject.Properties['scope'].Value
    Assert-TrustedBaselineScope $scope $Path $BaseRef $AcceptedRef
    return $document
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

    $normalized = (($Path -replace '\\', '/') -replace '^\./', '')
    $normalizedRoot = (($repoRoot -replace '\\', '/').TrimEnd('/'))
    $rootPrefix = "$normalizedRoot/"
    if ($normalized.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        return $normalized.Substring($rootPrefix.Length)
    }
    return $normalized
}

function Get-PathLanguage {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $extension = [IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($cppExtensions -contains $extension) {
        return 'cpp'
    }
    if ($powershellExtensions -contains $extension) {
        return 'powershell'
    }
    if ($unsupportedExecutableExtensions -contains $extension) {
        return 'unsupported'
    }
    if ($nonExecutableNames -contains [IO.Path]::GetFileName($Path) -or
        $nonExecutableExtensions -contains $extension) {
        return 'non-executable'
    }
    return 'unknown'
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
    param(
        [Parameter(Mandatory = $true)] [hashtable] $EmailSet,
        [Parameter(Mandatory = $true)] [hashtable] $NameSet
    )

    return ({
        param($Commit)
        $email = ([string] $Commit.Email).ToLowerInvariant()
        $name = [string] $Commit.Name
        return $EmailSet.ContainsKey($email) -or $NameSet.ContainsKey($name)
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
        $records.Add((New-PathRecord $parts))
    }
    return @($records)
}

function Get-StagedPathRecords {
    param([Parameter(Mandatory = $true)] [string] $Head)

    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($line in (Invoke-GitLines @('diff', '--cached', '--name-status', '--find-renames', $Head))) {
        $parts = $line -split "`t"
        if ($parts.Count -ge 2) {
            $records.Add((New-PathRecord $parts))
        }
    }
    return @($records)
}

function New-PathRecord {
    param([Parameter(Mandatory = $true)] [string[]] $Parts)

    $status = [string] $Parts[0]
    $oldPath = $null
    if ($status -match '^[RC]\d+$' -and $Parts.Count -ge 3) {
        $oldPath = Normalize-RepoPath $Parts[1]
    }
    return [pscustomobject]@{
        Status = $status
        Path   = Normalize-RepoPath $Parts[$Parts.Count - 1]
        OldPath = $oldPath
    }
}

function Get-CommitPathRecords {
    param([Parameter(Mandatory = $true)] [string] $CommitHash)

    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($line in (Invoke-GitLines @('diff-tree', '--no-commit-id', '--root', '--name-status', '--find-renames', '-r', $CommitHash))) {
        $parts = $line -split "`t"
        if ($parts.Count -ge 2) {
            $records.Add((New-PathRecord $parts))
        }
    }
    return @($records)
}

function Get-UntrackedPathRecords {
    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($path in (Invoke-GitLines @('ls-files', '--others', '--exclude-standard'))) {
        if ($path) {
            $records.Add([pscustomobject]@{
                    Status  = 'A'
                    Path    = Normalize-RepoPath $path
                    OldPath = $null
                })
        }
    }
    return @($records)
}

function Get-OperatorBlameLines {
    param(
        [Parameter(Mandatory = $true)] [string] $Ref,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [hashtable] $EmailSet,
        [Parameter(Mandatory = $true)] [hashtable] $NameSet
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
        if ($line.StartsWith('author ')) {
            $isOperator = $NameSet.ContainsKey($line.Substring('author '.Length))
            continue
        }
        if ($line.StartsWith('author-mail ')) {
            $email = $line.Substring('author-mail '.Length).Trim('<', '>').ToLowerInvariant()
            $isOperator = $isOperator -or $EmailSet.ContainsKey($email)
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
    return ,$lineSet
}

function Test-LineIntersection {
    param(
        [object] $Lines,
        [Parameter(Mandatory = $true)] [int] $StartLine,
        [Parameter(Mandatory = $true)] [int] $EndLine
    )

    if ($null -eq $Lines) {
        return $false
    }
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
    return ,$lineSet
}

function Test-CurrentPath {
    param([Parameter(Mandatory = $true)] [string] $Path)

    return Test-Path -LiteralPath (Join-Path $repoRoot $Path) -PathType Leaf
}

function Get-WorkingTreeRecreatedPaths {
    param([Parameter(Mandatory = $true)] [string] $Head)

    $deletedPaths = New-ExactMap
    foreach ($record in (Get-StagedPathRecords $Head)) {
        if ($record.Status -eq 'D') {
            $deletedPaths[[string] $record.Path] = $true
        }
    }
    foreach ($record in (Get-ChangedPathRecords $Head)) {
        if ($record.Status -eq 'D') {
            $deletedPaths[[string] $record.Path] = $true
        }
    }
    $recreatedPaths = New-ExactMap
    foreach ($path in $deletedPaths.Keys) {
        if (Test-CurrentPath ([string] $path)) {
            $recreatedPaths[[string] $path] = $true
        }
    }
    return $recreatedPaths
}

function Get-AllFileLines {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $lineSet = [System.Collections.Generic.HashSet[int]]::new()
    $lineCount = @(Get-Content -LiteralPath (Join-Path $repoRoot $Path)).Count
    for ($line = 1; $line -le $lineCount; $line++) {
        $null = $lineSet.Add($line)
    }
    return ,$lineSet
}

function Get-OperatorCommitPathRecords {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Commits,
        [Parameter(Mandatory = $true)] [scriptblock] $OperatorPredicate
    )

    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($commit in $Commits) {
        if (-not (& $OperatorPredicate $commit)) {
            continue
        }
        foreach ($record in (Get-CommitPathRecords ([string] $commit.Hash))) {
            $records.Add($record)
        }
    }
    return @($records)
}

function Update-PostAcceptedPathProvenance {
    param(
        [Parameter(Mandatory = $true)] [hashtable] $OwnedPaths,
        [Parameter(Mandatory = $true)] [hashtable] $LineagePaths,
        [Parameter(Mandatory = $true)] [object] $Record,
        [Parameter(Mandatory = $true)] [bool] $OperatorCommit
    )

    if ($OperatorCommit) {
        $LineagePaths[[string] $Record.Path] = $true
        if ($Record.OldPath) {
            $LineagePaths[[string] $Record.OldPath] = $true
        }
    }
    if ($Record.Status -eq 'D') {
        $null = $OwnedPaths.Remove([string] $Record.Path)
        return
    }
    if ($Record.Status -match '^R' -and $Record.OldPath) {
        $sourceWasOwned = $OwnedPaths.ContainsKey([string] $Record.OldPath)
        $null = $OwnedPaths.Remove([string] $Record.OldPath)
        if ($OperatorCommit -or $sourceWasOwned) {
            $OwnedPaths[[string] $Record.Path] = $true
            $LineagePaths[[string] $Record.Path] = $true
        }
        return
    }
    if ($OperatorCommit) {
        $OwnedPaths[[string] $Record.Path] = $true
    }
}

function Get-PostAcceptedOperatorPathSets {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Commits,
        [Parameter(Mandatory = $true)] [scriptblock] $OperatorPredicate
    )

    $ownedPaths = New-ExactMap
    $lineagePaths = New-ExactMap
    foreach ($commit in $Commits) {
        $operatorCommit = [bool] (& $OperatorPredicate $commit)
        foreach ($record in (Get-CommitPathRecords ([string] $commit.Hash))) {
            Update-PostAcceptedPathProvenance $ownedPaths $lineagePaths $record $operatorCommit
        }
    }
    return [pscustomobject]@{
        current  = $ownedPaths
        lineage  = $lineagePaths
    }
}

function Get-HistoricalPathAliases {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [string[]] $HistoricalPaths,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $RenameRecords
    )

    $aliases = New-ExactMap
    foreach ($path in $HistoricalPaths) {
        $aliases[$path] = [System.Collections.Generic.List[string]]::new()
        $aliases[$path].Add($path)
    }
    foreach ($record in $RenameRecords) {
        if (-not $record.OldPath -or -not $aliases.ContainsKey([string] $record.OldPath)) {
            continue
        }
        $current = [System.Collections.Generic.List[string]]::new()
        foreach ($path in $aliases[[string] $record.OldPath]) {
            $current.Add($path)
        }
        $aliases[[string] $record.Path] = $current
    }
    return $aliases
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
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [string[]] $Files,
        [Parameter(Mandatory = $true)] [string] $Python
    )

    if (@($Files).Count -eq 0) {
        return @()
    }

    $oldPythonPath = $env:PYTHONPATH
    if ($LizardPythonPath) {
        $separator = [IO.Path]::PathSeparator
        $env:PYTHONPATH = if ($oldPythonPath) { "$LizardPythonPath$separator$oldPythonPath" } else { $LizardPythonPath }
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
        $separator = [IO.Path]::PathSeparator
        $env:PYTHONPATH = if ($oldPythonPath) { "$LizardPythonPath$separator$oldPythonPath" } else { $LizardPythonPath }
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

function Get-AstDecisionWeight {
    param(
        [Parameter(Mandatory = $true)] $Node,
        [Parameter(Mandatory = $true)] [hashtable] $Types
    )

    if (Test-AstType $Node $Types 'FunctionDefinitionAst') {
        return 0
    }
    if (Test-AstType $Node $Types 'IfStatementAst') {
        return [Math]::Max(1, @($Node.Clauses).Count)
    }
    $loopNames = @('ForStatementAst', 'ForEachStatementAst', 'WhileStatementAst', 'DoWhileStatementAst', 'DoUntilStatementAst')
    if (@($loopNames | Where-Object { Test-AstType $Node $Types $_ }).Count -gt 0) {
        return 1
    }
    if (Test-AstType $Node $Types 'SwitchStatementAst') {
        return @($Node.Clauses).Count
    }
    $simpleNames = @('CatchClauseAst', 'TernaryExpressionAst', 'TrapStatementAst')
    if (@($simpleNames | Where-Object { Test-AstType $Node $Types $_ }).Count -gt 0) {
        return 1
    }
    if ((Test-AstType $Node $Types 'BinaryExpressionAst') -and ([string] $Node.Operator -match 'And|Or')) {
        return 1
    }
    return 0
}

function Get-AstCyclomaticComplexity {
    param(
        [Parameter(Mandatory = $true)] $Root,
        [object[]] $ExcludedFunctionRanges,
        [Parameter(Mandatory = $true)] [hashtable] $Types
    )

    $complexity = 1
    foreach ($node in @($Root.FindAll({ param($candidate) $true }, $true))) {
        if (-not (Test-AstInsideRange $node $ExcludedFunctionRanges)) {
            $complexity += Get-AstDecisionWeight $node $Types
        }
    }
    return $complexity
}

function Get-FunctionRanges {
    param([Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $Functions)

    return @($Functions | ForEach-Object {
            [pscustomobject]@{
                StartOffset = $_.Extent.StartOffset
                EndOffset   = $_.Extent.EndOffset
                StartLine   = $_.Extent.StartLineNumber
                EndLine     = $_.Extent.EndLineNumber
            }
        })
}

function ConvertTo-FunctionRangeArray {
    param([Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $FunctionRanges)

    if ($null -eq $FunctionRanges) {
        return [object[]]::new(0)
    }
    return [object[]] $FunctionRanges
}

function Get-RelatedBaselineMetrics {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    if ($Scope.IsRecreated) {
        return @()
    }
    $matches = [System.Collections.Generic.List[object]]::new()
    $paths = New-ExactMap
    $paths[[string] $Metric.file] = $true
    foreach ($path in $Scope.HistoricalPaths) {
        $paths[[string] $path] = $true
    }
    foreach ($path in $paths.Keys) {
        $key = Get-FunctionKey ([string] $Metric.language) $path ([string] $Metric.function) ([string] $Metric.signature) ([string] $Metric.scopeKind)
        if ($BaselineByKey.ContainsKey($key)) {
            $matches.Add($BaselineByKey[$key])
        }
    }
    return @($matches)
}

function Get-ObjectPropertyValue {
    param(
        [Parameter(Mandatory = $true)] [object] $Object,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($property) {
        return $property.Value
    }
    return $null
}

function Test-ExceptionIdentity {
    param(
        [Parameter(Mandatory = $true)] [object] $Exception,
        [Parameter(Mandatory = $true)] [object] $Metric
    )

    return [string](Get-ObjectPropertyValue $Exception 'language') -ceq [string] $Metric.language -and
        [string](Get-ObjectPropertyValue $Exception 'scopeKind') -ceq [string] $Metric.scopeKind -and
        (Normalize-RepoPath ([string](Get-ObjectPropertyValue $Exception 'file'))) -ceq [string] $Metric.file -and
        [string](Get-ObjectPropertyValue $Exception 'function') -ceq [string] $Metric.function -and
        [string](Get-ObjectPropertyValue $Exception 'signature') -ceq [string] $Metric.signature
}

function Validate-Allowlist {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $Allowlist,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    foreach ($exception in $Allowlist) {
        $required = @('language', 'scopeKind', 'file', 'function', 'signature', 'baselineKey', 'measuredCC')
        $missing = @($required | Where-Object { -not $exception.PSObject.Properties[$_] })
        if (@($missing).Count -gt 0) {
            throw "Complexity exception is missing required fields: $($missing -join ', ')."
        }
        $baselineKey = [string] $exception.baselineKey
        if (-not $BaselineByKey.ContainsKey($baselineKey)) {
            throw "Complexity exception baselineKey '$baselineKey' is not present in the accepted complexity baseline."
        }
        $baselineMetric = $BaselineByKey[$baselineKey]
        if (-not (Test-ExceptionIdentity $exception $baselineMetric)) {
            throw "Complexity exception identity does not exactly match baselineKey '$baselineKey'."
        }
        if ([int] $exception.measuredCC -ne [int] $baselineMetric.cyclomaticComplexity) {
            throw "Complexity exception measuredCC does not match baselineKey '$baselineKey'."
        }
    }
}

function Get-ExactException {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [object] $BaselineMetric,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $Allowlist
    )

    $matches = @($Allowlist | Where-Object { $null -ne $_ } | Where-Object { Test-ExceptionIdentity $_ $Metric })
    if (@($matches).Count -gt 1) {
        throw "Multiple complexity exceptions match $($Metric.file):$($Metric.startLine) $($Metric.function)."
    }
    if (@($matches).Count -eq 0 -or $null -eq $BaselineMetric) {
        return $null
    }
    if ([string] $matches[0].baselineKey -ne [string] $BaselineMetric.key) {
        return $null
    }
    return $matches[0]
}

function New-EmptyIdentityMaps {
    return [pscustomobject]@{
        entries        = @()
        byCurrentKey   = New-ExactMap
        byBaselineKey  = New-ExactMap
    }
}

function Get-ExactJsonPropertyValue {
    param(
        [Parameter(Mandatory = $true)] [object] $Object,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    $properties = @($Object.PSObject.Properties | Where-Object { $_.Name -ceq $Name })
    if (@($properties).Count -gt 1) {
        throw "JSON object contains duplicate property '$Name'."
    }
    if (@($properties).Count -eq 1) {
        return $properties[0].Value
    }
    return $null
}

function Get-IdentityStringField {
    param(
        [Parameter(Mandatory = $true)] [object] $Object,
        [Parameter(Mandatory = $true)] [string] $Name,
        [Parameter(Mandatory = $true)] [string] $Context
    )

    $value = Get-ExactJsonPropertyValue $Object $Name
    if ($null -eq $value -or $value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) {
        throw "$Context field '$Name' must be a non-empty JSON string."
    }
    return $value
}

function Assert-ExactJsonShape {
    param(
        [Parameter(Mandatory = $true)] [object] $Object,
        [Parameter(Mandatory = $true)] [string[]] $Allowed,
        [Parameter(Mandatory = $true)] [string] $Context
    )

    $unexpected = @($Object.PSObject.Properties | Where-Object {
            -not (Test-ExactStringInList $_.Name $Allowed)
        })
    if (@($unexpected).Count -gt 0) {
        throw "$Context contains unsupported property '$($unexpected[0].Name)'."
    }
}

function Read-IdentityMigrationEntries {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $fullPath = Get-RepoFilePath $Path
    $raw = Get-Content -LiteralPath $fullPath -Raw
    $jsonDocument = $null
    try {
        $jsonDocument = [System.Text.Json.JsonDocument]::Parse($raw)
        Assert-JsonUniqueProperties $jsonDocument.RootElement $Path
        $document = ConvertFrom-Json -InputObject $raw -NoEnumerate
    } catch {
        throw "Could not parse identity migration document '$Path': $($_.Exception.Message)"
    } finally {
        if ($null -ne $jsonDocument) {
            $jsonDocument.Dispose()
        }
    }
    if ($null -eq $document -or $document -isnot [Array]) {
        throw "Identity migration document '$Path' must be a JSON array."
    }
    foreach ($entry in $document) {
        if ($null -eq $entry -or $entry -is [Array] -or $entry -isnot [pscustomobject]) {
            throw "Identity migration document '$Path' contains a non-object entry."
        }
    }
    return ,$document
}

function Assert-JsonUniqueProperties {
    param(
        [Parameter(Mandatory = $true)] [System.Text.Json.JsonElement] $Element,
        [Parameter(Mandatory = $true)] [string] $Context
    )

    if ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object) {
        $seen = New-ExactMap
        foreach ($property in $Element.EnumerateObject()) {
            if ($seen.ContainsKey($property.Name)) {
                throw "JSON object contains duplicate property '$($property.Name)'."
            }
            $seen[$property.Name] = $true
            Assert-JsonUniqueProperties $property.Value "$Context.$($property.Name)"
        }
    } elseif ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array) {
        foreach ($item in $Element.EnumerateArray()) {
            Assert-JsonUniqueProperties $item $Context
        }
    }
}

function Get-IdentityMigrationBaseline {
    param(
        [Parameter(Mandatory = $true)] [object] $Entry,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    $baselineKey = Get-IdentityStringField $Entry 'baselineKey' 'Identity migration'
    if (-not $baselineKey -or -not $BaselineByKey.ContainsKey($baselineKey)) {
        throw "Identity migration baselineKey '$baselineKey' is not present in the accepted complexity baseline."
    }
    return [pscustomobject]@{
        key    = $baselineKey
        metric = $BaselineByKey[$baselineKey]
    }
}

function Get-IdentityMigrationCurrent {
    param(
        [Parameter(Mandatory = $true)] [object] $Entry,
        [Parameter(Mandatory = $true)] [string] $BaselineKey
    )

    $current = Get-ExactJsonPropertyValue $Entry 'current'
    if ($null -eq $current -or $current -is [Array] -or $current -isnot [pscustomobject]) {
        throw "Identity migration for '$BaselineKey' must contain a current identity object."
    }
    $required = @('language', 'scopeKind', 'file', 'function', 'signature')
    Assert-ExactJsonShape $current $required "Identity migration current object for '$BaselineKey'"
    $language = Get-IdentityStringField $current 'language' "Identity migration current object for '$BaselineKey'"
    $scopeKind = Get-IdentityStringField $current 'scopeKind' "Identity migration current object for '$BaselineKey'"
    $file = Normalize-RepoPath (Get-IdentityStringField $current 'file' "Identity migration current object for '$BaselineKey'")
    $function = Get-IdentityStringField $current 'function' "Identity migration current object for '$BaselineKey'"
    $signature = Get-IdentityStringField $current 'signature' "Identity migration current object for '$BaselineKey'"
    if ($language -notin @('cpp', 'powershell') -or $scopeKind -ne 'function' -or
        -not $file -or -not $function -or -not $signature) {
        throw "Identity migration for '$BaselineKey' has an invalid current identity."
    }
    return [pscustomobject]@{
        language  = $language
        scopeKind = $scopeKind
        file      = $file
        function  = $function
        signature = $signature
    }
}

function New-IdentityMigrationEntry {
    param(
        [Parameter(Mandatory = $true)] [object] $Entry,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    Assert-ExactJsonShape $Entry @('baselineKey', 'current') 'Identity migration entry'
    $baselineInfo = Get-IdentityMigrationBaseline $Entry $BaselineByKey
    $current = Get-IdentityMigrationCurrent $Entry $baselineInfo.key
    if ($baselineInfo.metric.scopeKind -cne 'function' -or [string] $baselineInfo.metric.language -cne $current.language) {
        throw "Identity migration for '$($baselineInfo.key)' changes language or scope kind."
    }
    $currentKey = Get-FunctionKey $current.language $current.file $current.function $current.signature $current.scopeKind
    if ($currentKey -ceq $baselineInfo.key) {
        throw "Identity migration '$($baselineInfo.key)' must change the current identity."
    }
    if ($BaselineByKey.ContainsKey($currentKey)) {
        throw "Identity migration target '$currentKey' is already an accepted baseline identity."
    }
    return [pscustomobject]@{
        baselineKey = $baselineInfo.key
        currentKey  = $currentKey
        language    = $current.language
        scopeKind   = $current.scopeKind
        file        = $current.file
        function    = $current.function
        signature   = $current.signature
        baseline    = $baselineInfo.metric
        source      = 'reviewed'
    }
}

function New-IdentityMigrationMaps {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $Entries,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    $byCurrent = New-ExactMap
    $byBaseline = New-ExactMap
    $normalized = [System.Collections.Generic.List[object]]::new()
    foreach ($entry in $Entries) {
        $migration = New-IdentityMigrationEntry $entry $BaselineByKey
        if ($byBaseline.ContainsKey($migration.baselineKey)) {
            throw "Duplicate identity migration baselineKey '$($migration.baselineKey)'."
        }
        if ($byCurrent.ContainsKey($migration.currentKey)) {
            throw "Duplicate identity migration target '$($migration.currentKey)'."
        }
        $byBaseline[$migration.baselineKey] = $migration.baseline
        $byCurrent[$migration.currentKey] = $migration.baseline
        $normalized.Add($migration)
    }
    return [pscustomobject]@{
        entries       = @($normalized)
        byCurrentKey  = $byCurrent
        byBaselineKey = $byBaseline
    }
}

function Merge-IdentityMigrationMaps {
    param(
        [Parameter(Mandatory = $true)] [object] $Explicit,
        [Parameter(Mandatory = $true)] [object] $Automatic
    )

    $byCurrent = New-ExactMap
    $byBaseline = New-ExactMap
    foreach ($key in $Explicit.byCurrentKey.Keys) {
        $byCurrent[$key] = $Explicit.byCurrentKey[$key]
    }
    foreach ($key in $Explicit.byBaselineKey.Keys) {
        $byBaseline[$key] = $Explicit.byBaselineKey[$key]
    }
    foreach ($key in $Automatic.byCurrentKey.Keys) {
        if ($byCurrent.ContainsKey($key)) {
            throw "Duplicate identity continuity target '$key'."
        }
        $byCurrent[$key] = $Automatic.byCurrentKey[$key]
    }
    foreach ($key in $Automatic.byBaselineKey.Keys) {
        if ($byBaseline.ContainsKey($key)) {
            throw "Duplicate identity continuity baseline '$key'."
        }
        $byBaseline[$key] = $Automatic.byBaselineKey[$key]
    }
    return [pscustomobject]@{
        entries       = @($Explicit.entries + $Automatic.entries)
        byCurrentKey  = $byCurrent
        byBaselineKey = $byBaseline
    }
}

function Get-IdentityMigrationTargetMetric {
    param(
        [Parameter(Mandatory = $true)] [object] $Migration,
        [Parameter(Mandatory = $true)] [object[]] $CurrentMetrics
    )

    $target = @($CurrentMetrics | Where-Object { [string] $_.key -ceq [string] $Migration.currentKey })
    if (@($target).Count -ne 1) {
        throw "Identity migration target '$($Migration.currentKey)' does not exist exactly once in the measured candidate."
    }
    return $target[0]
}

function Assert-IdentityMigrationLineage {
    param(
        [Parameter(Mandatory = $true)] [object] $Migration,
        [Parameter(Mandatory = $true)] [object] $TargetMetric,
        [Parameter(Mandatory = $true)] [object] $TargetScope,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    $lineageMatches = @(Get-RelatedBaselineMetrics $TargetMetric $TargetScope $BaselineByKey)
    if (@($lineageMatches).Count -eq 0) {
        return
    }
    $mappedMatch = @($lineageMatches | Where-Object { [string] $_.key -ceq [string] $Migration.baselineKey })
    if (@($mappedMatch).Count -eq 0) {
        throw "Identity migration target '$($Migration.currentKey)' is already bound to a different accepted baseline through file lineage."
    }
}

function Assert-IdentityMigrationUniqueName {
    param(
        [Parameter(Mandatory = $true)] [object] $Migration,
        [Parameter(Mandatory = $true)] [object] $TargetMetric,
        [Parameter(Mandatory = $true)] [object] $TargetScope,
        [Parameter(Mandatory = $true)] [object[]] $CurrentMetrics,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    if ($TargetScope.IsRecreated) {
        return
    }
    $sameNameBaseline = @(Get-SameNameBaselineCandidates $TargetMetric $TargetScope $BaselineByKey)
    if (@($sameNameBaseline).Count -ne 1) {
        return
    }
    $sameNameCurrent = @(Get-SameNameCurrentCandidates $TargetMetric $CurrentMetrics)
    if (@($sameNameCurrent).Count -eq 1 -and
        [string] $sameNameBaseline[0].key -cne [string] $Migration.baselineKey) {
        throw "Identity migration target '$($Migration.currentKey)' is already bound to a different accepted baseline by unique function-name continuity."
    }
}

function Assert-IdentityMigrationFunctionIdentity {
    param(
        [Parameter(Mandatory = $true)] [object] $Migration,
        [Parameter(Mandatory = $true)] [object] $TargetMetric,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    $sameIdentityBaselines = @($BaselineByKey.Values | Where-Object {
            $_.scopeKind -ceq $TargetMetric.scopeKind -and $_.language -ceq $TargetMetric.language -and
            $_.function -ceq $TargetMetric.function -and $_.signature -ceq $TargetMetric.signature
        })
    if (@($sameIdentityBaselines).Count -eq 0) {
        return
    }
    $mappedMatch = @($sameIdentityBaselines | Where-Object { [string] $_.key -ceq [string] $Migration.baselineKey })
    if (@($mappedMatch).Count -eq 0) {
        throw "Identity migration target '$($Migration.currentKey)' conflicts with another accepted baseline sharing its exact function identity."
    }
}

function Assert-IdentityMigrationNotStale {
    param(
        [Parameter(Mandatory = $true)] [object] $Migration,
        [Parameter(Mandatory = $true)] [object[]] $CurrentMetrics
    )

    $old = @($CurrentMetrics | Where-Object { [string] $_.key -ceq [string] $Migration.baselineKey })
    if (@($old).Count -gt 0) {
        throw "Identity migration '$($Migration.baselineKey)' is stale because the old identity still exists in the candidate."
    }
}

function Assert-IdentityMigrationTargets {
    param(
        [Parameter(Mandatory = $true)] [object] $Migrations,
        [Parameter(Mandatory = $true)] [object[]] $CurrentMetrics,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    foreach ($migration in $Migrations.entries) {
        $targetMetric = Get-IdentityMigrationTargetMetric $migration $CurrentMetrics
        $targetScope = $ScopeByPath[[string] $targetMetric.file]
        if ($null -eq $targetScope) {
            throw "Identity migration target '$($migration.currentKey)' has no measured scope record."
        }
        Assert-IdentityMigrationLineage $migration $targetMetric $targetScope $BaselineByKey
        Assert-IdentityMigrationUniqueName $migration $targetMetric $targetScope $CurrentMetrics $BaselineByKey
        Assert-IdentityMigrationFunctionIdentity $migration $targetMetric $BaselineByKey
        Assert-IdentityMigrationNotStale $migration $CurrentMetrics
    }
}

function Get-SameNameBaselineCandidates {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey
    )

    $lineage = @([string] $Metric.file) + @($Scope.HistoricalPaths)
    return @($BaselineByKey.Values | Where-Object {
            $_.scopeKind -ceq 'function' -and $_.language -ceq $Metric.language -and
            $_.function -ceq $Metric.function -and (Test-ExactStringInList ([string] $_.file) $lineage)
        })
}

function Get-SameNameCurrentCandidates {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [object[]] $Functions
    )

    return @($Functions | Where-Object {
            $_.file -ceq $Metric.file -and $_.language -ceq $Metric.language -and
            $_.function -ceq $Metric.function
        })
}

function New-AutomaticIdentity {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [object[]] $Functions,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath,
        [Parameter(Mandatory = $true)] [object] $Explicit
    )

    if ($Explicit.byCurrentKey.ContainsKey([string] $Metric.key)) {
        return $null
    }
    $scope = $ScopeByPath[[string] $Metric.file]
    if ($scope.IsRecreated) {
        return $null
    }
    $exact = @(Get-RelatedBaselineMetrics $Metric $scope $BaselineByKey)
    if (@($exact).Count -gt 1) {
        throw "Ambiguous baseline identity for $($Metric.file):$($Metric.startLine) $($Metric.function)."
    }
    if (@($exact).Count -eq 1) {
        return $null
    }
    $sameNameBaseline = @(Get-SameNameBaselineCandidates $Metric $scope $BaselineByKey)
    if (@($sameNameBaseline).Count -eq 0) {
        return $null
    }
    $sameNameCurrent = @(Get-SameNameCurrentCandidates $Metric $Functions)
    if (@($sameNameBaseline).Count -ne 1 -or @($sameNameCurrent).Count -ne 1) {
        throw "Ambiguous function identity for $($Metric.file):$($Metric.startLine) $($Metric.function); add an exact reviewed migration."
    }
    $baseline = $sameNameBaseline[0]
    if ($Explicit.byBaselineKey.ContainsKey([string] $baseline.key)) {
        return $null
    }
    return [pscustomobject]@{
        baselineKey = [string] $baseline.key
        currentKey  = [string] $Metric.key
        baseline    = $baseline
        source      = 'automatic-unique-name'
    }
}

function New-AutomaticIdentityMaps {
    param(
        [Parameter(Mandatory = $true)] [object[]] $CurrentMetrics,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath,
        [Parameter(Mandatory = $true)] [object] $Explicit
    )

    $byCurrent = New-ExactMap
    $byBaseline = New-ExactMap
    $entries = [System.Collections.Generic.List[object]]::new()
    $functions = @($CurrentMetrics | Where-Object { $_.scopeKind -eq 'function' })
    foreach ($metric in $functions) {
        $migration = New-AutomaticIdentity $metric $functions $BaselineByKey $ScopeByPath $Explicit
        if ($null -eq $migration) {
            continue
        }
        if ($byCurrent.ContainsKey($migration.currentKey) -or $byBaseline.ContainsKey($migration.baselineKey)) {
            throw "Duplicate automatic identity continuity mapping for '$($migration.currentKey)'."
        }
        $byCurrent[$migration.currentKey] = $migration.baseline
        $byBaseline[$migration.baselineKey] = $migration.baseline
        $entries.Add($migration)
    }
    return [pscustomobject]@{
        entries       = @($entries)
        byCurrentKey  = $byCurrent
        byBaselineKey = $byBaseline
    }
}

function Get-EnforcedScopeMetrics {
    param([Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Metrics)

    return @($Metrics | Where-Object { $_.scopeKind -ceq 'function' -or $_.scopeKind -ceq 'script-body' })
}

function Get-EnforcedBaselineMetrics {
    param([Parameter(Mandatory = $true)] [hashtable] $BaselineByKey)

    return @($BaselineByKey.Values | Where-Object { $_.scopeKind -eq 'function' -or $_.scopeKind -eq 'script-body' })
}

function Test-BaselineMetricIdentity {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [object] $BaselineMetric
    )

    return $Metric.scopeKind -ceq $BaselineMetric.scopeKind -and
        $Metric.language -ceq $BaselineMetric.language -and
        $Metric.function -ceq $BaselineMetric.function -and
        $Metric.signature -ceq $BaselineMetric.signature
}

function Test-BaselineIdentityPresent {
    param(
        [Parameter(Mandatory = $true)] [object] $BaselineMetric,
        [Parameter(Mandatory = $true)] [object[]] $CurrentMetrics,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath
    )

    foreach ($metric in (Get-EnforcedScopeMetrics $CurrentMetrics)) {
        if (-not (Test-BaselineMetricIdentity $metric $BaselineMetric)) {
            continue
        }
        $scope = $ScopeByPath[[string] $metric.file]
        if ($scope.IsRecreated) {
            continue
        }
        $lineage = @([string] $metric.file) + @($scope.HistoricalPaths)
        if (Test-ExactStringInList ([string] $BaselineMetric.file) $lineage) {
            return $true
        }
    }
    return $false
}

function Get-IdentityDisappearanceMessage {
    param([Parameter(Mandatory = $true)] [object] $BaselineMetric)

    if ($BaselineMetric.scopeKind -ceq 'script-body') {
        return "Accepted script-body identity disappeared without a reviewed migration: $($BaselineMetric.key). A replacement/new script body is present; add an exact identity migration."
    }
    return "Accepted function identity disappeared without a reviewed migration: $($BaselineMetric.key). A replacement/new function is present; add an exact identity migration."
}

function Get-UnmigratedIdentityViolations {
    param(
        [Parameter(Mandatory = $true)] [object[]] $CurrentMetrics,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath,
        [Parameter(Mandatory = $true)] [object] $Continuity
    )

    $missing = [System.Collections.Generic.List[object]]::new()
    foreach ($baseline in (Get-EnforcedBaselineMetrics $BaselineByKey)) {
        if ($Continuity.byBaselineKey.ContainsKey([string] $baseline.key)) {
            continue
        }
        if (-not (Test-BaselineIdentityPresent $baseline $CurrentMetrics $ScopeByPath)) {
            $missing.Add($baseline)
        }
    }
    if ($missing.Count -eq 0) {
        return @()
    }
    $newFunctions = [System.Collections.Generic.List[object]]::new()
    foreach ($metric in (Get-EnforcedScopeMetrics $CurrentMetrics)) {
        $scope = $ScopeByPath[[string] $metric.file]
        $baseline = Find-BeforeMetric $metric $BaselineByKey $scope $Continuity.byCurrentKey
        if ($null -eq $baseline) {
            $newFunctions.Add($metric)
        }
    }
    if ($newFunctions.Count -eq 0) {
        return @()
    }
    $violations = [System.Collections.Generic.List[string]]::new()
    foreach ($baseline in $missing) {
        $violations.Add((Get-IdentityDisappearanceMessage $baseline))
    }
    return @($violations)
}

function New-MetricKeyMap {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $Metrics,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    $map = New-ExactMap
    foreach ($metric in $Metrics) {
        $key = [string] $metric.key
        if ($map.ContainsKey($key)) {
            throw "Duplicate $Label function identity '$key'."
        }
        $map[$key] = $metric
    }
    return $map
}

function Assert-UniqueFunctionIdentities {
    param([Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Metrics)

    $null = New-MetricKeyMap (Get-EnforcedScopeMetrics $Metrics) 'measured'
}

function Test-MetricInScope {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [hashtable] $IdentityMigrationsByCurrentKey
    )

    if ($Mode -eq 'Baseline') {
        return Test-LineIntersection $Scope.AcceptedOperatorLines $Metric.startLine $Metric.endLine
    }
    if ($IdentityMigrationsByCurrentKey.ContainsKey([string] $Metric.key)) {
        return $true
    }
    $matches = @(Get-RelatedBaselineMetrics $Metric $Scope $BaselineByKey)
    if (@($matches).Count -gt 1) {
        throw "Ambiguous baseline identity for $($Metric.file):$($Metric.startLine) $($Metric.function)."
    }
    return $Scope.IsNewAfterAccepted -or $Scope.IsRecreated -or @($matches).Count -eq 1 -or
        (Test-LineIntersection $Scope.CurrentOperatorLines $Metric.startLine $Metric.endLine) -or
        (Test-LineIntersection $Scope.CandidateLines $Metric.startLine $Metric.endLine)
}

function New-PowerShellFunctionMetric {
    param(
        [Parameter(Mandatory = $true)] [object] $Function,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string[]] $SourceLines,
        [Parameter(Mandatory = $true)] [object[]] $Functions,
        [Parameter(Mandatory = $true)] [hashtable] $Types,
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [hashtable] $IdentityMigrationsByCurrentKey,
        [switch] $IgnoreScope
    )

    $name = [string] $Function.Name
    $metric = [pscustomobject]@{
        key                   = Get-FunctionKey 'powershell' $Path $name $name 'function'
        language              = 'powershell'
        scopeKind             = 'function'
        file                  = $Path
        function              = $name
        signature             = $name
        startLine             = [int] $Function.Extent.StartLineNumber
        endLine               = [int] $Function.Extent.EndLineNumber
        nloc                  = Get-NonBlankLineCount $SourceLines $Function.Extent.StartLineNumber $Function.Extent.EndLineNumber
        cyclomaticComplexity  = 0
        parameterCount        = @($Function.Parameters).Count
        analyzer              = 'PowerShell Language.Parser AST'
        nlocMethod            = 'nonblank non-comment source lines'
        ccMethod             = 'base 1 plus AST if/elseif, loop, switch-clause, catch, trap, ternary, and logical-and/or nodes'
    }
    if (-not $IgnoreScope -and -not (Test-MetricInScope $metric $Scope $Mode $BaselineByKey $IdentityMigrationsByCurrentKey)) {
        return $null
    }
    $nestedRanges = @($Functions | Where-Object {
            $_.Extent.StartOffset -gt $Function.Body.Extent.StartOffset -and
            $_.Extent.EndOffset -le $Function.Body.Extent.EndOffset
        } | ForEach-Object {
            [pscustomobject]@{
                StartOffset = $_.Extent.StartOffset
                EndOffset   = $_.Extent.EndOffset
            }
        })
    $metric.cyclomaticComplexity = Get-AstCyclomaticComplexity $Function.Body $nestedRanges $Types
    return $metric
}

function New-PowerShellScriptBodyMetric {
    param(
        [Parameter(Mandatory = $true)] $Ast,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string[]] $SourceLines,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $FunctionRanges,
        [Parameter(Mandatory = $true)] [AllowNull()] [object] $OperatorLines,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [hashtable] $Types
    )

    if ($null -eq $OperatorLines) {
        $OperatorLines = [System.Collections.Generic.HashSet[int]]::new()
    }
    $functionRangeArray = ConvertTo-FunctionRangeArray -FunctionRanges $FunctionRanges
    $outside = [System.Collections.Generic.HashSet[int]]::new()
    foreach ($lineNumber in $OperatorLines) {
        $inside = @($functionRangeArray | Where-Object {
                $lineNumber -ge $_.StartLine -and $lineNumber -le $_.EndLine
            }).Count -gt 0
        if (-not $inside) {
            $null = $outside.Add($lineNumber)
        }
    }
    if ($Mode -eq 'Baseline' -and $outside.Count -eq 0) {
        return $null
    }
    return [pscustomobject]@{
        key                   = Get-FunctionKey 'powershell' $Path '<script-body>' '<script-body>' 'script-body'
        language              = 'powershell'
        scopeKind             = 'script-body'
        file                  = $Path
        function              = '<script-body>'
        signature             = '<script-body>'
        startLine             = 1
        endLine               = $SourceLines.Count
        nloc                  = Get-NonBlankLineCount $SourceLines 1 $SourceLines.Count $functionRangeArray
        cyclomaticComplexity  = Get-AstCyclomaticComplexity $Ast $functionRangeArray $Types
        parameterCount        = 0
        analyzer              = 'PowerShell Language.Parser AST'
        nlocMethod            = 'nonblank non-comment source lines outside function definitions'
        ccMethod             = 'base 1 plus AST if/elseif, loop, switch-clause, catch, trap, ternary, and logical-and/or nodes outside functions'
    }
}

function Get-PowerShellFileMetrics {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [hashtable] $Types,
        [Parameter(Mandatory = $true)] [hashtable] $IdentityMigrationsByCurrentKey,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [System.Collections.Generic.List[object]] $AllMetrics
    )

    $fullPath = Join-Path $repoRoot $Path
    $sourceLines = @(Get-Content -LiteralPath $fullPath)
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($fullPath, [ref] $tokens, [ref] $errors)
    if (@($errors).Count -gt 0) {
        $messages = @($errors | ForEach-Object { $_.Message })
        throw "PowerShell parser failed for '$Path'; refusing to measure an unverifiable executable file: $($messages -join ' | ')"
    }
    $functions = @($ast.FindAll({
                param($candidate)
                Test-AstType $candidate $Types 'FunctionDefinitionAst'
            }, $true))
    $ranges = Get-FunctionRanges $functions
    $metrics = [System.Collections.Generic.List[object]]::new()
    foreach ($function in $functions) {
        $metric = New-PowerShellFunctionMetric $function $Path $sourceLines $functions $Types $Scope $Mode $BaselineByKey $IdentityMigrationsByCurrentKey -IgnoreScope
        $AllMetrics.Add($metric)
        if (Test-MetricInScope $metric $Scope $Mode $BaselineByKey $IdentityMigrationsByCurrentKey) {
            $metrics.Add($metric)
        }
    }
    $operatorLines = if ($Mode -eq 'Baseline') { $Scope.AcceptedOperatorLines } else { $Scope.CurrentOperatorLines }
    $body = New-PowerShellScriptBodyMetric -Ast $ast -Path $Path -SourceLines $sourceLines -FunctionRanges $ranges -OperatorLines $operatorLines -Mode $Mode -Types $Types
    if ($null -ne $body) {
        $AllMetrics.Add($body)
        $metrics.Add($body)
    }
    return @($metrics)
}

function Get-PowerShellMetrics {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [string[]] $Files,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [hashtable] $IdentityMigrationsByCurrentKey,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [System.Collections.Generic.List[object]] $AllMetrics
    )

    $types = Get-AstTypeMap
    if (-not $types.ContainsKey('FunctionDefinitionAst')) {
        throw 'PowerShell FunctionDefinitionAst is unavailable; cannot measure PowerShell scripts.'
    }
    $metrics = [System.Collections.Generic.List[object]]::new()
    foreach ($path in $Files) {
        foreach ($metric in (Get-PowerShellFileMetrics $path $ScopeByPath[$path] $Mode $BaselineByKey $types $IdentityMigrationsByCurrentKey $AllMetrics)) {
            $metrics.Add($metric)
        }
    }
    return @($metrics)
}

function Get-ScopedCppMetrics {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [string[]] $Files,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [hashtable] $BaselineByKey,
        [Parameter(Mandatory = $true)] [string] $Python,
        [Parameter(Mandatory = $true)] [hashtable] $IdentityMigrationsByCurrentKey,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [System.Collections.Generic.List[object]] $AllMetrics
    )

    $version = Get-LizardVersion $Python
    $rows = @(Get-LizardRows $Files $Python)
    $metrics = [System.Collections.Generic.List[object]]::new()
    foreach ($row in $rows) {
        $file = Normalize-RepoPath ([string] $row.File)
        if (-not $ScopeByPath.ContainsKey($file)) {
            continue
        }
        $metric = [pscustomobject]@{
            key                  = Get-FunctionKey 'cpp' $file ([string] $row.Function) ([string] $row.Signature) 'function'
            language             = 'cpp'
            scopeKind            = 'function'
            file                 = $file
            function             = [string] $row.Function
            signature            = [string] $row.Signature
            startLine            = [int] $row.StartLine
            endLine              = [int] $row.EndLine
            nloc                 = [int] $row.NLOC
            cyclomaticComplexity = [int] $row.CCN
            parameterCount       = [int] $row.Parameters
            analyzer             = "lizard $version"
            nlocMethod           = 'lizard NLOC'
            ccMethod             = 'lizard default CCN (switch cases counted individually)'
        }
        $AllMetrics.Add($metric)
        if (-not (Test-MetricInScope $metric $ScopeByPath[$file] $Mode $BaselineByKey $IdentityMigrationsByCurrentKey)) {
            continue
        }
        $metrics.Add($metric)
    }
    return @($metrics)
}

function Get-StatisticsMetrics {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Metrics,
        [Parameter(Mandatory = $true)] [ValidateSet('function', 'script-body', 'enforced')] [string] $ScopeKind
    )

    if ($ScopeKind -eq 'enforced') {
        return Get-EnforcedScopeMetrics $Metrics
    }
    return @($Metrics | Where-Object { $_.scopeKind -ceq $ScopeKind })
}

function Get-Statistics {
    param(
        [Parameter(Mandatory = $true)] [object[]] $Metrics,
        [ValidateSet('function', 'script-body', 'enforced')] [string] $ScopeKind = 'function'
    )

    $values = @(Get-StatisticsMetrics $Metrics $ScopeKind |
        ForEach-Object { [int] $_.cyclomaticComplexity } | Sort-Object)
    if (@($values).Count -eq 0) {
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
    $middle = [int] [Math]::Floor(@($values).Count / 2)
    $median = if (@($values).Count % 2 -eq 0) {
        ($values[$middle - 1] + $values[$middle]) / 2
    } else {
        $values[$middle]
    }
    $p90Rank = [int] [Math]::Ceiling(@($values).Count * 0.9)
    $p90 = $values[[Math]::Max(0, $p90Rank - 1)]
    return [ordered]@{
        scopedFunctionCount = @($values).Count
        averageCC           = [Math]::Round((($values | Measure-Object -Average).Average), 3)
        medianCC            = [Math]::Round($median, 3)
        p90CC               = $p90
        maximumCC           = $values[-1]
        countCCGreater5     = @($values | Where-Object { $_ -gt 5 }).Count
        countCCGreater7     = @($values | Where-Object { $_ -gt 7 }).Count
        countCCGreater10    = @($values | Where-Object { $_ -gt 10 }).Count
    }
}

function Get-SortedScopeMetrics {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Metrics,
        [Parameter(Mandatory = $true)] [ValidateSet('function', 'script-body', 'enforced')] [string] $ScopeKind
    )

    return @(Get-StatisticsMetrics $Metrics $ScopeKind |
        Sort-Object @{ Expression = { [int] $_.cyclomaticComplexity }; Descending = $true },
        @{ Expression = { [int] $_.nloc }; Descending = $true },
        @{ Expression = { [string] $_.file }; Descending = $false },
        @{ Expression = { [int] $_.startLine }; Descending = $false })
}

function Get-SortedFunctions {
    param([Parameter(Mandatory = $true)] [object[]] $Metrics)

    return Get-SortedScopeMetrics $Metrics 'function'
}

function Get-SortedScriptBodies {
    param([Parameter(Mandatory = $true)] [object[]] $Metrics)

    return Get-SortedScopeMetrics $Metrics 'script-body'
}

function Get-ReportStatistics {
    param(
        [Parameter(Mandatory = $true)] [object] $Report,
        [Parameter(Mandatory = $true)] [ValidateSet('function', 'script-body', 'enforced')] [string] $ScopeKind
    )

    $scopeSummary = $Report.PSObject.Properties['scopeSummary']
    $propertyName = switch ($ScopeKind) {
        'function' { 'namedFunctions' }
        'script-body' { 'scriptBodies' }
        'enforced' { 'enforcedScopes' }
    }
    if ($null -ne $scopeSummary -and $null -ne $scopeSummary.Value.PSObject.Properties[$propertyName]) {
        return $scopeSummary.Value.PSObject.Properties[$propertyName].Value
    }
    return Get-Statistics @($Report.functions) $ScopeKind
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

function Format-ScopeStatTable {
    param(
        [Parameter(Mandatory = $true)] [object] $Statistics,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    return @(
        '| Measure | Value |',
        '|---|---:|',
        "| $Label | $($Statistics.scopedFunctionCount) |",
        "| Average $Label CC | $($Statistics.averageCC) |",
        "| Median $Label CC | $($Statistics.medianCC) |",
        "| 90th percentile $Label CC (nearest rank) | $($Statistics.p90CC) |",
        "| Maximum $Label CC | $($Statistics.maximumCC) |",
        "| $Label with CC > 5 | $($Statistics.countCCGreater5) |",
        "| $Label with CC > 7 | $($Statistics.countCCGreater7) |",
        "| $Label with CC > 10 | $($Statistics.countCCGreater10) |"
    ) -join "`n"
}

function Format-FunctionList {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $Functions,
        [Parameter(Mandatory = $true)] [int] $Threshold
    )

    $selected = @($Functions | Where-Object { [int] $_.cyclomaticComplexity -gt $Threshold })
    if (@($selected).Count -eq 0) {
        return '_None._'
    }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($function in $selected) {
        $null = $lines.Add("- ``$($function.function)`` — ``$($function.file):$($function.startLine)-$($function.endLine)``, CC $($function.cyclomaticComplexity), NLOC $($function.nloc), params $($function.parameterCount)")
    }
    return $lines -join "`n"
}

function Add-ScopeMarkdown {
    param(
        [Parameter(Mandatory = $true)] [object] $Lines,
        [Parameter(Mandatory = $true)] [object] $Report,
        [Parameter(Mandatory = $true)] [ValidateSet('script-body', 'enforced')] [string] $ScopeKind,
        [Parameter(Mandatory = $true)] [string] $Heading,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    $metrics = @(Get-SortedScopeMetrics $Report.functions $ScopeKind)
    $top = @($metrics | Select-Object -First 25)
    $null = $Lines.Add("## $Heading")
    $null = $Lines.Add('')
    $null = $Lines.Add((Format-ScopeStatTable (Get-ReportStatistics $Report $ScopeKind) $Label))
    $null = $Lines.Add('')
    $null = $Lines.Add("### $Label with CC > 5")
    $null = $Lines.Add('')
    $null = $Lines.Add((Format-FunctionList $metrics 5))
    $null = $Lines.Add('')
    $null = $Lines.Add("### $Label with CC > 7")
    $null = $Lines.Add('')
    $null = $Lines.Add((Format-FunctionList $metrics 7))
    $null = $Lines.Add('')
    $null = $Lines.Add("### $Label with CC > 10")
    $null = $Lines.Add('')
    $null = $Lines.Add((Format-FunctionList $metrics 10))
    $null = $Lines.Add('')
    $null = $Lines.Add("### Top 25 $($Label.ToLowerInvariant())")
    $null = $Lines.Add('')
    $null = $Lines.Add('| Rank | File | Lines | NLOC | CC |')
    $null = $Lines.Add('|---:|---|---:|---:|---:|')
    $rank = 1
    foreach ($metric in $top) {
        $null = $Lines.Add("| $rank | ``$($metric.file)`` | $($metric.startLine)-$($metric.endLine) | $($metric.nloc) | $($metric.cyclomaticComplexity) |")
        $rank++
    }
    $null = $Lines.Add('')
}

function New-BaselineMarkdown {
    param(
        [Parameter(Mandatory = $true)] [object] $Report,
        [Parameter(Mandatory = $true)] [object] $Inventory
    )

    $functions = @(Get-SortedFunctions $Report.functions)
    $top = @($functions | Select-Object -First 25)
    $lines = [System.Collections.Generic.List[string]]::new()
    $null = $lines.Add('# Phase-1 Cyclomatic Complexity Baseline')
    $null = $lines.Add('')
    $null = $lines.Add("Accepted production checkpoint: ``$($Report.scope.acceptedRef)``")
    $null = $lines.Add("Ownership base: ``$($Report.scope.baseRef)`` (merge-base of ``origin/master`` and accepted HEAD)")
    $null = $lines.Add('')
    $null = $lines.Add('## Ownership and measurement scope')
    $null = $lines.Add('')
    $null = $lines.Add("Operator-authored commits in lineage: **$($Inventory.operatorAuthoredCommitCountInLineage)**")
    $null = $lines.Add("Operator-authored commits touching scoped executable code: **$($Inventory.operatorAuthoredCodeCommitCountInScope)**")
    $null = $lines.Add("Current executable files in scope: **$($Inventory.cyclomaticTargetFiles.Count)**")
    $null = $lines.Add('')
    $null = $lines.Add('The complete authorship and file inventory is in `complexity-ownership-inventory.json` and `.md`. Scope was derived from Git history and current `git blame`; it was not inferred from filenames alone.')
    $null = $lines.Add('')
    $null = $lines.Add('## Analyzer')
    $null = $lines.Add('')
    $null = $lines.Add("- C/C++: $($Report.analyzer.cpp)")
    $null = $lines.Add("- PowerShell: $($Report.analyzer.powershell)")
    $null = $lines.Add('- CMake/YAML/JSON/Markdown/license files are recorded for review but are not included in function-level CC statistics.')
    $null = $lines.Add('')
    $null = $lines.Add('## Baseline summary')
    $null = $lines.Add('')
    $null = $lines.Add((Format-StatTable $Report.summary))
    $null = $lines.Add('')
    $null = $lines.Add('The p90 uses nearest-rank: `ceil(0.90 * N)`. The named-function summary excludes PowerShell top-level script bodies; script bodies are measured and enforced separately below. The historical Before snapshot contains only bodies present in that accepted historical scope, while After includes the complete current project-owned script-body scope.')
    $null = $lines.Add('')
    $null = $lines.Add('## Functions with CC > 5')
    $null = $lines.Add('')
    $null = $lines.Add((Format-FunctionList $functions 5))
    $null = $lines.Add('')
    $null = $lines.Add('## Functions with CC > 7')
    $null = $lines.Add('')
    $null = $lines.Add((Format-FunctionList $functions 7))
    $null = $lines.Add('')
    $null = $lines.Add('## Functions with CC > 10')
    $null = $lines.Add('')
    $null = $lines.Add((Format-FunctionList $functions 10))
    $null = $lines.Add('')
    $null = $lines.Add('## Top 25 scoped functions')
    $null = $lines.Add('')
    $null = $lines.Add('| Rank | Function | File | Lines | NLOC | CC | Params |')
    $null = $lines.Add('|---:|---|---|---:|---:|---:|---:|')
    $rank = 1
    foreach ($function in $top) {
        $null = $lines.Add("| $rank | ``$($function.function)`` | ``$($function.file)`` | $($function.startLine)-$($function.endLine) | $($function.nloc) | $($function.cyclomaticComplexity) | $($function.parameterCount) |")
        $rank++
    }
    $null = $lines.Add('')
    Add-ScopeMarkdown -Lines $lines -Report $Report -ScopeKind 'script-body' -Heading 'PowerShell script-body complexity' -Label 'Script bodies'
    $null = $lines.Add('## Combined enforced executable-scope summary')
    $null = $lines.Add('')
    $null = $lines.Add((Format-ScopeStatTable (Get-ReportStatistics $Report 'enforced') 'Enforced scopes'))
    $null = $lines.Add('')
    if (@($Report.limitations).Count -gt 0) {
        $null = $lines.Add('## Measurement limitations')
        $null = $lines.Add('')
        foreach ($limitation in $Report.limitations) {
            $null = $lines.Add("- ``$($limitation.file)``: $($limitation.reason)")
        }
        $null = $lines.Add('')
    }
    return $lines -join "`n"
}

function New-InventoryMarkdown {
    param([Parameter(Mandatory = $true)] [object] $Inventory)

    $lines = [System.Collections.Generic.List[string]]::new()
    $null = $lines.Add('# Phase-1 Complexity Ownership Inventory')
    $null = $lines.Add('')
    $null = $lines.Add("Accepted HEAD: ``$($Inventory.acceptedRef)``")
    $null = $lines.Add("Ownership base: ``$($Inventory.baseRef)``")
    $null = $lines.Add('')
    $null = $lines.Add('## Discovery')
    $null = $lines.Add('')
    $null = $lines.Add('Ownership was derived from `git log BASE..accepted` author metadata, then file/function attribution was checked with current accepted-HEAD `git blame`. Archived WIP branches were not traversed.')
    $null = $lines.Add('')
    $null = $lines.Add("- GitHub account: **$($Inventory.githubAccount)**")
    $null = $lines.Add("- Author-authored commits in accepted lineage: **$($Inventory.operatorAuthoredCommitCountInLineage)**")
    $null = $lines.Add("- Author-authored commits touching current executable scope: **$($Inventory.operatorAuthoredCodeCommitCountInScope)**")
    $null = $lines.Add('')
    $null = $lines.Add('| Author name | Author email |')
    $null = $lines.Add('|---|---|')
    foreach ($alias in $Inventory.operatorIdentityAliases) {
        $null = $lines.Add("| $($alias.name) | ``$($alias.email)`` |")
    }
    $null = $lines.Add('')
    $null = $lines.Add('## Current executable files')
    $null = $lines.Add('')
    $null = $lines.Add('| File | Language | Status | Operator commits | Blame lines | Scoped functions |')
    $null = $lines.Add('|---|---|---|---:|---:|---:|')
    foreach ($file in $Inventory.cyclomaticTargetFiles) {
        $null = $lines.Add("| ``$($file.path)`` | $($file.language) | $($file.status) | $($file.operatorCommitCount) | $($file.operatorBlameLineCount) | $($file.scopedFunctionCount) |")
    }
    $null = $lines.Add('')
    $null = $lines.Add('## Non-CC changed paths')
    $null = $lines.Add('')
    $null = $lines.Add('These paths remain part of the authorship audit but are not function-level cyclomatic targets:')
    $null = $lines.Add('')
    foreach ($file in $Inventory.nonCyclomaticChangedFiles) {
        $null = $lines.Add("- ``$($file.path)`` — $($file.category)")
    }
    $null = $lines.Add('')
    $null = $lines.Add('## Scope rules')
    $null = $lines.Add('')
    foreach ($rule in $Inventory.scopeRules) {
        $null = $lines.Add("- $rule")
    }
    $null = $lines.Add('')
    return $lines -join "`n"
}

function Find-BeforeMetric {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [hashtable] $Exact,
        [Parameter(Mandatory = $true)] [object] $Scope,
        [hashtable] $IdentityMigrationsByCurrentKey = $(New-ExactMap)
    )

    $matches = @(Get-RelatedBaselineMetrics $Metric $Scope $Exact)
    $mapped = if ($IdentityMigrationsByCurrentKey.ContainsKey([string] $Metric.key)) {
        $IdentityMigrationsByCurrentKey[[string] $Metric.key]
    } else {
        $null
    }
    if ($null -ne $mapped) {
        if (@($matches).Count -gt 0 -and
            @($matches | Where-Object { [string] $_.key -ceq [string] $mapped.key }).Count -eq 0) {
            throw "Identity migration for '$($Metric.key)' conflicts with an existing exact or lineage baseline identity."
        }
        return $mapped
    }
    if (@($matches).Count -gt 1) {
        throw "Ambiguous baseline identity for $($Metric.file):$($Metric.startLine) $($Metric.function)."
    }
    if (@($matches).Count -eq 1) {
        return $matches[0]
    }
    return $null
}

function Get-ComparisonLabel {
    param([Parameter(Mandatory = $true)] [string] $Name)

    $labels = @{
        scopedFunctionCount = 'Scoped functions'
        averageCC           = 'Average CC'
        medianCC            = 'Median CC'
        p90CC               = '90th percentile CC'
        maximumCC           = 'Maximum CC'
        countCCGreater5     = 'Functions with CC > 5'
        countCCGreater7     = 'Functions with CC > 7'
        countCCGreater10    = 'Functions with CC > 10'
    }
    return $labels[$Name]
}

function Get-ComparisonMetricLine {
    param(
        [Parameter(Mandatory = $true)] [object] $AfterMetric,
        [object] $BeforeMetric
    )

    if ($BeforeMetric) {
        $delta = [int] $AfterMetric.cyclomaticComplexity - [int] $BeforeMetric.cyclomaticComplexity
        $note = if ($delta -lt 0) { "reduced by $(-$delta)" } elseif ($delta -gt 0) { "increased by $delta" } else { 'unchanged' }
        $beforeCC = $BeforeMetric.cyclomaticComplexity
        $beforeNloc = $BeforeMetric.nloc
    } else {
        $note = 'new cohesive helper/function in scoped file'
        $beforeCC = '—'
        $beforeNloc = '—'
    }
    return "| ``$($AfterMetric.function)`` | ``$($AfterMetric.file)`` | $beforeCC | $($AfterMetric.cyclomaticComplexity) | $beforeNloc | $($AfterMetric.nloc) | $note |"
}

function Add-ComparisonSummary {
    param(
        [Parameter(Mandatory = $true)] [object] $Lines,
        [Parameter(Mandatory = $true)] [object] $Before,
        [Parameter(Mandatory = $true)] [object] $After
    )

    foreach ($name in @('scopedFunctionCount', 'averageCC', 'medianCC', 'p90CC', 'maximumCC', 'countCCGreater5', 'countCCGreater7', 'countCCGreater10')) {
        $label = Get-ComparisonLabel $name
        $null = $Lines.Add("| $label | $($Before.summary.$name) | $($After.summary.$name) |")
    }
}

function Add-ComparisonExceptions {
    param(
        [Parameter(Mandatory = $true)] [object] $Lines,
        [object[]] $Exceptions
    )

    if (@($Exceptions).Count -eq 0) {
        $null = $Lines.Add('_Empty._')
        return
    }
    $null = $Lines.Add('| File | Function | Measured CC | Reason | Date/task | Reviewer note |')
    $null = $Lines.Add('|---|---|---:|---|---|---|')
    foreach ($exception in $Exceptions) {
        $null = $Lines.Add("| ``$($exception.file)`` | ``$($exception.function)`` | $($exception.measuredCC) | $($exception.reason) | $($exception.dateTask) | $($exception.reviewerNote) |")
    }
}

function Add-ComparisonLimitations {
    param(
        [Parameter(Mandatory = $true)] [object] $Lines,
        [Parameter(Mandatory = $true)] [object] $After
    )

    if (@($After.limitations).Count -eq 0) {
        return
    }
    $null = $Lines.Add('## Measurement limitations')
    $null = $Lines.Add('')
    foreach ($limitation in $After.limitations) {
        $null = $Lines.Add("- ``$($limitation.file)``: $($limitation.reason)")
    }
    $null = $Lines.Add('')
}

function Add-ComparisonScopeSummary {
    param(
        [Parameter(Mandatory = $true)] [object] $Lines,
        [Parameter(Mandatory = $true)] [object] $Before,
        [Parameter(Mandatory = $true)] [object] $After,
        [Parameter(Mandatory = $true)] [ValidateSet('script-body', 'enforced')] [string] $ScopeKind,
        [Parameter(Mandatory = $true)] [string] $Label
    )

    $beforeStats = Get-ReportStatistics $Before $ScopeKind
    $afterStats = Get-ReportStatistics $After $ScopeKind
    $labels = [ordered]@{
        scopedFunctionCount = $Label
        averageCC = "Average $Label CC"
        medianCC = "Median $Label CC"
        p90CC = "90th percentile $Label CC"
        maximumCC = "Maximum $Label CC"
        countCCGreater5 = "$Label with CC > 5"
        countCCGreater7 = "$Label with CC > 7"
        countCCGreater10 = "$Label with CC > 10"
    }
    $null = $Lines.Add("## $Label before/after summary")
    $null = $Lines.Add('')
    $null = $Lines.Add('| Measure | Before | After |')
    $null = $Lines.Add('|---|---:|---:|')
    foreach ($name in $labels.Keys) {
        $null = $Lines.Add("| $($labels[$name]) | $($beforeStats.$name) | $($afterStats.$name) |")
    }
    $null = $Lines.Add('')
}

function Add-ComparisonMetricSection {
    param(
        [Parameter(Mandatory = $true)] [object] $Lines,
        [Parameter(Mandatory = $true)] [string] $Heading,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [AllowNull()] [object[]] $Metrics,
        [Parameter(Mandatory = $true)] [hashtable] $BeforeExact,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath
    )

    $null = $Lines.Add("## $Heading")
    $null = $Lines.Add('')
    $null = $Lines.Add('| Function | File | Before CC | After CC | Before NLOC | After NLOC | Notes |')
    $null = $Lines.Add('|---|---|---:|---:|---:|---:|---|')
    foreach ($afterMetric in $Metrics) {
        $beforeMetric = Find-BeforeMetric $afterMetric $BeforeExact $ScopeByPath[$afterMetric.file]
        $null = $Lines.Add((Get-ComparisonMetricLine $afterMetric $beforeMetric))
    }
    $null = $Lines.Add('')
}

function New-ComparisonMarkdown {
    param(
        [Parameter(Mandatory = $true)] [object] $Before,
        [Parameter(Mandatory = $true)] [object] $After,
        [Parameter(Mandatory = $true)] [object] $Inventory,
        [Parameter(Mandatory = $true)] [hashtable] $ScopeByPath,
        [object[]] $Allowlist
    )

    $exceptions = @($Allowlist | Where-Object { $null -ne $_ })
    $beforeExact = New-MetricKeyMap @($Before.functions) 'comparison baseline'
    $afterFunctions = @(Get-SortedFunctions $After.functions)
    $lines = [System.Collections.Generic.List[string]]::new()
    $null = $lines.Add('# Phase-1 Complexity Hardening Report')
    $null = $lines.Add('')
    $null = $lines.Add("Accepted starting SHA: ``$($Before.scope.acceptedRef)``")
    $null = $lines.Add("Candidate measurement HEAD: ``$($After.scope.measurementHead)``")
    $null = $lines.Add('')
    $null = $lines.Add('## Before/after summary')
    $null = $lines.Add('')
    $null = $lines.Add('| Measure | Before | After |')
    $null = $lines.Add('|---|---:|---:|')
    Add-ComparisonSummary -Lines $lines -Before $Before -After $After
    $null = $lines.Add('')
    $null = $lines.Add('The p90 is nearest-rank `ceil(0.90 * N)`. The named-function summary excludes PowerShell top-level script bodies; script bodies are measured and enforced separately. The historical Before snapshot contains only bodies present in that accepted historical scope, while After includes the complete current project-owned script-body scope.')
    $null = $lines.Add('')
    Add-ComparisonScopeSummary -Lines $lines -Before $Before -After $After -ScopeKind 'script-body' -Label 'Script bodies'
    Add-ComparisonScopeSummary -Lines $lines -Before $Before -After $After -ScopeKind 'enforced' -Label 'Enforced scopes'
    Add-ComparisonMetricSection -Lines $lines -Heading 'Named function-by-function comparison' -Metrics $afterFunctions -BeforeExact $beforeExact -ScopeByPath $ScopeByPath
    Add-ComparisonMetricSection -Lines $lines -Heading 'PowerShell script-body comparison' -Metrics (Get-SortedScriptBodies $After.functions) -BeforeExact $beforeExact -ScopeByPath $ScopeByPath
    $null = $lines.Add('## Top remaining functions')
    $null = $lines.Add('')
    $null = $lines.Add((Format-FunctionList $afterFunctions 5))
    $null = $lines.Add('')
    $null = $lines.Add('## Remaining functions with CC > 7')
    $null = $lines.Add('')
    $null = $lines.Add((Format-FunctionList $afterFunctions 7))
    $null = $lines.Add('')
    $null = $lines.Add('## Remaining functions with CC > 10')
    $null = $lines.Add('')
    $null = $lines.Add((Format-FunctionList $afterFunctions 10))
    $null = $lines.Add('')
    $null = $lines.Add('## Intentional exceptions')
    $null = $lines.Add('')
    Add-ComparisonExceptions -Lines $lines -Exceptions $exceptions
    $null = $lines.Add('')
    Add-ComparisonLimitations -Lines $lines -After $After
    $null = $lines.Add('See `complexity-ownership-inventory.md` for the complete Git-derived attribution scope.')
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
            currentScopeRule = 'Historical operator-attributed functions plus current C/C++ and PowerShell files introduced by operator work after acceptedRef or descended through later file renames; changed functions in later operator-modified files are selected by exact baseline identity, current blame, or candidate diff lines. Unsupported executable languages fail closed.'
        }
        analyzer      = [ordered]@{
            cpp        = "lizard $LizardVersion (C/C++ parser, CSV output, default CCN)"
            powershell = "PowerShell $($PSVersionTable.PSVersion) System.Management.Automation.Language.Parser AST"
            nloc       = 'lizard NLOC for C/C++; nonblank non-comment line count for PowerShell'
            p90        = 'nearest-rank ceil(0.90 * N)'
        }
        summary       = Get-Statistics $Metrics 'function'
        scopeSummary  = [ordered]@{
            namedFunctions = Get-Statistics $Metrics 'function'
            scriptBodies   = Get-Statistics $Metrics 'script-body'
            enforcedScopes = Get-Statistics $Metrics 'enforced'
        }
        functions    = @($Metrics | Sort-Object @{ Expression = { [string] $_.file } }, @{ Expression = { [int] $_.startLine } }, @{ Expression = { [string] $_.function } })
        limitations  = @($limitations)
        lineage      = [ordered]@{ operatorAuthoredCommitCount = $LineageCommitCount }
    }
}

function Read-OptionalReport {
    param([Parameter(Mandatory = $true)] [string] $Path)

    if (-not (Test-Path -LiteralPath (Get-RepoFilePath $Path))) {
        return $null
    }
    try {
        return Read-JsonFile $Path
    } catch {
        return $null
    }
}

function Read-CheckBaselineReport {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $BaseRef,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef
    )

    if ($Mode -ne 'Check' -or -not (Test-Path -LiteralPath (Get-RepoFilePath $Path))) {
        return $null
    }
    return Read-TrustedAcceptedBaseline $Path $BaseRef $AcceptedRef
}

function Read-IdentityEntriesForMode {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [string] $Path
    )

    if ($Mode -eq 'Check' -and (Test-Path -LiteralPath (Get-RepoFilePath $Path))) {
        return Read-IdentityMigrationEntries $Path
    }
    return @()
}

function New-InclusionBaselineMap {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [object] $AcceptedReport,
        [object] $BeforeReport
    )

    if ($Mode -eq 'Check' -and $null -ne $AcceptedReport) {
        $map = New-MetricKeyMap @($AcceptedReport.functions) 'accepted complexity baseline'
        if ($null -ne $BeforeReport) {
            foreach ($metric in $BeforeReport.functions) {
                if (-not $map.ContainsKey([string] $metric.key)) {
                    $map[[string] $metric.key] = $metric
                }
            }
        }
        return $map
    }
    if ($null -ne $BeforeReport) {
        return New-MetricKeyMap @($BeforeReport.functions) 'scope baseline'
    }
    return New-ExactMap
}

function New-BaselineContext {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [string] $BeforePath,
        [Parameter(Mandatory = $true)] [string] $BaselinePath,
        [Parameter(Mandatory = $true)] [string] $IdentityMigrationsPath,
        [Parameter(Mandatory = $true)] [string] $BaseRef,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef
    )

    $beforeReport = Read-OptionalReport $BeforePath
    $acceptedReport = Read-CheckBaselineReport $Mode $BaselinePath $BaseRef $AcceptedRef
    $acceptedByKey = if ($null -ne $acceptedReport) {
        New-MetricKeyMap @($acceptedReport.functions) 'accepted complexity baseline'
    } else {
        New-ExactMap
    }
    $identityEntries = Read-IdentityEntriesForMode $Mode $IdentityMigrationsPath
    $explicitMaps = if ($Mode -eq 'Check') {
        New-IdentityMigrationMaps $identityEntries $acceptedByKey
    } else {
        New-EmptyIdentityMaps
    }
    return [pscustomobject]@{
        BeforeReport       = $beforeReport
        AcceptedReport     = $acceptedReport
        AcceptedByKey      = $acceptedByKey
        ExplicitIdentity   = $explicitMaps
        InclusionByKey     = New-InclusionBaselineMap $Mode $acceptedReport $beforeReport
    }
}

function New-HistoryContext {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $BaseRef,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef
    )

    $acceptedResolved = Resolve-Commit $AcceptedRef
    $baseInput = $BaseRef
    if (-not $baseInput) {
        $baseInput = (@(Invoke-GitLines @('merge-base', 'origin/master', $acceptedResolved)) | Select-Object -First 1).Trim()
    }
    $baseResolved = Resolve-Commit $baseInput
    $range = "$baseResolved..$acceptedResolved"
    $measurementHead = (@(Invoke-GitLines @('rev-parse', 'HEAD')) | Select-Object -First 1).Trim()
    $history = @(Get-CommitRecords $range)
    $futureHistory = @(Get-CommitRecords "$acceptedResolved..$measurementHead")
    $identityHistory = @($history + $futureHistory)
    $discoveredAliases = @($identityHistory | Where-Object {
            ([string] $_.Email) -match '(?i)ymgpwcca' -or [string] $_.Name -ceq $knownOperatorName
        } | Select-Object Name, Email -Unique)
    $operatorEmails = @($knownOperatorEmails + @($discoveredAliases | ForEach-Object { $_.Email })) |
        ForEach-Object { ([string] $_).ToLowerInvariant() } | Sort-Object -Unique
    $operatorEmailSet = New-ExactMap
    foreach ($email in $operatorEmails) {
        $operatorEmailSet[$email] = $true
    }
    $operatorNames = @($knownOperatorName + @($discoveredAliases | ForEach-Object { $_.Name })) | Sort-Object -Unique
    $operatorNameSet = New-ExactMap
    foreach ($name in $operatorNames) {
        $operatorNameSet[$name] = $true
    }
    $operatorPredicate = Get-OperatorPredicate $operatorEmailSet $operatorNameSet
    $operatorCommits = @($history | Where-Object { & $operatorPredicate $_ })
    if ($operatorCommits.Count -eq 0) {
        throw 'No operator-authored commits were found in the accepted production lineage.'
    }
    return [pscustomobject]@{
        AcceptedResolved       = $acceptedResolved
        BaseResolved           = $baseResolved
        Range                  = $range
        MeasurementHead        = $measurementHead
        History                = $history
        FutureHistory          = $futureHistory
        OperatorPredicate      = $operatorPredicate
        OperatorCommits        = $operatorCommits
        FutureOperatorCommits  = @($futureHistory | Where-Object { & $operatorPredicate $_ })
        OperatorEmailSet       = $operatorEmailSet
        OperatorNameSet        = $operatorNameSet
    }
}

function New-HistoricalPathContext {
    param(
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [scriptblock] $OperatorPredicate,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef
    )

    $operatorRecords = @(Get-OperatorCommitPathRecords $History.History $OperatorPredicate)
    $operatorPathSet = New-ExactMap
    foreach ($record in $operatorRecords) {
        $operatorPathSet[[string] $record.Path] = $true
        if ($record.OldPath) {
            $operatorPathSet[[string] $record.OldPath] = $true
        }
    }
    $records = @(Get-ChangedPathRecords $History.Range)
    $pathSet = New-ExactMap
    foreach ($record in $records) {
        if (Test-PathAtRef $AcceptedRef $record.Path) {
            $pathSet[[string] $record.Path] = $true
        }
    }
    return [pscustomobject]@{
        Records             = $records
        Paths               = @($pathSet.Keys | Sort-Object)
        OperatorPathSet     = $operatorPathSet
        OperatorRecords     = $operatorRecords
    }
}

function Get-PostHistoryRecreatedPaths {
    param([Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $FutureHistory)

    $recreated = New-ExactMap
    $deleted = New-ExactMap
    foreach ($commit in $FutureHistory) {
        foreach ($record in (Get-CommitPathRecords ([string] $commit.Hash))) {
            if ($record.Status -eq 'D') {
                $deleted[[string] $record.Path] = $true
            } elseif ($record.Status -eq 'A' -and $deleted.ContainsKey([string] $record.Path)) {
                $recreated[[string] $record.Path] = $true
            } elseif ($record.Status -match '^R' -and $record.OldPath -and
                ($deleted.ContainsKey([string] $record.OldPath) -or $deleted.ContainsKey([string] $record.Path))) {
                $recreated[[string] $record.Path] = $true
            }
        }
    }
    return [pscustomobject]@{ Recreated = $recreated; Deleted = $deleted }
}

function New-PostPathContext {
    param(
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [scriptblock] $OperatorPredicate,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef,
        [Parameter(Mandatory = $true)] [string] $MeasurementHead
    )

    $postCommitRecords = @(Get-ChangedPathRecords "$AcceptedRef..$MeasurementHead")
    $workingTreeRecords = @(Get-ChangedPathRecords $MeasurementHead)
    $untrackedRecords = @(Get-UntrackedPathRecords)
    $postRecords = @($postCommitRecords + $workingTreeRecords + $untrackedRecords)
    $operatorPathSets = Get-PostAcceptedOperatorPathSets @($History.FutureHistory) $OperatorPredicate
    $workingTreePathSet = New-ExactMap
    foreach ($record in $workingTreeRecords + $untrackedRecords) {
        $workingTreePathSet[[string] $record.Path] = $true
    }
    $recreatedPaths = Get-PostHistoryRecreatedPaths @($History.FutureHistory)
    foreach ($path in (Get-WorkingTreeRecreatedPaths $MeasurementHead).Keys) {
        $recreatedPaths.Recreated[[string] $path] = $true
    }
    return [pscustomobject]@{
        PostRecords             = $postRecords
        WorkingTreePathSet      = $workingTreePathSet
        FutureOperatorPathSet   = $operatorPathSets.current
        FutureOperatorLineage   = $operatorPathSets.lineage
        RecreatedPathSet         = $recreatedPaths.Recreated
        DeletedPathSet           = $recreatedPaths.Deleted
    }
}

function New-CurrentPathSet {
    param(
        [Parameter(Mandatory = $true)] [string[]] $HistoricalPaths,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $PostRecords
    )

    $paths = New-ExactMap
    foreach ($path in $HistoricalPaths) {
        if (Test-CurrentPath $path) {
            $paths[$path] = $true
        }
    }
    foreach ($record in $PostRecords) {
        if (Test-CurrentPath ([string] $record.Path)) {
            $paths[[string] $record.Path] = $true
        }
    }
    return $paths
}

function Get-PathScopeState {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [object] $Post,
        [Parameter(Mandatory = $true)] [object] $Historical,
        [Parameter(Mandatory = $true)] [hashtable] $HistoricalAliases,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef
    )

    $language = Get-PathLanguage $Path
    $pathRecords = @($Post.PostRecords | Where-Object { $_.Path -ceq $Path })
    $hasFutureOperatorChange = $Post.FutureOperatorPathSet.ContainsKey($Path)
    $hasWorkingTreeChange = $Post.WorkingTreePathSet.ContainsKey($Path)
    $historicalPathAliases = if ($HistoricalAliases.ContainsKey($Path)) {
        @($HistoricalAliases[$Path])
    } elseif ($Historical.Paths -contains $Path) {
        @($Path)
    } else {
        @()
    }
    $isHistorical = @($historicalPathAliases).Count -gt 0
    $hasHistoricalOperatorChange = @($historicalPathAliases | Where-Object {
            $Historical.OperatorPathSet.ContainsKey([string] $_)
        }).Count -gt 0
    $isRecreated = $Post.RecreatedPathSet.ContainsKey($Path)
    $isNewAfterAccepted = -not $isHistorical -and -not (Test-PathAtRef $AcceptedRef $Path) -and
        ($hasFutureOperatorChange -or $hasWorkingTreeChange)
    $eligible = if ($Mode -eq 'Baseline') {
        $isHistorical
    } else {
        $isHistorical -or $hasFutureOperatorChange -or $hasWorkingTreeChange
    }
    return [pscustomobject]@{
        Path                    = $Path
        Language                = $language
        PathRecords             = $pathRecords
        HistoricalPathAliases   = $historicalPathAliases
        IsHistorical            = $isHistorical
        HasHistoricalOperatorChange = $hasHistoricalOperatorChange
        IsRecreated             = $isRecreated
        IsNewAfterAccepted      = $isNewAfterAccepted
        Eligible                = $eligible
        HasFutureOperatorChange = $hasFutureOperatorChange
        HasWorkingTreeChange    = $hasWorkingTreeChange
    }
}

function Get-PathScopeLines {
    param(
        [Parameter(Mandatory = $true)] [object] $State,
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef,
        [Parameter(Mandatory = $true)] [string] $MeasurementHead
    )

    $acceptedLines = [System.Collections.Generic.HashSet[int]]::new()
    foreach ($historicalPath in $State.HistoricalPathAliases) {
        if (Test-PathAtRef $AcceptedRef $historicalPath) {
            foreach ($line in (Get-OperatorBlameLines $AcceptedRef $historicalPath $History.OperatorEmailSet $History.OperatorNameSet)) {
                $null = $acceptedLines.Add($line)
            }
        }
    }
    $currentLines = [System.Collections.Generic.HashSet[int]]::new()
    if (Test-PathAtRef $MeasurementHead $State.Path) {
        foreach ($line in (Get-OperatorBlameLines $MeasurementHead $State.Path $History.OperatorEmailSet $History.OperatorNameSet)) {
            $null = $currentLines.Add($line)
        }
    }
    $candidateLines = if ($State.IsNewAfterAccepted -or $State.IsRecreated -or $State.HasWorkingTreeChange) {
        Get-AllFileLines $State.Path
    } else {
        Get-CandidateChangedLines $AcceptedRef $State.Path
    }
    return [pscustomobject]@{
        Accepted = $acceptedLines
        Current  = $currentLines
        Candidate = $candidateLines
    }
}

function Test-UnsupportedProjectPath {
    param(
        [Parameter(Mandatory = $true)] [object] $State
    )

    $projectExecutablePath = $state.HasHistoricalOperatorChange -or $state.HasFutureOperatorChange -or $state.HasWorkingTreeChange
    return $state.Language -in @('unsupported', 'unknown') -and $state.Eligible -and $projectExecutablePath
}

function Test-AnalyzablePath {
    param([Parameter(Mandatory = $true)] [object] $State)

    return $state.Language -in @('cpp', 'powershell') -and $state.Eligible
}

function Test-UnscopedExecutablePath {
    param(
        [Parameter(Mandatory = $true)] [object] $State
    )

    return $state.Language -in @('cpp', 'powershell', 'unsupported', 'unknown') -and
        @($state.PathRecords).Count -gt 0 -and -not $state.Eligible
}

function New-PathExclusionResult {
    param(
        [string] $PolicyError,
        [string] $Unscoped
    )

    return [pscustomobject]@{ Scope = $null; PolicyError = $PolicyError; Unscoped = $Unscoped }
}

function Get-PathScopeExclusion {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [object] $State
    )

    if (Test-UnsupportedProjectPath $State) {
        return New-PathExclusionResult "Unsupported or unclassified executable path '$Path'. Supported analyzers are C/C++ and PowerShell only; define a language policy/analyzer before adding this project file." $null
    }
    if (-not (Test-AnalyzablePath $State)) {
        $unscoped = if (Test-UnscopedExecutablePath $State) { $Path } else { $null }
        return New-PathExclusionResult $null $unscoped
    }
    return $null
}

function New-PathScopeResult {
    param(
        [Parameter(Mandatory = $true)] [string] $Path,
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [object] $Post,
        [Parameter(Mandatory = $true)] [object] $Historical,
        [Parameter(Mandatory = $true)] [hashtable] $HistoricalAliases,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef,
        [Parameter(Mandatory = $true)] [string] $MeasurementHead
    )

    $state = Get-PathScopeState $Path $Mode $History $Post $Historical $HistoricalAliases $AcceptedRef
    $exclusion = Get-PathScopeExclusion $Path $state
    if ($null -ne $exclusion) {
        return $exclusion
    }
    $lines = Get-PathScopeLines $state $History $AcceptedRef $MeasurementHead
    $scope = [pscustomobject]@{
        path                    = $Path
        language                = $state.Language
        status                  = if (@($state.PathRecords).Count -gt 0) { $state.PathRecords[-1].Status } else { 'A' }
        historicalPaths         = @($state.HistoricalPathAliases)
        isNewAfterAccepted      = $state.IsNewAfterAccepted
        isRecreated             = $state.IsRecreated
        hasFutureOperatorChange = $state.HasFutureOperatorChange
        acceptedOperatorLines   = $lines.Accepted
        currentOperatorLines    = $lines.Current
        candidateLines          = $lines.Candidate
    }
    return [pscustomobject]@{ Scope = $scope; PolicyError = $null; Unscoped = $null }
}

function New-ScopeContext {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [object] $Post,
        [Parameter(Mandatory = $true)] [object] $Historical,
        [Parameter(Mandatory = $true)] [string[]] $HistoricalPaths,
        [Parameter(Mandatory = $true)] [hashtable] $HistoricalAliases,
        [Parameter(Mandatory = $true)] [hashtable] $InclusionBaselineByKey,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef,
        [Parameter(Mandatory = $true)] [string] $MeasurementHead
    )

    $scopeByPath = New-ExactMap
    $scopedRecords = [System.Collections.Generic.List[object]]::new()
    $policyErrors = [System.Collections.Generic.List[string]]::new()
    $unscoped = [System.Collections.Generic.List[string]]::new()
    foreach ($path in ((New-CurrentPathSet $HistoricalPaths $Post.PostRecords).Keys | Sort-Object)) {
        $result = New-PathScopeResult $path $Mode $History $Post $Historical $HistoricalAliases $AcceptedRef $MeasurementHead
        if ($result.PolicyError) {
            $policyErrors.Add($result.PolicyError)
        }
        if ($result.Unscoped) {
            $unscoped.Add($result.Unscoped)
        }
        if ($null -ne $result.Scope) {
            $scopeByPath[$path] = $result.Scope
            $scopedRecords.Add($result.Scope)
        }
    }
    foreach ($path in ($unscoped | Sort-Object -Unique)) {
        $limitations.Add([pscustomobject]@{
                file   = $path
                reason = 'Executable path changed after the accepted checkpoint but was not attributable to an operator commit; it remains outside the project-owned complexity universe.'
            })
    }
    if ($policyErrors.Count -gt 0) {
        throw "Complexity language policy failed:`n$($policyErrors -join "`n")"
    }
    return [pscustomobject]@{ ByPath = $scopeByPath; Records = @($scopedRecords) }
}

function New-MeasurementContext {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [object] $Baseline,
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [object] $BaselineContext
    )

    $cppPaths = @($Scope.Records | Where-Object language -eq 'cpp' | ForEach-Object path | Sort-Object -Unique)
    $scriptPaths = @($Scope.Records | Where-Object language -eq 'powershell' | ForEach-Object path | Sort-Object -Unique)
    $allCppMetrics = [System.Collections.Generic.List[object]]::new()
    $allScriptMetrics = [System.Collections.Generic.List[object]]::new()
    $python = Get-PythonExecutable
    $cppMetrics = @(Get-ScopedCppMetrics $cppPaths $Scope.ByPath $Mode $Baseline.InclusionByKey $python $BaselineContext.ExplicitIdentity.byCurrentKey $allCppMetrics)
    $scriptMetrics = @(Get-PowerShellMetrics $scriptPaths $Scope.ByPath $Mode $Baseline.InclusionByKey $BaselineContext.ExplicitIdentity.byCurrentKey $allScriptMetrics)
    $metrics = @($cppMetrics + $scriptMetrics)
    if ($metrics.Count -eq 0) {
        throw 'No scoped functions were measured.'
    }
    Assert-UniqueFunctionIdentities $metrics
    $allFunctionMetrics = @($allCppMetrics + $allScriptMetrics)
    Assert-UniqueFunctionIdentities $allFunctionMetrics
    $lizardVersion = Get-LizardVersion $python
    $automatic = if ($Mode -eq 'Check') {
        New-AutomaticIdentityMaps $allFunctionMetrics $BaselineContext.AcceptedByKey $Scope.ByPath $BaselineContext.ExplicitIdentity
    } else {
        New-EmptyIdentityMaps
    }
    return [pscustomobject]@{
        CppPaths       = $cppPaths
        ScriptPaths    = $scriptPaths
        Metrics        = $metrics
        AllFunctions   = $allFunctionMetrics
        LizardVersion  = $lizardVersion
        Continuity     = Merge-IdentityMigrationMaps $BaselineContext.ExplicitIdentity $automatic
    }
}

function New-CodePathContext {
    param(
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [string[]] $CppPaths,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [string[]] $ScriptPaths
    )

    $set = New-ExactMap
    foreach ($path in $CppPaths + $ScriptPaths) {
        $set[$path] = $true
    }
    return $set
}

function New-OperatorCodeCommitRecords {
    param(
        [Parameter(Mandatory = $true)] [object[]] $Commits,
        [Parameter(Mandatory = $true)] [hashtable] $CodePathSet,
        [Parameter(Mandatory = $true)] [hashtable] $OperatorLineagePathSet
    )

    $records = [System.Collections.Generic.List[object]]::new()
    foreach ($commit in $Commits) {
        $commitRecords = @(Get-CommitPathRecords ([string] $commit.Hash))
        $hits = @($commitRecords | ForEach-Object {
                if ($CodePathSet.ContainsKey([string] $_.Path) -or $OperatorLineagePathSet.ContainsKey([string] $_.Path)) { $_.Path }
                if ($_.OldPath -and ($CodePathSet.ContainsKey([string] $_.OldPath) -or $OperatorLineagePathSet.ContainsKey([string] $_.OldPath))) { $_.OldPath }
            } | Sort-Object -Unique)
        if ($hits.Count -gt 0) {
            $records.Add([pscustomobject]@{
                    hash    = $commit.Hash
                    name    = $commit.Name
                    email   = $commit.Email
                    subject = $commit.Subject
                    files   = $hits
                })
        }
    }
    return @($records)
}

function New-InventoryFileRecords {
    param(
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [object] $Measurement,
        [Parameter(Mandatory = $true)] [object[]] $OperatorCodeRecords
    )

    $files = [System.Collections.Generic.List[object]]::new()
    foreach ($scopeRecord in $Scope.Records) {
        $path = [string] $scopeRecord.path
        $operatorFileCommits = @($OperatorCodeRecords | Where-Object { $_.files -contains $path })
        $scopedCount = @($Measurement.Metrics | Where-Object { $_.file -eq $path -and $_.scopeKind -eq 'function' }).Count
        $files.Add([ordered]@{
                path                   = $path
                language               = if ($scopeRecord.language -eq 'cpp') { 'C/C++' } else { 'PowerShell' }
                status                 = $scopeRecord.status
                operatorCommitCount    = $operatorFileCommits.Count
                operatorCommits        = @($operatorFileCommits | ForEach-Object { $_.hash })
                operatorBlameLineCount = $scopeRecord.acceptedOperatorLines.Count
                scopedFunctionCount    = $scopedCount
                currentFunctionKeys    = @($Measurement.Metrics | Where-Object { $_.file -eq $path -and $_.scopeKind -eq 'function' } | ForEach-Object { $_.key })
            })
    }
    return @($files)
}

function Get-NonCyclomaticCategory {
    param([Parameter(Mandatory = $true)] [string] $Path)

    $language = Get-PathLanguage $Path
    $extension = [IO.Path]::GetExtension($Path).ToLowerInvariant()
    if ($language -eq 'non-executable') {
        return 'Declaration/configuration/documentation path; excluded from cyclomatic targets.'
    }
    if ($language -eq 'unsupported') {
        return 'Executable language is known but has no analyzer policy; introduced project paths fail closed.'
    }
    if ($language -eq 'unknown') {
        return 'Unknown path language; introduced project paths fail closed.'
    }
    if ([IO.Path]::GetFileName($Path) -eq 'CMakeLists.txt' -or $extension -eq '.cmake') {
        return 'CMake/control script; reviewed separately because lizard is not the chosen analyzer.'
    }
    if ($extension -in @('.yaml', '.yml', '.json', '.md', '.txt')) {
        return 'Static declaration/documentation; excluded from cyclomatic targets.'
    }
    return 'Non-target changed path; excluded from cyclomatic targets by explicit language policy.'
}

function New-NonCyclomaticRecords {
    param(
        [Parameter(Mandatory = $true)] [string[]] $HistoricalPaths,
        [Parameter(Mandatory = $true)] [AllowEmptyCollection()] [object[]] $PostRecords,
        [Parameter(Mandatory = $true)] [hashtable] $CodePathSet
    )

    $nonCyclomatic = [System.Collections.Generic.List[object]]::new()
    $currentPaths = New-CurrentPathSet $HistoricalPaths $PostRecords
    foreach ($path in ($currentPaths.Keys | Sort-Object)) {
        if ($CodePathSet.ContainsKey($path)) {
            continue
        }
        $nonCyclomatic.Add([ordered]@{
                path = $path
                status = 'current'
                category = Get-NonCyclomaticCategory $path
            })
    }
    return @($nonCyclomatic)
}

function New-InventoryContext {
    param(
        [Parameter(Mandatory = $true)] [object] $Scope,
        [Parameter(Mandatory = $true)] [object] $Measurement,
        [Parameter(Mandatory = $true)] [object] $History,
        [Parameter(Mandatory = $true)] [object] $Post,
        [Parameter(Mandatory = $true)] [object] $Historical
    )

    $codePathSet = New-CodePathContext $Measurement.CppPaths $Measurement.ScriptPaths
    $operatorAttributableCommits = @($History.OperatorCommits + $History.FutureOperatorCommits)
    $operatorCodeRecords = New-OperatorCodeCommitRecords $operatorAttributableCommits $codePathSet $Post.FutureOperatorLineage
    $files = New-InventoryFileRecords $Scope $Measurement $operatorCodeRecords
    $nonCyclomatic = New-NonCyclomaticRecords $Historical.Paths $Post.PostRecords $codePathSet
    $aliases = @($History.History | Where-Object { & $History.OperatorPredicate $_ } |
        Select-Object @{ Name = 'name'; Expression = { $_.Name } }, @{ Name = 'email'; Expression = { $_.Email } } -Unique)
    return [ordered]@{
        schemaVersion                           = 2
        githubAccount                           = 'YMGPwcca'
        baseRef                                 = $History.BaseResolved
        acceptedRef                             = $History.AcceptedResolved
        discoveryMethod                         = 'historical git log BASE..accepted plus post-accepted operator commit paths and accepted/current git blame; upstream-only future executable paths are reported but excluded; no filename ownership inference'
        operatorIdentityAliases                 = @($aliases)
        operatorAuthoredCommitCountInLineage   = $History.OperatorCommits.Count
        operatorAuthoredCodeCommitCountInScope = $operatorCodeRecords.Count
        operatorAuthoredCommits                 = @($History.OperatorCommits | ForEach-Object {
                [ordered]@{ hash = $_.Hash; name = $_.Name; email = $_.Email; subject = $_.Subject }
            })
        cyclomaticTargetFiles                   = @($files)
        nonCyclomaticChangedFiles               = @($nonCyclomatic)
        scopeRules                              = @(
            'Historical C/C++ scope is the complete current function when accepted-HEAD blame attributes at least one line in the function to an operator commit.',
            'After and Check add every current C/C++ or PowerShell file added by operator work after acceptedRef, including descendants of later file renames, and add changed functions in operator-modified files by current blame or candidate diff lines.',
            'A renamed historical file retains exact path aliases for baseline comparison; a deleted-and-recreated path is measured as a new file identity.',
            'Only C/C++ and PowerShell are analyzable. Known unsupported or unknown executable paths introduced by project work fail closed instead of entering the wrong parser.',
            'CMake/control files and static declarations are recorded for review but are not function-level CC targets.',
            'Baseline measures the accepted checkout; After and Check measure the current working tree/HEAD.'
        )
    }
}

function New-ComplexityRunContext {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [AllowEmptyString()] [string] $BaseRef,
        [Parameter(Mandatory = $true)] [string] $AcceptedRef,
        [Parameter(Mandatory = $true)] [string] $BeforePath,
        [Parameter(Mandatory = $true)] [string] $BaselinePath,
        [Parameter(Mandatory = $true)] [string] $IdentityMigrationsPath
    )

    $history = New-HistoryContext $BaseRef $AcceptedRef
    $historical = New-HistoricalPathContext $history $history.OperatorPredicate $history.AcceptedResolved
    $post = New-PostPathContext $history $history.OperatorPredicate $history.AcceptedResolved $history.MeasurementHead
    $historicalAliases = Get-HistoricalPathAliases $historical.Paths $post.PostRecords
    $baseline = New-BaselineContext $Mode $BeforePath $BaselinePath $IdentityMigrationsPath $history.BaseResolved $history.AcceptedResolved
    $scope = New-ScopeContext $Mode $history $post $historical $historical.Paths $historicalAliases $baseline.InclusionByKey $history.AcceptedResolved $history.MeasurementHead
    $measurement = New-MeasurementContext $Mode $baseline $scope $history $baseline
    $inventory = New-InventoryContext $scope $measurement $history $post $historical
    return [pscustomobject]@{
        History        = $history
        Historical     = $historical
        Post           = $post
        HistoricalAliases = $historicalAliases
        Baseline       = $baseline
        Scope          = $scope
        Measurement    = $measurement
        Inventory      = $inventory
    }
}

function Get-ComplexityAllowlist {
    param([Parameter(Mandatory = $true)] [string] $Path)

    if (-not (Test-Path -LiteralPath (Get-RepoFilePath $Path))) {
        return @()
    }
    $allowlist = @((Read-JsonFile $Path) | Where-Object { $null -ne $_ })
    if ($null -eq $allowlist) {
        return @()
    }
    return $allowlist
}

function Get-ExceptionMetricViolation {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [object] $Exception,
        [Parameter(Mandatory = $true)] [bool] $IsException
    )

    if ($Exception -and -not $IsException) {
        return "Exception complexity mismatch: $($Metric.file):$($Metric.startLine) $($Metric.function) recorded $($Exception.measuredCC), current $($Metric.cyclomaticComplexity)."
    }
    return $null
}

function Get-CeilingMetricViolations {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [Parameter(Mandatory = $true)] [AllowNull()] [object] $BaselineMetric,
        [Parameter(Mandatory = $true)] [bool] $IsException
    )

    $violations = [System.Collections.Generic.List[string]]::new()
    if ([int] $Metric.cyclomaticComplexity -gt 10 -and -not $IsException) {
        $violations.Add("CC > 10: $($Metric.file):$($Metric.startLine) $($Metric.function) measured $($Metric.cyclomaticComplexity).")
    }
    if (-not $BaselineMetric -and [int] $Metric.cyclomaticComplexity -gt 10 -and -not $IsException) {
        $scopeLabel = if ($Metric.scopeKind -ceq 'script-body') { 'script-body' } else { 'function' }
        $violations.Add("New $scopeLabel exceeds CC 10: $($Metric.file):$($Metric.startLine) $($Metric.function) measured $($Metric.cyclomaticComplexity).")
    }
    return @($violations)
}

function Get-RegressionMetricViolation {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [object] $BaselineMetric,
        [Parameter(Mandatory = $true)] [bool] $IsException
    )

    if ($BaselineMetric -and [int] $Metric.cyclomaticComplexity -gt [int] $BaselineMetric.cyclomaticComplexity -and -not $IsException) {
        return "Complexity increased: $($Metric.file):$($Metric.startLine) $($Metric.function) baseline $($BaselineMetric.cyclomaticComplexity), current $($Metric.cyclomaticComplexity)."
    }
    return $null
}

function Get-CheckMetricViolations {
    param(
        [Parameter(Mandatory = $true)] [object] $Metric,
        [object] $BaselineMetric,
        [object] $Exception
    )

    $isException = $null -ne $Exception -and [int] $Exception.measuredCC -eq [int] $Metric.cyclomaticComplexity
    $violations = [System.Collections.Generic.List[string]]::new()
    foreach ($violation in @(
            Get-ExceptionMetricViolation $Metric $Exception $isException
            Get-CeilingMetricViolations $Metric $BaselineMetric $isException
            Get-RegressionMetricViolation $Metric $BaselineMetric $isException
        )) {
        if ($null -ne $violation) {
            $violations.Add($violation)
        }
    }
    return @($violations)
}

function Get-ScriptBodyWarnings {
    param([Parameter(Mandatory = $true)] [object[]] $Metrics)

    return @(Get-SortedScriptBodies $Metrics | Where-Object { [int] $_.cyclomaticComplexity -gt 5 } | ForEach-Object {
            "Complexity scope warning: script-body $($_.file):$($_.startLine)-$($_.endLine) CC $($_.cyclomaticComplexity)."
        })
}

function Invoke-BaselineMode {
    param([Parameter(Mandatory = $true)] [object] $Context)

    $report = New-ReportObject $Context.Measurement.Metrics 'baseline' $Context.History.BaseResolved $Context.History.AcceptedResolved $Context.History.MeasurementHead $Context.History.OperatorCommits.Count $Context.Measurement.LizardVersion
    $jsonText = $report | ConvertTo-Json -Depth 30
    Write-Utf8File $InventoryPath ($Context.Inventory | ConvertTo-Json -Depth 30)
    Write-Utf8File ($InventoryPath -replace '\.json$', '.md') (New-InventoryMarkdown $Context.Inventory)
    $baselineOutput = if ($JsonPath) { $JsonPath } else { 'complexity-baseline.json' }
    $baselineMarkdown = if ($MarkdownPath) { $MarkdownPath } else { 'complexity-baseline.md' }
    Write-Utf8File $baselineOutput $jsonText
    Write-Utf8File $baselineMarkdown (New-BaselineMarkdown $report $Context.Inventory)
    Write-Utf8File $BeforePath $jsonText
    Write-Output "Baseline written: $baselineOutput"
    Write-Output "Baseline markdown written: $baselineMarkdown"
    Write-Output "Ownership inventory written: $InventoryPath"
    Write-Output ((Format-StatTable $report.summary))
    exit 0
}

function Invoke-AfterMode {
    param([Parameter(Mandatory = $true)] [object] $Context)

    if ($null -eq $Context.Baseline.BeforeReport) {
        throw "Before report '$BeforePath' was not found."
    }
    $allowlist = Get-ComplexityAllowlist $AllowlistPath
    $report = New-ReportObject $Context.Measurement.Metrics 'after' $Context.History.BaseResolved $Context.History.AcceptedResolved $Context.History.MeasurementHead $Context.History.OperatorCommits.Count $Context.Measurement.LizardVersion
    $afterOutput = if ($JsonPath) { $JsonPath } else { 'complexity-after.json' }
    $afterMarkdown = if ($MarkdownPath) { $MarkdownPath } else { 'complexity-report.md' }
    Write-Utf8File $afterOutput ($report | ConvertTo-Json -Depth 30)
    Write-Utf8File $afterMarkdown (New-ComparisonMarkdown $Context.Baseline.BeforeReport $report $Context.Inventory $Context.Scope.ByPath $allowlist)
    Write-Output "After report written: $afterOutput"
    Write-Output "Comparison report written: $afterMarkdown"
    Write-Output ((Format-StatTable $report.summary))
    exit 0
}

function Invoke-CheckMode {
    param([Parameter(Mandatory = $true)] [object] $Context)

    $baseline = if ($null -ne $Context.Baseline.AcceptedReport) { $Context.Baseline.AcceptedReport } else {
        throw "Accepted complexity baseline '$BaselinePath' was not found. Run After first or pass -BaselinePath."
    }
    $allowlist = Get-ComplexityAllowlist $AllowlistPath
    $baselineByKey = New-MetricKeyMap @($baseline.functions) 'accepted complexity baseline'
    Assert-IdentityMigrationTargets $Context.Measurement.Continuity $Context.Measurement.AllFunctions $Context.Scope.ByPath $baselineByKey
    $violations = [System.Collections.Generic.List[string]]::new()
    foreach ($identityViolation in (Get-UnmigratedIdentityViolations $Context.Measurement.AllFunctions $baselineByKey $Context.Scope.ByPath $Context.Measurement.Continuity)) {
        $violations.Add($identityViolation)
    }
    foreach ($metric in (Get-EnforcedScopeMetrics $Context.Measurement.Metrics)) {
        $baselineMetric = Find-BeforeMetric $metric $baselineByKey $Context.Scope.ByPath[[string] $metric.file] $Context.Measurement.Continuity.byCurrentKey
        $exception = Get-ExactException $metric $baselineMetric $allowlist
        foreach ($violation in (Get-CheckMetricViolations $metric $baselineMetric $exception)) {
            $violations.Add($violation)
        }
    }
    Write-Output ((Format-StatTable (Get-Statistics $Context.Measurement.Metrics)))
    Write-Output ((Format-ScopeStatTable (Get-Statistics $Context.Measurement.Metrics 'script-body') 'Script bodies'))
    foreach ($warning in (Get-ScriptBodyWarnings $Context.Measurement.Metrics)) {
        Write-Output $warning
    }
    foreach ($note in ($limitations | Sort-Object file, reason)) {
        Write-Output "Complexity scope note: $($note.file) — $($note.reason)"
    }
    if ($violations.Count -gt 0) {
        throw "Complexity regression gate failed:`n$($violations -join "`n")"
    }
    Write-Output 'Complexity regression gate: PASS'
}

function Invoke-ComplexityMode {
    param(
        [Parameter(Mandatory = $true)] [string] $Mode,
        [Parameter(Mandatory = $true)] [object] $Context
    )

    if ($Mode -eq 'Baseline') {
        Invoke-BaselineMode $Context
    }
    if ($Mode -eq 'After') {
        Invoke-AfterMode $Context
    }
    Invoke-CheckMode $Context
}

function Invoke-ComplexityRun {
    $context = New-ComplexityRunContext $Mode $BaseRef $AcceptedRef $BeforePath $BaselinePath $IdentityMigrationsPath
    if ($Mode -eq 'Check' -and $JsonPath) {
        $report = New-ReportObject $context.Measurement.Metrics 'check' $context.History.BaseResolved $context.History.AcceptedResolved $context.History.MeasurementHead $context.History.OperatorCommits.Count $context.Measurement.LizardVersion
        Write-Utf8File $JsonPath ($report | ConvertTo-Json -Depth 30)
    }
    Invoke-ComplexityMode $Mode $context
}

$null = Invoke-ComplexityRun
