<#
.SYNOPSIS
  Reconstruye el ejecutable Release | Win32 y lo copia junto a los recursos (distribucion).

.DESCRIPTION
  1. Compila luchaLibreCompuG.vcxproj en Release | Win32 con MSBuild (SolutionDir explicito).
  2. Copia Release\luchaLibreCompuG.exe a luchaLibreCompuG\luchaLibreCompuG\luchaLibreCompuG.exe,
     la carpeta que ya contiene Shaders\, Skybox\, modelos\ y las DLL.
  3. Verifica con dumpbin que el EXE y las DLL no dependen de los runtimes de DEPURACION
     (MSVCP140D, VCRUNTIME140D, ucrtbased...), que solo existen en equipos con Visual Studio,
     y que cada DLL que no es del sistema esta junto al EXE.

  Uso (desde cualquier carpeta):
    powershell -ExecutionPolicy Bypass -File luchaLibreCompuG\luchaLibreCompuG\herramientas\construir_distribucion.ps1
    ... -SinCompilar     solo copia y verifica (usa el Release\ ya compilado)
#>
param([switch]$SinCompilar)

$ErrorActionPreference = 'Stop'
$proyecto = Split-Path -Parent $PSScriptRoot            # ...\luchaLibreCompuG\luchaLibreCompuG
$solucion = Split-Path -Parent $proyecto                # ...\luchaLibreCompuG (SolutionDir)
$vcxproj  = Join-Path $proyecto 'luchaLibreCompuG.vcxproj'
$exeBuild = Join-Path $solucion 'Release\luchaLibreCompuG.exe'
$exeFinal = Join-Path $proyecto 'luchaLibreCompuG.exe'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'No se encontro vswhere.exe: instala Visual Studio con la carga de trabajo de C++.' }
$vsRaiz = & $vswhere -latest -prerelease -products * -property installationPath
if (-not $vsRaiz) { throw 'No se encontro ninguna instalacion de Visual Studio.' }

if (-not $SinCompilar) {
    $msbuild = Join-Path $vsRaiz 'MSBuild\Current\Bin\MSBuild.exe'
    Write-Host "== Compilando Release | Win32 con $msbuild"
    & $msbuild $vcxproj -p:Configuration=Release -p:Platform=Win32 "-p:SolutionDir=$solucion\" -m -nologo -v:minimal
    if ($LASTEXITCODE -ne 0) { throw "MSBuild fallo (codigo $LASTEXITCODE)." }
}
if (-not (Test-Path $exeBuild)) { throw "No existe $exeBuild" }

Copy-Item $exeBuild $exeFinal -Force
Write-Host "== Copiado: $exeFinal ($([math]::Round((Get-Item $exeFinal).Length/1KB)) KB)"

# ---- Verificacion de dependencias
$dumpbin = Get-ChildItem (Join-Path $vsRaiz 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
           Where-Object FullName -like '*Hostx64*x86*' | Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) { Write-Warning 'dumpbin.exe no encontrado: se omite la verificacion de dependencias.'; return }

$depuracion = '(?i)^(msvcp\d+d|vcruntime\d+d|ucrtbased|msvcr\d+d|concrtd?\d*d|vcomp\d+d)\.dll$'
$sistema    = '(?i)^(kernel32|user32|gdi32|shell32|opengl32|advapi32|ole32|oleaut32|shlwapi|ws2_32|comdlg32|winmm|imm32|version|setupapi|dwmapi|msvcp140|vcruntime140|vcruntime140_1|api-ms-win-.*)\.dll$'
$errores = 0
foreach ($archivo in @($exeFinal) + (Get-ChildItem $proyecto -Filter *.dll | ForEach-Object FullName)) {
    $deps = & $dumpbin /dependents $archivo | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() }
    Write-Host "`n-- $(Split-Path $archivo -Leaf): $($deps -join ', ')"
    foreach ($d in $deps) {
        if ($d -match $depuracion) { Write-Host "   ERROR: depende del runtime de DEPURACION $d (no existe en equipos sin Visual Studio)" -ForegroundColor Red; $errores++ }
        elseif ($d -notmatch $sistema -and -not (Test-Path (Join-Path $proyecto $d))) { Write-Host "   ERROR: falta $d junto al EXE" -ForegroundColor Red; $errores++ }
    }
}
Write-Host "`nRequisito en el equipo de destino: Microsoft Visual C++ Redistributable 2015-2022 (x86), que aporta MSVCP140.dll y VCRUNTIME140.dll."
if ($errores -gt 0) { throw "$errores problema(s) de dependencias: la distribucion NO es portable todavia." }
Write-Host 'Dependencias correctas.' -ForegroundColor Green
