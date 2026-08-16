<#
.SYNOPSIS
    Diffractor repository lint: the mechanically checkable subset of AGENTS.md.

.DESCRIPTION
    AGENTS.md states architectural rules as prose. Prose is advisory; this script is not.
    Every rule here corresponds to a named rule in AGENTS.md and fails the build when broken,
    so a boundary violation is caught by `.\dd.ps1 test` rather than by review.

    Only rules that can be decided by inspecting text belong here. Rules that need type or
    call-graph knowledge -- "no I/O under an index lock", "no UI-owned object captured by a
    worker" -- stay in AGENTS.md as review rules, because a lint that guesses at them would
    produce false positives and be turned off.

.PARAMETER Fix
    Reserved. No rule is auto-fixable today.

.EXAMPLE
    pwsh -File tools/lint_repo.ps1
#>

[CmdletBinding()]
param(
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo

try {
    # secrets.h is generated locally and git-ignored; it is not part of the source contract.
    $sourceFiles = Get-ChildItem src -File -Include *.cpp, *.h -Recurse |
        Where-Object { $_.Name -ne 'secrets.h' }

    # Instruction and prompt files carry the same routing as the docs and rot the same way.
    $docFiles = @(Get-ChildItem docs -Filter *.md) +
        @(Get-ChildItem .github/instructions -Filter *.md -ErrorAction SilentlyContinue) +
        @(Get-ChildItem .github/prompts -Filter *.md -ErrorAction SilentlyContinue) +
        @(Get-Item AGENTS.md) + @(Get-Item README.md)

    $rules = [ordered]@{}

    # ---------------------------------------------------------------- code boundaries

    $rules['purpose-comment'] = @{
        Why   = 'AGENTS.md "Working rules": every src file states what it is for.'
        Check = {
            $sourceFiles | Where-Object {
                ((Get-Content $_.FullName -TotalCount 25) -join "`n") -notmatch '//\s*Purpose:'
            } | ForEach-Object { "src/$($_.Name): no '// Purpose:' in the first 25 lines" }
        }
    }

    $rules['no-app-threads'] = @{
        Why   = 'AGENTS.md "Strict code anti-patterns": use async_strategy or an existing queue, never a raw thread.'
        Check = {
            $sourceFiles |
                Where-Object { $_.Name -notmatch '^(platform_|test_)' } |
                Select-String -Pattern '\bstd::(thread|jthread)\s+[A-Za-z_]' |
                ForEach-Object { "src/$($_.Filename):$($_.LineNumber): $($_.Line.Trim())" }
        }
    }

    $rules['platform-containment'] = @{
        Why   = 'AGENTS.md "Working rules": platform-specific code exists only in platform* files.'
        Check = {
            $sourceFiles |
                Where-Object { $_.Name -notmatch '^(platform_|platform\.h$|test_platform_)' } |
                Select-String -Pattern '#include\s*[<"](?:[Ww]indows\.h|d3d11|dxgi|dwrite|shlobj|shellapi|wincodec)|\b(?:HWND|LRESULT|WPARAM|LPARAM)\b' |
                ForEach-Object { "src/$($_.Filename):$($_.LineNumber): $($_.Line.Trim())" }
        }
    }

    $rules['sqlite-containment'] = @{
        Why   = 'docs/implementation.md "SQLite connection ownership": database access belongs to its owning module.'
        Check = {
            $sourceFiles |
                Where-Object { $_.Name -notmatch '^(model_db|model_tile_cache|test_)' } |
                Select-String -Pattern '\bsqlite3_' |
                ForEach-Object { "src/$($_.Filename):$($_.LineNumber): $($_.Line.Trim())" }
        }
    }

    $rules['no-const-pointer-cast'] = @{
        Why   = 'AGENTS.md "Thread ownership": never cast away const to publish a worker result.'
        Check = {
            $sourceFiles |
                Select-String -Pattern '\bconst_pointer_cast\b' |
                ForEach-Object { "src/$($_.Filename):$($_.LineNumber): $($_.Line.Trim())" }
        }
    }

    $rules['frame-accessor'] = @{
        # ui_dialog.h is excluded because it declares both a ui::frame_ptr and a
        # ui::control_frame_ptr named _frame in different classes, and only the first is
        # covered by the rule. Text alone cannot tell the two apart.
        Why   = 'AGENTS.md "Absent handles": a ui::frame_ptr member is reached only through its no_frame() accessor.'
        Check = {
            $sourceFiles |
                Where-Object { $_.Name -ne 'ui_dialog.h' } |
                Where-Object {
                    $t = Get-Content $_.FullName -Raw
                    $t -match '(?m)^\s*(?:ui::)?frame_ptr\s+_frame\s*;' -and $t -notmatch 'control_frame_ptr\s+_frame'
                } |
                Select-String -Pattern '(?<![A-Za-z0-9_])_frame->' |
                ForEach-Object { "src/$($_.Filename):$($_.LineNumber): $($_.Line.Trim()) -- use frame() instead" }
        }
    }

    # ---------------------------------------------------------------- documentation integrity
    # Docs are read as fact by an agent, so a stale one is worse than a missing one.

    $rules['doc-links'] = @{
        Why   = 'A broken link in a doc an agent is told to consult sends it to the wrong place.'
        Check = {
            foreach ($d in $docFiles) {
                $text = Get-Content $d.FullName -Raw
                foreach ($m in [regex]::Matches($text, '\]\(([^)#:]+?)(?:#[^)]*)?\)')) {
                    $target = $m.Groups[1].Value.Trim()
                    if ($target -match '^(https?:|mailto:)') { continue }
                    if (-not (Test-Path (Join-Path $d.DirectoryName $target))) {
                        "$($d.Name): broken link -> $target"
                    }
                }
            }
        }
    }

    $rules['doc-anchors'] = @{
        Why   = 'A link to a section that no longer exists lands the reader at the top of a 1200-line document.'
        Check = {
            # GitHub slug rules: lower-case, punctuation dropped, spaces to hyphens.
            $headings = @{}
            $slugsFor = {
                param($path)
                $set = @{}
                foreach ($line in (Get-Content $path)) {
                    if ($line -match '^#{1,6}\s+(.*)$') {
                        $s = $Matches[1].Trim().ToLowerInvariant()
                        $s = $s -replace '`', '' -replace '\[([^\]]*)\]\([^)]*\)', '$1'
                        $s = $s -replace '[^\p{L}\p{Nd} _-]', ''
                        $set[($s -replace ' ', '-')] = $true
                    }
                }
                $set
            }

            foreach ($d in $docFiles) {
                $text = Get-Content $d.FullName -Raw
                foreach ($m in [regex]::Matches($text, '\]\(([^)\s]*?)#([^)\s]+)\)')) {
                    $target = $m.Groups[1].Value
                    $file = if ($target) { Join-Path $d.DirectoryName $target } else { $d.FullName }
                    if (-not (Test-Path $file)) { continue }   # doc-links already reports this
                    $key = (Resolve-Path $file).Path
                    if (-not $headings.ContainsKey($key)) { $headings[$key] = & $slugsFor $key }
                    $frag = $m.Groups[2].Value.ToLowerInvariant()
                    if (-not $headings[$key].ContainsKey($frag)) {
                        "$($d.Name): #$frag is not a heading in $(Split-Path $file -Leaf)"
                    }
                }
            }
        }
    }

    $rules['doc-code-anchors'] = @{
        # third-party.md is excluded: its "src/..." paths are paths inside vendored packages,
        # not paths in this repository.
        Why   = 'The "Where this lives" anchors are the routing an agent uses; a renamed file must not silently orphan one.'
        Check = {
            foreach ($d in ($docFiles | Where-Object { $_.Name -ne 'third-party.md' })) {
                $text = Get-Content $d.FullName -Raw
                foreach ($m in [regex]::Matches($text, '(?<![\w./-])(?:\.\./)*(src/[A-Za-z0-9_./-]+\.(?:h|cpp))')) {
                    $p = $m.Groups[1].Value
                    if (-not (Test-Path $p)) { "$($d.Name): references missing $p" }
                }
            }
        }
    }

    $rules['doc-ownership'] = @{
        Why   = 'AGENTS.md "Information ownership": every doc has exactly one owner, and the table is that claim.'
        Check = {
            $agents = Get-Content AGENTS.md -Raw
            # v-*.md are archived release notes; only the current one is named in the table.
            Get-ChildItem docs -Filter *.md |
                Where-Object { $_.Name -notmatch '^v-\d' } |
                Where-Object { $agents -notmatch [regex]::Escape("docs/$($_.Name)") } |
                ForEach-Object { "AGENTS.md: docs/$($_.Name) has no owner row in the ownership table" }
        }
    }

    $rules['doc-where-this-lives'] = @{
        # v-*.md are version records -- release notes and post-release context -- not subject
        # documents, and own no code.
        Why   = 'AGENTS.md: each subject document names the source that implements it, so a reader can route without searching.'
        Check = {
            Get-ChildItem docs -Filter *.md |
                Where-Object { $_.Name -notmatch '^v-' } |
                Where-Object { (Get-Content $_.FullName -Raw) -notmatch '(?m)^##+\s+Where this lives\s*$' } |
                ForEach-Object { "docs/$($_.Name): no '## Where this lives' section" }
        }
    }

    $rules['test-taxonomy'] = @{
        Why   = 'docs/testing.md "Taxonomy": a test file with no row is a subject nobody has claimed.'
        Check = {
            $testing = Get-Content docs\testing.md -Raw
            Get-ChildItem src -Filter 'test_*.cpp' |
                Where-Object { $testing -notmatch [regex]::Escape($_.Name) } |
                ForEach-Object { "docs/testing.md: no row for src/$($_.Name)" }
        }
    }

    # ---------------------------------------------------------------- run

    $failed = 0
    $violationCount = 0

    foreach ($name in $rules.Keys) {
        $violations = @(& $rules[$name].Check)

        if ($violations.Count -eq 0) {
            if (-not $Quiet) { Write-Host ("  PASS  {0}" -f $name) -ForegroundColor DarkGray }
        }
        else {
            $failed++
            $violationCount += $violations.Count
            Write-Host ("  FAIL  {0} ({1})" -f $name, $violations.Count) -ForegroundColor Red
            Write-Host ("        {0}" -f $rules[$name].Why) -ForegroundColor Yellow
            $violations | Select-Object -First 20 | ForEach-Object { Write-Host "        $_" }
            if ($violations.Count -gt 20) {
                Write-Host ("        ... and {0} more" -f ($violations.Count - 20))
            }
        }
    }

    Write-Host ""
    if ($failed -eq 0) {
        Write-Host ("Lint passed: {0} rules, no violations." -f $rules.Count) -ForegroundColor Green
        exit 0
    }

    Write-Host ("Lint FAILED: {0} of {1} rules, {2} violations." -f $failed, $rules.Count, $violationCount) -ForegroundColor Red
    exit 1
}
finally {
    Pop-Location
}
