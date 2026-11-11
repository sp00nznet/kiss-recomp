# One-click setup for the KISS Psycho Circus: The Nightmare Child static recompilation.
# Run it by double-clicking Setup.cmd in the repo folder; the README's "Step by
# step" is the same thing by hand, command for command.
#
# It checks the tools, links your installed and patched (1.13) copy of the
# game as game\, relocates and catalogs every module, lifts them to C, builds
# build\kiss.exe and leaves a shortcut to it in this folder. Each step is
# skipped when its output already exists (-Force redoes them), so a rerun after
# a failure resumes. Everything it does goes to setup.log.
#
#   -GameDir <folder with client.exe and KPCPatch.rez>  skip the question (-Game works too)
#   -Yes                                                answer yes to every install question
#
# The toolkit is ..\pcrecomp on its pcrecomp-lithtech2 branch (PCRECOMP
# overrides it). Under recomp-launcher (RECOMP_LAUNCHER=1) it never waits for
# Enter, and exit code 0 means the shortcut is there.
param([switch]$Force, [switch]$Yes, [Alias('Game')][string]$GameDir = "")
$Game = $GameDir

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Toolkit = $env:PCRECOMP
if (-not $Toolkit) {
  $Toolkit = Join-Path (Split-Path -Parent $Root) 'pcrecomp'
}
$env:PCRECOMP = $Toolkit   # the Python steps read it too
$Log = Join-Path $Root 'setup.log'
Set-Location $Root
function Log($t) { Add-Content -Path $Log -Value $t -Encoding UTF8 }
Log "==== setup $(Get-Date -Format s)"

function Say($t, $c = 'Gray') { Write-Host $t -ForegroundColor $c; Log $t }
function Step($n, $t) { Write-Host ""; Say "[$n/6] $t" 'Cyan' }
function Pause-End { if (-not $Yes -and $env:RECOMP_LAUNCHER -ne '1') { Read-Host "Press Enter to close" | Out-Null } }
function Fail($t) {
  Say ""; Say "Setup stopped: $t" 'Red'
  Say "The details are in $Log. Fix that and run Setup.cmd again; finished steps are skipped." 'Yellow'
  Pause-End; exit 1
}
trap { Fail "$_" }
function Ask($q) { if ($Yes) { Say "$q yes"; return $true }; $a = Read-Host "$q [Y/n]"; return -not ($a -match '^[nN]') }
function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}
# Run a program with its output in the log. Windows PowerShell turns a native
# program's stderr into errors, so 'Stop' is off while it runs.
function Exec([string[]]$cmd) {
  $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $rest = @($cmd | Select-Object -Skip 1)   # @(): a one-element slice would splat as characters
  & $cmd[0] @rest 2>&1 | ForEach-Object { Log "$_" }
  $code = $LASTEXITCODE
  $ErrorActionPreference = $old
  return $code
}
function Run($what, [string[]]$cmd) {
  Say "  $what..."
  $code = Exec $cmd
  if ($code -ne 0) { Fail "$what failed (exit code $code)." }
}

if ($env:RECOMP_LAUNCHER -ne '1') { Clear-Host }
Say "KISS Psycho Circus: The Nightmare Child: static recompilation setup" 'White'
Say "You need your own copy of KISS Psycho Circus, patched to 1.13 (see the README), and about 3 GB free."

# ---------------------------------------------------------------- tools
Step 1 "Checking the tools"
# A Python that answers "Python 3.x". The Store's placeholder "python" (the
# one that opens the Store) answers nothing, so asking is the reliable test.
function Find-Python {
  # The per-user install winget makes, before PATH has it.
  $user = @(Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | ForEach-Object { ,@($_.FullName) })
  foreach ($c in (@(@('py', '-3'), @('python'), @('python3')) + $user)) {
    if (-not (Get-Command $c[0] -ErrorAction SilentlyContinue)) { continue }
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $rest = @($c | Select-Object -Skip 1)
    $v = (& $c[0] @rest --version 2>&1 | Out-String).Trim()
    $ErrorActionPreference = $old
    if ($v -match '^Python 3\.(\d+)' -and [int]$Matches[1] -ge 10) { return ,$c }
  }
  return $null
}
$pyargs = Find-Python
if (-not $pyargs) {
  Say "  Python 3.10 or newer is not installed."
  if (-not (Ask "  Install Python 3.12 now (winget, for your user only, about 30 MB)?")) { Fail "Python 3 is required." }
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail "winget is missing. Install 'App Installer' from the Microsoft Store, or install Python yourself."
  }
  Exec @('winget', 'install', '-e', '--id', 'Python.Python.3.12', '--scope', 'user', '--accept-package-agreements', '--accept-source-agreements') | Out-Null
  Refresh-Path
  $pyargs = Find-Python
  if (-not $pyargs) { Fail "Python installed, but it can't be found. Install Python 3.12 from python.org yourself." }
}
Say "  Python: $($pyargs -join ' ')"

