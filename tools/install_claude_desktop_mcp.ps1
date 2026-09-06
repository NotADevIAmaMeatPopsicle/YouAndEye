#requires -Version 5.1
<#
.SYNOPSIS
    Registers the YouandEye local STDIO MCP server with the Claude desktop app.

.DESCRIPTION
    Adds a "youandeye" entry to claude_desktop_config.json exposing the project's
    four semantic face tools:
    express, face_status, face_capabilities, neutral.

    The script is additive and non-destructive:
      * an existing config is backed up before any write
      * unrelated servers in the config are preserved untouched
      * re-running replaces only the "youandeye" entry (idempotent)

    It does NOT flash firmware, open a serial port, or install system software.
    The MCP server connects lazily on the first physical tool call.

.PARAMETER ProjectRoot
    Repository root passed to `uv run --directory`. Defaults to the parent of tools/.

.PARAMETER ConfigPath
    Target config. Defaults to %APPDATA%\Claude\claude_desktop_config.json.

.PARAMETER DryRun
    Print only the proposed YouandEye entry without writing anything. Existing
    desktop preferences and unrelated server configuration are never printed.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\install_claude_desktop_mcp.ps1 -DryRun
    powershell -ExecutionPolicy Bypass -File tools\install_claude_desktop_mcp.ps1
#>
[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$ConfigPath,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$SERVER_NAME = 'youandeye'

# --- Resolve project root -------------------------------------------------
if (-not $ProjectRoot) {
    $ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}
if (-not (Test-Path (Join-Path $ProjectRoot 'pyproject.toml'))) {
    throw "No pyproject.toml under '$ProjectRoot'. Pass -ProjectRoot explicitly."
}
Write-Host "Project root : $ProjectRoot"

# --- Resolve uv -----------------------------------------------------------
# The desktop app launches MCP servers with a minimal PATH, so an absolute
# path to uv.exe is required; a bare "uv" frequently fails to spawn.
$uvCmd = Get-Command uv -ErrorAction SilentlyContinue
if ($uvCmd) {
    $uvPath = $uvCmd.Source
} else {
    $fallback = Join-Path $env:USERPROFILE '.local\bin\uv.exe'
    if (Test-Path $fallback) {
        $uvPath = (Resolve-Path $fallback).Path
    } else {
        throw "uv not found on PATH or at '$fallback'. Install uv, then re-run."
    }
}
Write-Host "uv           : $uvPath"

# --- Resolve config path --------------------------------------------------
if (-not $ConfigPath) {
    $ConfigPath = Join-Path $env:APPDATA 'Claude\claude_desktop_config.json'
}
Write-Host "Config       : $ConfigPath"

# --- Load existing config -------------------------------------------------
$config = $null
if (Test-Path $ConfigPath) {
    $raw = Get-Content -Raw -Path $ConfigPath -Encoding UTF8
    if ($raw.Trim()) {
        try {
            $config = $raw | ConvertFrom-Json
        } catch {
            throw "'$ConfigPath' is not valid JSON. Fix or move it, then re-run. ($_)"
        }
    }
}
if (-not $config) {
    $config = [PSCustomObject]@{}
    Write-Host "Config       : none found, a new one will be created"
}

# --- Build the server entry ----------------------------------------------
$serverEntry = [PSCustomObject]@{
    command = $uvPath
    args    = @('run', '--directory', $ProjectRoot, '--extra', 'serial', 'youandeye-mcp')
    env     = [PSCustomObject]@{
        YOUANDEYE_PORT = 'auto'
    }
}

# --- Merge, preserving every unrelated server ----------------------------
$topLevel = @($config.PSObject.Properties.Name)
if (($topLevel -notcontains 'mcpServers') -or (-not $config.mcpServers)) {
    $config | Add-Member -MemberType NoteProperty -Name 'mcpServers' -Value ([PSCustomObject]@{}) -Force
}
$existing = @($config.mcpServers.PSObject.Properties.Name)
if ($existing -contains $SERVER_NAME) {
    Write-Host "Action       : replacing existing '$SERVER_NAME' entry"
} else {
    Write-Host "Action       : adding '$SERVER_NAME' entry"
}
$others = @($existing | Where-Object { $_ -ne $SERVER_NAME })
if ($others.Count -gt 0) {
    Write-Host "Preserving   : $($others.Count) unrelated server entr$(if ($others.Count -eq 1) { 'y' } else { 'ies' })"
}
$config.mcpServers | Add-Member -MemberType NoteProperty -Name $SERVER_NAME -Value $serverEntry -Force

# Depth must exceed the deepest nesting in the file. The desktop app stores its
# whole preferences tree here, and ConvertTo-Json silently replaces anything past
# -Depth with a type-name string instead of the real object. 64 is well clear of
# the observed ~8 levels while staying under the cmdlet's maximum of 100.
$json = $config | ConvertTo-Json -Depth 64

if ($DryRun) {
    Write-Host ''
    Write-Host '--- DRY RUN, nothing written ---'
    [PSCustomObject]@{ mcpServers = [PSCustomObject]@{ youandeye = $serverEntry } } |
        ConvertTo-Json -Depth 8 |
        Write-Output
    return
}

# --- Back up, then write --------------------------------------------------
$dir = Split-Path -Parent $ConfigPath
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
if (Test-Path $ConfigPath) {
    $backup = "$ConfigPath.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    Copy-Item -Path $ConfigPath -Destination $backup -Force
    Write-Host "Backup       : $backup"
}

# UTF-8 without BOM; PowerShell 5.1's Out-File/Set-Content would emit a BOM.
[System.IO.File]::WriteAllText($ConfigPath, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Host ''
Write-Host "Installed '$SERVER_NAME'. Fully quit the Claude desktop app (tray icon -> Quit) and reopen it."
Write-Host "Verify with the 'face_capabilities' tool; it reports device availability without opening the port."
