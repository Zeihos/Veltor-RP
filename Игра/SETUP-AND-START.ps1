# VELTOR ROLEPLAY local test installer (Windows PowerShell 5.1+)
# Downloads official open.mp components, compiles Pawn and runs local server.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$server = Join-Path $root 'server'
$cache = Join-Path $root '.download-cache'
$tools = Join-Path $server 'TOOLS\Pawn'
$compiler = Join-Path $tools 'pawncc.exe'
$exe = Join-Path $server 'omp-server.exe'
$source = Join-Path $root 'gamemodes\veltor.pwn'
$amx = Join-Path $server 'gamemodes\veltor.amx'
$serverUrl = 'https://github.com/openmultiplayer/open.mp/releases/download/v1.6.7.3218/open.mp-win-x86.zip'
$compilerUrl = 'https://github.com/openmultiplayer/compiler/releases/download/v3.10.11/pawnc-3.10.11-windows.zip'
function Get-Zip([string]$url, [string]$dest) {
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing -MaximumRedirection 5
    $f=[IO.File]::OpenRead($dest)
    try {
        if($f.Length -lt 30000 -or $f.ReadByte() -ne 80 -or $f.ReadByte() -ne 75) { throw 'Invalid ZIP download' }
    } finally { $f.Dispose() }
}
function Copy-Contents([string]$from, [string]$dest) {
    New-Item -ItemType Directory -Force $dest | Out-Null
    Get-ChildItem -LiteralPath $from -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dest -Recurse -Force }
}
try {
    if(!(Test-Path $source)){ throw 'Cannot locate gamemodes\veltor.pwn' }
    New-Item -ItemType Directory -Force $server,$cache,$tools | Out-Null
    if(!(Test-Path $exe)) {
        Get-Zip $serverUrl (Join-Path $cache 'server.zip')
        Expand-Archive (Join-Path $cache 'server.zip') (Join-Path $cache 'server') -Force
        $binary=Get-ChildItem (Join-Path $cache 'server') -Recurse -Filter omp-server.exe -File | Select-Object -First 1
        if(!$binary){ throw 'Cannot locate omp-server.exe in official ZIP' }
        Copy-Contents (Split-Path $binary.FullName) $server
    }
    if(!(Test-Path $compiler)) {
        Get-Zip $compilerUrl (Join-Path $cache 'compiler.zip')
        Expand-Archive (Join-Path $cache 'compiler.zip') (Join-Path $cache 'compiler') -Force
        $binary=Get-ChildItem (Join-Path $cache 'compiler') -Recurse -Filter pawncc.exe -File | Select-Object -First 1
        if(!$binary){ throw 'Cannot locate pawncc.exe in official ZIP' }
        Copy-Contents (Split-Path $binary.FullName) $tools
    }
    $inc=Get-ChildItem $server -Recurse -Filter 'open.mp.inc' -File | Where-Object { $_.FullName -notlike '*TOOLS\Pawn*' } | Select-Object -First 1
    if(!$inc){ throw 'open.mp.inc missing from downloaded open.mp package' }
    $incPath=Split-Path $inc.FullName
    New-Item -ItemType Directory -Force (Join-Path $server 'gamemodes'),(Join-Path $server 'scriptfiles') | Out-Null
    $log=Join-Path $root 'BUILD-LOG.txt'
    & $compiler $source "-i$incPath" "-o$amx" '-Z' 2>&1 | Tee-Object -FilePath $log
    $code=$LASTEXITCODE
    if($code -ne 0 -or !(Test-Path $amx)){throw "Pawn build failed (exit $code). Open BUILD-LOG.txt."}
    $config = @{
      name='VELTOR ROLEPLAY | LOCAL TEST'
      language='Russian'
      announce=$false
      website='https://vk.ru/veltor_rp'
      max_players=30
      network=@{port=7777;allow_037_clients=$true}
      pawn=@{main_scripts=@('veltor 1');side_scripts=@();legacy_plugins=@()}
      rcon=@{enable=$false;password=[Guid]::NewGuid().ToString('N')}
      logging=@{enable=$true}
    }
    [IO.File]::WriteAllText((Join-Path $server 'config.json'),(ConvertTo-Json -InputObject $config -Depth 12),([System.Text.UTF8Encoding]::new($false)))
    Write-Host 'VELTOR: compiled. Local connect address: 127.0.0.1:7777' -ForegroundColor Green
    Push-Location $server
    try { & $exe } finally { Pop-Location }
    exit $LASTEXITCODE
} catch {
    Write-Host ('VELTOR setup error: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