if ((Exec ($pyargs + @('-c', 'import pefile, capstone'))) -ne 0) {
  Say "  The Python packages pefile and capstone are missing (about 20 MB)."
  if (-not (Ask "  Install them now (pip, for your user only)?")) { Fail "pefile and capstone are required." }
  Run "Installing pefile and capstone" ($pyargs + @('-m', 'pip', 'install', '--user', 'pefile', 'capstone'))
}

if (-not (Test-Path (Join-Path $Toolkit 'tools\lift\lift32.py'))) {
  Say "  The pcrecomp toolkit is not beside this folder ($Toolkit)."
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    if (-not (Ask "  git is needed to fetch it. Install Git for Windows now (winget, about 60 MB)?")) { Fail "git is needed to fetch pcrecomp." }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install Git for Windows yourself." }
    Exec @('winget', 'install', '-e', '--id', 'Git.Git', '--accept-package-agreements', '--accept-source-agreements') | Out-Null
    Refresh-Path
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "Git did not install. Install Git for Windows yourself." }
  }
  if (-not (Ask "  Download it now (git, about 20 MB)?")) { Fail "pcrecomp is required at $Toolkit." }
  Run "Cloning pcrecomp" @('git', 'clone', '--depth', '1', '-b', 'pcrecomp-lithtech2', 'https://github.com/sp00nznet/pcrecomp', $Toolkit)
}
# The toolkit changes this game needs (on pcrecomp-lithtech2, not yet in
# main), each found by a line it adds.
$need = @(@('the DirectInput HINSTANCE shim', 'runtime\native32\native32.c', 'dinput_create'),
          @('the EnumDevices split', 'runtime\native32\native32.c', 'di_enum_split'),
          @('the headless desktop', 'runtime\native32\native32.c', 'native32_headless'),
          @('the loader reservation check', 'runtime\recomp32\image_loader.c', 'native32_hold_image'))
function Missing { @($need | Where-Object {
  $f = Join-Path $Toolkit $_[1]
  -not ((Test-Path $f) -and (Select-String -Path $f -Pattern $_[2] -SimpleMatch -Quiet)) } | ForEach-Object { $_[0] }) }
