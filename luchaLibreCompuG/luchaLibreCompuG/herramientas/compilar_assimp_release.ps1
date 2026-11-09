<#
.SYNOPSIS
  Compila Assimp 4.1.0 (codigo fuente oficial) como DLL Release x86 y la copia junto al EXE.

.DESCRIPTION
  La assimp-vc140-mt.dll original del repositorio es una compilacion DEBUG (depende de MSVCP140D,
  VCRUNTIME140D y ucrtbased, que solo existen con Visual Studio). Este script genera la equivalente
  Release, con la MISMA version (4.1.0) que los encabezados de External Libraries\assimp\include:
    1. Descarga https://github.com/assimp/assimp/archive/refs/tags/v4.1.0.tar.gz
    2. Verifica el SHA-256 del archivo y que su commit sea 80799bdbf90ce626475635815ee18537718a05b1
       (el del tag v4.1.0 de github.com/assimp/assimp).
    3. Cambia SOLO una linea del CMakeLists.txt de contrib\zlib (cmake_policy CMP0048 OLD -> NEW),
       porque CMake 4.x ya no admite la politica OLD. No se modifica codigo de la biblioteca.
    4. Configura con CMake + Ninja (x86, Release, DLL, zlib estatico incluido) y compila con el
       toolset de Visual Studio, en una carpeta temporal FUERA del repositorio.
    5. Comprueba con dumpbin que la DLL es x86, que no depende de runtimes de depuracion y que
       exporta los mismos simbolos que la DLL original (por eso la assimp-vc140-mt.lib de
       External Libraries sigue siendo valida y no se reemplaza).
    6. La copia a luchaLibreCompuG\luchaLibreCompuG\assimp-vc140-mt.dll.
  La DLL Debug original queda intacta en External Libraries\assimp\lib\ (la usa la configuracion Debug).

  Uso:  powershell -ExecutionPolicy Bypass -File herramientas\compilar_assimp_release.ps1
        (-Trabajo <carpeta> cambia la carpeta temporal; por defecto %TEMP%\assimp_4.1.0_release)
#>
param([string]$Trabajo = (Join-Path $env:TEMP 'assimp_4.1.0_release'))

$ErrorActionPreference = 'Stop'
$proyecto = Split-Path -Parent $PSScriptRoot
$solucion = Split-Path -Parent $proyecto
$url      = 'https://github.com/assimp/assimp/archive/refs/tags/v4.1.0.tar.gz'
$sha256   = '3520b1e9793b93a2ca3b797199e16f40d61762617e072f2d525fad70f9678a71'
$commit   = '80799bdbf90ce626475635815ee18537718a05b1'

if ((Resolve-Path -LiteralPath (Split-Path -Parent $Trabajo) -ErrorAction Stop).Path -like "$solucion*") { throw 'La carpeta de trabajo debe estar fuera del repositorio.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -prerelease -products * -property installationPath
if (-not $vs) { throw 'No se encontro Visual Studio.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$dumpbin = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe | Where-Object FullName -like '*Hostx64*x86*' | Select-Object -First 1 -ExpandProperty FullName

if (Test-Path $Trabajo) { Remove-Item $Trabajo -Recurse -Force }
New-Item -ItemType Directory $Trabajo | Out-Null
$tgz = Join-Path $Trabajo 'assimp-4.1.0.tar.gz'
Write-Host "== Descargando $url"
Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $tgz

# ---- Procedencia
$hash = (Get-FileHash $tgz -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $sha256) { throw "SHA-256 inesperado ($hash). No se compila." }
$tar = Join-Path $Trabajo 'assimp-4.1.0.tar'
$in = [IO.File]::OpenRead($tgz); $gz = New-Object IO.Compression.GZipStream($in, [IO.Compression.CompressionMode]::Decompress)
$out = [IO.File]::Create($tar); $gz.CopyTo($out); $out.Close(); $gz.Close(); $in.Close()
$idCommit = (cmd /c "git get-tar-commit-id < `"$tar`"").Trim()
if ($idCommit -ne $commit) { throw "Commit inesperado ($idCommit). No se compila." }
Write-Host "   SHA-256 y commit $commit verificados (tag v4.1.0)"

tar -xf $tar -C $Trabajo
$fuente = Join-Path $Trabajo 'assimp-4.1.0'
$cm = Get-Content (Join-Path $fuente 'CMakeLists.txt') -Raw
if ($cm -notmatch 'ASSIMP_VERSION_MAJOR 4\)\s*SET \(ASSIMP_VERSION_MINOR 1\)\s*SET \(ASSIMP_VERSION_PATCH 0\)') { throw 'El CMakeLists.txt no declara la version 4.1.0.' }
$zl = Join-Path $fuente 'contrib\zlib\CMakeLists.txt'
(Get-Content $zl -Raw).Replace('cmake_policy(SET CMP0048 OLD)', 'cmake_policy(SET CMP0048 NEW)') | Set-Content $zl -NoNewline

# ---- Compilacion x86 Release
$bat = Join-Path $Trabajo 'compilar.bat'
@"
@echo off
set "PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%"
call "$vcvars" x86 || exit /b 1
set "PATH=$ninja;%PATH%"
"$cmake" -S "$fuente" -B "$Trabajo\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF -DASSIMP_WERROR=OFF -DASSIMP_BUILD_ZLIB=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 || exit /b 2
"$cmake" --build "$Trabajo\build" -j 8 || exit /b 3
"@ | Set-Content $bat -Encoding ASCII
Write-Host '== Compilando (unos minutos)'
$registro = Join-Path $Trabajo 'compilacion.log'
cmd /c "`"$bat`" > `"$registro`" 2>&1"   # la redireccion la hace cmd: CMake escribe avisos por stderr
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion (codigo $LASTEXITCODE). Revisa $Trabajo\compilacion.log" }
$dll = Join-Path $Trabajo 'build\code\assimp-vc140-mt.dll'
if (-not (Test-Path $dll)) { throw "No se genero $dll" }

# ---- Verificaciones
$cab = & $dumpbin /headers $dll | Out-String
if ($cab -notmatch '14C machine \(x86\)') { throw 'La DLL no es x86.' }
$deps = (& $dumpbin /dependents $dll | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
Write-Host "   Dependencias: $($deps -join ', ')"
if ($deps | Where-Object { $_ -match '(?i)(msvcp\d+d|vcruntime\d+d|ucrtbased)\.dll' }) { throw 'La DLL depende de runtimes de depuracion.' }
function Exportes($f) { & $dumpbin /exports $f | Where-Object { $_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+\S+' } | ForEach-Object { ($_ -split '\s+')[4] } | Sort-Object }
$orig = Join-Path $solucion 'External Libraries\assimp\lib\assimp-vc140-mt.dll'
$dif = Compare-Object (Exportes $orig) (Exportes $dll)
if ($dif) { throw "Las exportaciones difieren de la DLL original ($($dif.Count)): la .lib actual no sirve, hay que reemplazarla." }
Write-Host '   Exportaciones identicas a la DLL original: la assimp-vc140-mt.lib existente sigue siendo valida.'

Copy-Item $dll (Join-Path $proyecto 'assimp-vc140-mt.dll') -Force
Write-Host "== Copiada a $proyecto\assimp-vc140-mt.dll ($([math]::Round((Get-Item $dll).Length/1KB)) KB)" -ForegroundColor Green
Write-Host "   Puedes borrar $Trabajo"
