param(
    [ValidateSet('auto', 'cpu', 'cuda')][string]$Backend = 'auto',
    [int]$Port = 0,
    [ValidateRange(0,1000)][int]$DelayMs = 0
)
$ErrorActionPreference = 'Stop'
$pongRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$pongBuild = Join-Path $pongRoot 'build'
$pongExe = Join-Path $pongBuild 'sg_net_pong.exe'
if (!(Test-Path -LiteralPath $pongExe)) {
    & cmake -S $pongRoot -B $pongBuild -DCMAKE_BUILD_TYPE=Release -DSG_BUILD_EXAMPLES=ON -DSG_BUILD_GL=ON -DSG_BUILD_DSL=ON
    if ($LASTEXITCODE) { throw 'Pong configuration failed' }
}
& cmake --build $pongBuild --target sg_net_pong sg_net_keys -j 8
if ($LASTEXITCODE) { throw 'Pong build failed' }
if ($Port -eq 0) {
    foreach ($pongCandidate in 49270..49900) {
        $pongSockets = @()
        try {
            $pongSockets += [Net.Sockets.UdpClient]::new($pongCandidate)
            $pongSockets += [Net.Sockets.UdpClient]::new($pongCandidate+1)
            $Port = $pongCandidate
            break
        } catch { } finally { foreach ($pongSocket in $pongSockets) { $pongSocket.Dispose() } }
    }
    if ($Port -eq 0) { throw 'No free local Pong port pair' }
}
$pongOut = Join-Path $pongBuild 'out/pong/play'
New-Item -ItemType Directory -Force -Path $pongOut | Out-Null
$pongCredentials = Join-Path $pongOut ("credentials-"+[Guid]::NewGuid().ToString('N'))
& (Join-Path $pongBuild 'sg_net_keys.exe') $pongCredentials 2 2 0 pong
if ($LASTEXITCODE) { throw 'Pong credentials failed' }
$pongProcesses = @()
foreach ($pongPlayer in 0,1) {
    $pongArgs = @('--player', $pongPlayer, '--backend', $Backend, '--port', $Port, '--credentials', ('"'+$pongCredentials+'"'), '--delay-ms', $DelayMs)
    $pongProcesses += Start-Process -FilePath $pongExe -ArgumentList $pongArgs -WorkingDirectory $pongBuild -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $pongOut "p$pongPlayer.log") -RedirectStandardError (Join-Path $pongOut "p$pongPlayer.err")
}
Start-Sleep -Milliseconds 1000
foreach ($pongProcess in $pongProcesses) { if ($pongProcess.HasExited) { throw "Pong exited. See $pongOut" } }
Write-Host "Two equal Pong peers on localhost:$Port and $($Port+1). W/S = left, arrows = right. Focus either window. Esc closes that window."
