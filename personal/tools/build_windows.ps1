<#
Build this MuseScore 3 Evolution checkout using its local Qt 5 SDK.
No downloads, global environment changes, cleanup, or source edits.
Example: .\personal\tools\build_windows.ps1 -Configuration Debug -Install
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('Visual Studio 16 2019', 'Visual Studio 17 2022')]
    [string]$Generator = 'Visual Studio 16 2019',
    [string]$QtRoot,
    [string]$BuildDirectory = 'msvc.build_probe_x64',
    [string]$InstallDirectory = 'msvc.install_probe_x64',
    [ValidateRange(1, 16)]
    [int]$Parallel = 2,
    [switch]$ConfigureOnly,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $QtRoot) {
    $QtRoot = Join-Path $taskRepo 'dependencies\qt5.15.2\msvc2019_64'
}
$taskQt = [IO.Path]::GetFullPath($QtRoot)
if (-not (Test-Path -LiteralPath (Join-Path $taskQt 'bin\qmake.exe'))) {
    throw "Qt SDK qmake.exe missing in $taskQt. See personal/docs/09-windows-build-check.md."
}
foreach ($taskDependency in @('include\portaudio.h', 'libx64\portaudio.lib', 'libx64\libvorbis.lib', 'libx64\libsndfile-1.lib', 'libx64\zlibstat.lib')) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskRepo ('dependencies\' + $taskDependency)))) {
        throw "Project dependency missing: $taskDependency. See personal/docs/09-windows-build-check.md."
    }
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake must be available in PATH.'
}
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVswhere)) {
    throw 'Visual Studio Installer/vswhere.exe is missing.'
}
if ($Generator -eq 'Visual Studio 16 2019') {
    $taskVsRange = '[16,17)'
    $taskToolset = 'v142'
}
else {
    $taskVsRange = '[17,18)'
    $taskToolset = 'v143'
}
$taskVsPath = & $taskVswhere -latest -products '*' -version $taskVsRange -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVsPath) {
    throw "$Generator C++ tools were not found (Build Tools editions are included)."
}

function Get-LocalOutputPath([string]$Directory) {
    $taskOutput = [IO.Path]::GetFullPath((Join-Path $taskRepo $Directory))
    if (-not $taskOutput.StartsWith($taskRepo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build/install output must be a subdirectory of this checkout.'
    }
    return $taskOutput
}
$taskBuild = Get-LocalOutputPath $BuildDirectory
$taskInstall = Get-LocalOutputPath $InstallDirectory
$taskCache = Join-Path $taskBuild 'CMakeCache.txt'
if (Test-Path -LiteralPath $taskCache) {
    $taskCacheGenerator = (Select-String -LiteralPath $taskCache -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$').Matches.Groups[1].Value
    if ($taskCacheGenerator -and $taskCacheGenerator -ne $Generator) {
        throw "Build directory already uses $taskCacheGenerator. Select a different -BuildDirectory for $Generator."
    }
}

function Invoke-TaskCmake([string[]]$Arguments) {
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake failed with exit code $LASTEXITCODE."
    }
}

$taskOldPath = $env:PATH
$taskOldClOptions = $env:_CL_
Push-Location -LiteralPath $taskRepo
try {
    $env:PATH = (Join-Path $taskQt 'bin') + ';' + $taskOldPath
    $env:_CL_ = ($taskOldClOptions + " /MP$Parallel").Trim()
    Write-Host "Qt SDK: $taskQt"
    Write-Host "Toolchain: $Generator / $taskToolset / x64"
    Write-Host "Build/install: $taskBuild / $taskInstall"
    $taskConfigureArgs = @(
        '-S', $taskRepo, '-B', $taskBuild, '-G', $Generator, '-A', 'x64', '-T', $taskToolset,
        "-DCMAKE_BUILD_TYPE=$Configuration", '-DMUSESCORE_BUILD_CONFIG=dev', '-DBUILD_64=ON',
        '-DBUILD_JACK=OFF', '-DDOWNLOAD_SOUNDFONT=OFF',
        "-DCMAKE_PREFIX_PATH=$taskQt", "-DCMAKE_INSTALL_PREFIX=$taskInstall"
    )
    Invoke-TaskCmake -Arguments $taskConfigureArgs
    if (-not $ConfigureOnly) {
        Invoke-TaskCmake -Arguments @('--build', $taskBuild, '--config', $Configuration, '--target', 'mscore', '--parallel', "$Parallel", '--', '/verbosity:minimal', '/nologo')
        if ($Install) {
            Invoke-TaskCmake -Arguments @('--install', $taskBuild, '--config', $Configuration)
        }
    }
}
finally {
    $env:PATH = $taskOldPath
    $env:_CL_ = $taskOldClOptions
    Pop-Location
}
