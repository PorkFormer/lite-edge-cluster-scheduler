param(
    [string]$GrpcVersion = 'v1.60.0'
)

$ErrorActionPreference = 'Stop'

$deps = @(
    @{Url='https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip'; Target='googletest-src'},
    @{Url='https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.0.zip'; Target='httplib-src'},
    @{Url='https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip'; Target='json-src'},
    @{Url='https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.zip'; Target='spdlog-src'},
    @{Url="https://github.com/grpc/grpc/archive/refs/tags/$GrpcVersion.zip"; Target='grpc-src'},
    @{Url='https://github.com/boostorg/uuid/archive/refs/tags/boost-1.86.0.zip'; Target='boost-uuid-src'},
    @{Url='https://github.com/boostorg/assert/archive/refs/tags/boost-1.86.0.zip'; Target='boost-assert-src'},
    @{Url='https://github.com/boostorg/config/archive/refs/tags/boost-1.86.0.zip'; Target='boost-config-src'},
    @{Url='https://github.com/boostorg/throw_exception/archive/refs/tags/boost-1.86.0.zip'; Target='boost-throw-src'},
    @{Url='https://github.com/boostorg/type_traits/archive/refs/tags/boost-1.86.0.zip'; Target='boost-traits-src'},
    @{Url='https://github.com/boostorg/static_assert/archive/refs/tags/boost-1.86.0.zip'; Target='boost-static-src'}
)

$depsRoot = Join-Path $PSScriptRoot '..\3rdparty'
$depsRoot = Resolve-Path $depsRoot
New-Item -ItemType Directory -Force -Path $depsRoot | Out-Null

function Get-TopLevelDir([string]$zipPath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $entry = $zip.Entries | Where-Object { $_.FullName -notmatch '^__MACOSX/' } | Select-Object -First 1
        if ($null -eq $entry) { return $null }
        $parts = $entry.FullName -split '/'
        return $parts[0]
    } finally {
        $zip.Dispose()
    }
}

foreach ($dep in $deps) {
    $targetPath = Join-Path $depsRoot $dep.Target
    $needDownload = $true
    if (Test-Path $targetPath) {
        $hasContent = Get-ChildItem -Force -Path $targetPath -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hasContent) {
            Write-Host "[skip] $($dep.Target) already exists"
            $needDownload = $false
        } else {
            Remove-Item -Force -Recurse $targetPath
        }
    }
    if (-not $needDownload) { continue }

    $zipPath = Join-Path $depsRoot ($dep.Target + '.zip')
    if (Test-Path $zipPath) {
        $zipPath = Join-Path $depsRoot ($dep.Target + '-' + [Guid]::NewGuid().ToString() + '.zip')
    }
    Write-Host "[download] $($dep.Url) -> $zipPath"
    Invoke-WebRequest -Uri $dep.Url -OutFile $zipPath

    $topDir = Get-TopLevelDir $zipPath
    if (-not $topDir) {
        throw "Failed to detect top-level directory in $zipPath"
    }

    Write-Host "[extract] $zipPath"
    Expand-Archive -Force -Path $zipPath -DestinationPath $depsRoot
    $extractedPath = Join-Path $depsRoot $topDir

    if (-not (Test-Path $extractedPath)) {
        throw "Expected extracted directory not found: $extractedPath"
    }

    Move-Item -Force -Path $extractedPath -Destination $targetPath
    Remove-Item -Force $zipPath
}

Write-Host "Done."
