<#
.SYNOPSIS
  Prueba independiente de la distribucion: copia SOLO lo necesario a una carpeta nueva fuera del
  repositorio y ejecuta el EXE desde alli, con otro directorio de trabajo.

.DESCRIPTION
  Copia luchaLibreCompuG.exe, glew32.dll, assimp-vc140-mt.dll, Shaders\, Skybox\ y modelos\ a -Destino
  (por defecto %TEMP%\lucha_libre_distribucion). Despues ejecuta el EXE con --captura-todas (una imagen
  por cada una de las 8 vistas) lanzado desde el directorio del usuario y comprueba que:
    - el programa termina con codigo 0,
    - se generan las 8 capturas (en la carpeta del EXE, no en la de trabajo),
    - la salida no contiene errores de carga (shaders, skybox, modelos, texturas).
  Las capturas quedan en -Destino para revisarlas.

  Uso:
    powershell -ExecutionPolicy Bypass -File herramientas\probar_distribucion.ps1
    ... -Abrir        ademas deja la arena abierta para probar a mano las teclas T, Espacio, B, X, Z y 1-8
#>
param(
    [string]$Destino = (Join-Path $env:TEMP 'lucha_libre_distribucion'),
    [switch]$Abrir
)

$ErrorActionPreference = 'Stop'
$proyecto = Split-Path -Parent $PSScriptRoot
$repo     = Split-Path -Parent (Split-Path -Parent $proyecto)
if (-not (Test-Path (Join-Path $proyecto 'luchaLibreCompuG.exe'))) { throw 'Falta luchaLibreCompuG.exe: ejecuta primero construir_distribucion.ps1.' }
if ((Resolve-Path -LiteralPath (Split-Path -Parent $Destino) -ErrorAction SilentlyContinue).Path -like "$repo*") {
    throw 'El destino de la prueba debe estar fuera del repositorio.'
}

if (Test-Path $Destino) { Remove-Item $Destino -Recurse -Force }
New-Item -ItemType Directory $Destino | Out-Null
foreach ($carpeta in 'Shaders', 'Skybox', 'modelos') {
    robocopy (Join-Path $proyecto $carpeta) (Join-Path $Destino $carpeta) /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy fallo copiando $carpeta" }
}
foreach ($archivo in 'luchaLibreCompuG.exe', 'glew32.dll', 'assimp-vc140-mt.dll') {
    Copy-Item (Join-Path $proyecto $archivo) $Destino
}
$mb = [math]::Round((Get-ChildItem $Destino -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "== Distribucion copiada a $Destino ($mb MB)"

$exe = Join-Path $Destino 'luchaLibreCompuG.exe'
$log = Join-Path $Destino 'salida.log'
Write-Host '== Ejecutando --captura-todas desde otro directorio de trabajo (puede tardar ~30 s)'
$p = Start-Process $exe -ArgumentList '--captura-todas', 'vista' -WorkingDirectory $env:USERPROFILE `
     -RedirectStandardOutput $log -PassThru -NoNewWindow
$null = $p.Handle   # sin esto Windows PowerShell 5.1 no conserva ExitCode con -NoNewWindow
if (-not $p.WaitForExit(300000)) { $p.Kill(); throw 'El programa no termino en 5 minutos.' }

$errores = 0
if ($p.ExitCode -ne 0) { Write-Host "ERROR: codigo de salida $($p.ExitCode)" -ForegroundColor Red; $errores++ }
$capturas = @(Get-ChildItem $Destino -Filter 'vista_*.png')
if ($capturas.Count -ne 8) { Write-Host "ERROR: se esperaban 8 capturas y hay $($capturas.Count)" -ForegroundColor Red; $errores++ }
$fallos = @(Select-String -Path $log -Pattern 'ERROR|no se pudo|No se encontr|Error al' -CaseSensitive:$false)
if ($fallos.Count) { $fallos | ForEach-Object { Write-Host "ERROR en salida: $($_.Line)" -ForegroundColor Red }; $errores += $fallos.Count }
if ($errores) { throw "La prueba de distribucion fallo ($errores problema(s)). Revisa $log" }
Write-Host "Prueba correcta: 8 vistas renderizadas, sin errores de carga. Capturas en $Destino" -ForegroundColor Green

if ($Abrir) {
    Write-Host '== Abriendo la arena para prueba manual (Esc para cerrar)'
    Start-Process $exe -WorkingDirectory $env:USERPROFILE
}
