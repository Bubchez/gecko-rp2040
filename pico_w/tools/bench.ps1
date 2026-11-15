# SPDX-License-Identifier: MIT
# PowerShell 5.1 / 7. No firmware flashing, drive copying or Gecko communication.
[CmdletBinding()]
param(
    [switch]$IsolatedPico,
    [switch]$VerifyOnly,
    [string]$Port,
    [switch]$Reprovision,
    [string]$Python
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$Work = Join-Path $Root 'work/pico-bench'
$ReportFolder = Join-Path $Work 'reports'
$ReportPath = Join-Path $ReportFolder ('bench-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ') + '.json')
$ExitCode = 1
$RunnerStarted = $false
$BootstrapStage = 'mode'
$Stages = @('artifact','usb_identity','provisioning','wifi_online','http_identity','http_authentication',
            'http_read_only','http_malformed','status_freshness','diagnostics','wifi_reconnection','post_recovery')

function Write-BootstrapReport {
    $Checks = @($Stages | ForEach-Object { @{ id=$_; status='not_run'; code='ok'; duration_ms=0; metrics=@{} } })
    $Document = @{
        report_version='1.0'; tool_version='0.1.0'; started_at=[DateTime]::UtcNow.ToString('o')
        platform=$(if ($env:OS -eq 'Windows_NT') { 'windows' } elseif ($PSVersionTable.ContainsKey('Platform') -and $PSVersionTable.Platform -eq 'Unix' -and $PSVersionTable.OS -match 'Linux') { 'linux' } else { 'other' })
        overall='fail'; blocker='bootstrap_failed'; bootstrap_step=$BootstrapStage; isolation_declared=[bool]$IsolatedPico
        hardware_accessed=$(if ($RunnerStarted -and $IsolatedPico) { $null } else { $false }); artifact=$null; checks=$Checks
        unverified=@('electrical_gpio_levels','flash_readback_attestation','heap_stack_irq_watermarks',
            'real_access_point_outage','flash_power_loss_endurance','windows_usb_if_not_run_on_windows',
            'all_gecko_electrical_bus_protocol_behaviour')
    }
    New-Item -ItemType Directory -Force -Path $ReportFolder | Out-Null
    [IO.File]::WriteAllText($ReportPath, ($Document | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
    Write-Host ('Redacted report: ' + $ReportPath)
}

function Invoke-Setup([string]$File, [string[]]$Arguments) {
    # Windows PowerShell 5.1 turns native stderr into ErrorRecords. Expected
    # nonzero probes must not bypass dependency installation or expose raw output.
    $Previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $File @Arguments 2>&1 | Out-Null
        return $LASTEXITCODE
    } finally { $ErrorActionPreference = $Previous }
}

function Set-PrivateAcl([string]$Path, [bool]$Directory) {
    $Sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    if ($Directory) {
        $Acl = New-Object Security.AccessControl.DirectorySecurity
        $Inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    } else {
        $Acl = New-Object Security.AccessControl.FileSecurity
        $Inheritance = [Security.AccessControl.InheritanceFlags]::None
    }
    $Acl.SetOwner($Sid)
    $Acl.SetAccessRuleProtection($true, $false)
    foreach ($Identity in @($Sid, (New-Object Security.Principal.SecurityIdentifier('S-1-5-18')))) {
        $Rule = New-Object Security.AccessControl.FileSystemAccessRule($Identity, 'FullControl', $Inheritance, 'None', 'Allow')
        $Acl.AddAccessRule($Rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $Acl
}

Push-Location $Root
try {
    New-Item -ItemType Directory -Force -Path $Work | Out-Null
    $Ignore = Join-Path $Root 'work/.gitignore'
    if (-not (Test-Path -LiteralPath $Ignore)) { [IO.File]::WriteAllText($Ignore, "*`n") }
    if ($IsolatedPico -and $VerifyOnly) { throw 'mode' }
    if (($Reprovision -or $Port) -and -not $IsolatedPico) { throw 'isolation' }
    $BootstrapStage = 'artifact_hash'
    # Bootstrap hash check. Python performs complete UF2/input/safety verification.
    $Artifacts = Join-Path $Root 'pico_w/artifacts/t09'
    $Manifest = Get-Content -LiteralPath (Join-Path $Artifacts 'manifest.json') -Raw | ConvertFrom-Json
    $Image = Join-Path $Artifacts 'gecko_pico_w_SIMULATION_NO_I2C.uf2'
    if ((Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Manifest.uf2_sha256) { throw 'hash' }
    $LaunchArgs = @()
    $BootstrapStage = 'python'
    if ($Python) {
        $Launcher = (Get-Command $Python -ErrorAction Stop).Source
    } elseif (Get-Command py -ErrorAction SilentlyContinue) {
        $Launcher = (Get-Command py).Source
        $LaunchArgs = @('-3')
    } elseif (Get-Command python3 -ErrorAction SilentlyContinue) {
        $Launcher = (Get-Command python3).Source
    } else {
        throw 'python'
    }
    & $Launcher @LaunchArgs -c 'import sys; sys.exit(0 if sys.version_info >= (3,12) else 1)'
    if ($LASTEXITCODE -ne 0) { throw 'python_version' }
    $RunPython = $Launcher
    $RunArgs = $LaunchArgs
    if ($IsolatedPico) {
        $BootstrapStage = 'private_acl'
        $Private = Join-Path $Work 'private'
        New-Item -ItemType Directory -Force -Path $Private | Out-Null
        if ($env:OS -eq 'Windows_NT') {
            Set-PrivateAcl $Private $true
            $Token = Join-Path $Private 'token.txt'
            if (Test-Path -LiteralPath $Token) { Set-PrivateAcl $Token $false }
        }
        $EnvPath = Join-Path $Work 'env'
        $BootstrapStage = 'environment'
        $RunPython = Join-Path $EnvPath 'Scripts/python.exe'
        if ($env:OS -ne 'Windows_NT') { $RunPython = Join-Path $EnvPath 'bin/python' }
        if (-not (Test-Path -LiteralPath $RunPython)) {
            if ((Invoke-Setup $Launcher ($LaunchArgs + @('-m','venv',$EnvPath))) -ne 0) { throw 'venv' }
            if (-not (Test-Path -LiteralPath $RunPython)) { throw 'venv_python' }
        }
        $RunArgs = @()
        $BootstrapStage = 'dependencies'
        $Probe = "import importlib.metadata as m,sys; v={d.metadata.get('Name','').lower():d.version for d in m.distributions()}; sys.exit(0 if v.get('pyserial')=='3.5' and v.get('jsonschema')=='4.23.0' else 1)"
        if ((Invoke-Setup $RunPython @('-c',$Probe)) -ne 0) {
            if ((Invoke-Setup $RunPython @('-m','pip','--disable-pip-version-check','install','--quiet','-r',(Join-Path $PSScriptRoot 'requirements-bench.txt'))) -ne 0) { throw 'dependencies' }
        }
    }
    $Arguments = @((Join-Path $PSScriptRoot 'bench.py'), '--report', $ReportPath)
    if ($IsolatedPico) { $Arguments += '--isolated-pico' } else { $Arguments += '--verify-only' }
    if ($Port) { $Arguments += @('--port', $Port) }
    if ($Reprovision) { $Arguments += '--reprovision' }
    $RunnerStarted = $true
    $BootstrapStage = 'runner'
    & $RunPython @RunArgs @Arguments
    $ExitCode = $LASTEXITCODE
    if (-not (Test-Path -LiteralPath $ReportPath)) { throw 'missing_report' }
    if ($ExitCode -ne 0) {
        Write-Host 'Check the report code and pico_w/docs/ISOLATED_BENCH.md. A firmware mismatch needs a manual UF2 copy; this launcher never flashes.'
    }
} catch {
    Write-Host 'Bench setup failed before completion. Python 3.12+ with the py launcher is required. No firmware was flashed.'
    try { if (-not (Test-Path -LiteralPath $ReportPath)) { Write-BootstrapReport } } catch { Write-Host 'Report could not be written.' }
    $ExitCode = 1
} finally {
    Pop-Location
}
exit $ExitCode