$missing = Missing
if ($missing) {
  Fail "your pcrecomp at $Toolkit is missing $($missing -join ', '). Check out its pcrecomp-lithtech2 branch (git -C `"$Toolkit`" checkout pcrecomp-lithtech2), or point PCRECOMP at a checkout that has it."
}
Say "  pcrecomp: $Toolkit"

# Visual Studio 2022 or its Build Tools, with the C++ x86 compiler; CMake and
# Ninja ship inside that workload, so build.cmd needs nothing else.
function Find-VS {
  foreach ($p in @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
                   "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe")) {
    if (Test-Path $p) {
      $vs = & $p -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
      if ($vs) { return $vs }
    }
  }
  foreach ($d in (Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio\2022", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022" -Directory -ErrorAction SilentlyContinue)) {
    if (Test-Path "$($d.FullName)\VC\Auxiliary\Build\vcvarsall.bat") { return $d.FullName }
  }
  return $null
}
if (-not (Find-VS)) {
  Say "  Visual Studio 2022 with the C++ tools is not installed. The recompiled game is C, and this is the compiler."
  if (-not (Ask "  Install the Visual Studio 2022 Build Tools with the C++ workload now (winget, about 3 GB)?")) { Fail "a C compiler (Visual Studio 2022 C++) is required." }
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "winget is missing. Install Visual Studio 2022 Build Tools with 'Desktop development with C++' yourself." }
  Exec @('winget', 'install', '-e', '--id', 'Microsoft.VisualStudio.2022.BuildTools', '--accept-package-agreements', '--accept-source-agreements',
         '--override', '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended') | Out-Null
  if (-not (Find-VS)) { Fail "the Build Tools did not install. Install Visual Studio 2022 with 'Desktop development with C++' yourself." }
}
Say "  C compiler: $(Find-VS)"

# ---------------------------------------------------------------- the game
# The game is linked, not copied: game\ is a junction to your install.
Step 2 "Linking your copy of the game as game\"
function Is-Kiss($d) { (Test-Path (Join-Path $d 'client.exe')) -and (Test-Path (Join-Path $d 'KPCPatch.rez')) }
if ((Is-Kiss 'game') -and -not $Force) {
  Say "  Already there (skipping)."
} else {
  $Game = $Game.Trim('"', ' ')
  if (-not $Game) {
    foreach ($d in @('C:\Games\Psycho', "${env:ProgramFiles(x86)}\Gathering of Developers\Psycho", "$env:ProgramFiles\Gathering of Developers\Psycho", 'C:\Psycho')) {
      if (Is-Kiss $d) { $Game = $d; Say "  Found the game in $Game"; break }
    }
  }
  if (-not $Game -and $Yes) { Fail "KISS Psycho Circus 1.13 wasn't found. Give its folder with -GameDir." }
  while (-not $Game) {
    $Game = (Read-Host "  Where is KISS Psycho Circus? The folder with client.exe and KPCPatch.rez (patched to 1.13)").Trim('"', ' ')
  }
  if (-not (Is-Kiss $Game)) {
    Fail "$Game has no client.exe and KPCPatch.rez. Copy the CD's Psycho folder and apply the 1.1 and 1.13 patches first (docs/bringup.md)."
  }
  if (Test-Path 'game') { Run "Removing the old game link" @('cmd', '/d', '/c', 'rmdir', 'game') }
  Run "Linking game -> $Game" @('cmd', '/d', '/c', 'mklink', '/J', 'game', $Game)
}

# ---------------------------------------------------------------- rebase
Step 3 "Relocating the modules (a few seconds)"
if ((Test-Path 'work\rebased\cshell.dll') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Relocating" ($pyargs + @('tools\rebase.py')) }

# ---------------------------------------------------------------- catalog
Step 4 "Finding every function (a few minutes)"
if ((Test-Path 'work\cshell.dll.functions.json') -and -not $Force) { Say "  Already done (skipping)." }
else { Run "Disassembling 8 modules" ($pyargs + @('tools\catalog.py')) }

# ---------------------------------------------------------------- lift
Step 5 "Recompiling to C, then building build\kiss.exe (several minutes)"
if ((Test-Path 'src\recomp\gen\recomp_dispatch.c') -and -not $Force) { Say "  Lift already done (skipping)." }
else { Run "Lifting" ($pyargs + @('run_lift.py')) }
if ((Test-Path 'build\kiss.exe') -and -not $Force) { Say "  Already built (skipping)." }
else {
  $env:CMAKE_ARGS = "-DPCRECOMP=$($Toolkit -replace '\\', '/')"
  Run "Compiling" @('cmd', '/d', '/c', "`"$Root\build.cmd`"")
}

# ---------------------------------------------------------------- shortcut
Step 6 "Making the shortcut"
$lnk = Join-Path $Root 'KISS Psycho Circus.lnk'
$sh = New-Object -ComObject WScript.Shell
$s = $sh.CreateShortcut($lnk)
$s.TargetPath = Join-Path $Root 'build\kiss.exe'
$s.Arguments = '--run -- +windowed 1 +RGBRast 1'
$s.WorkingDirectory = $Root
$s.IconLocation = (Join-Path $Root 'game\psycho.exe') + ',0'
$s.Save()
Say "  $lnk"

if (-not (Test-Path $lnk)) { Fail "the shortcut was not made." }
Write-Host ""
Say "Done. Double-click 'KISS Psycho Circus' in this folder to play." 'Green'
Pause-End
exit 0
