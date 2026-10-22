# List COS buckets (name + region) for the account in cos.env. Secrets are never printed.
param([string]$EnvFile = "C:\Users\asuperj\Desktop\密钥\cos.env")

$cfg = @{}
Get-Content -Path $EnvFile -Encoding UTF8 | ForEach-Object {
    if ($_ -match '^\s*([A-Z_]+)\s*=(.*)$') { $cfg[$matches[1]] = $matches[2].Trim() }
}

$sid = $cfg['COS_SECRET_ID']
$skey = $cfg['COS_SECRET_KEY']
if (-not $sid -or -not $skey) { throw "missing COS_SECRET_ID / COS_SECRET_KEY" }

function HmacSha1Hex([byte[]]$key, [string]$data) {
    $h = New-Object System.Security.Cryptography.HMACSHA1
    $h.Key = $key
    (($h.ComputeHash([Text.Encoding]::UTF8.GetBytes($data)) | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Sha1Hex([string]$data) {
    $s = [System.Security.Cryptography.SHA1]::Create()
    (($s.ComputeHash([Text.Encoding]::UTF8.GetBytes($data)) | ForEach-Object { $_.ToString('x2') }) -join '')
}

$start = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$end = $start + 600
$keyTime = "$start;$end"

$signKey = HmacSha1Hex ([Text.Encoding]::UTF8.GetBytes($skey)) $keyTime
$cosHost = 'service.cos.myqcloud.com'
$httpString = "get`n/`n`nhost=$cosHost`n"
$stringToSign = "sha1`n$keyTime`n" + (Sha1Hex $httpString) + "`n"
$signature = HmacSha1Hex ([Text.Encoding]::UTF8.GetBytes($signKey)) $stringToSign

$auth = "q-sign-algorithm=sha1&q-ak=$sid&q-sign-time=$keyTime&q-key-time=$keyTime" +
        "&q-header-list=host&q-url-param-list=&q-signature=$signature"

$raw = curl.exe --noproxy "*" -s -H "Authorization: $auth" "https://$cosHost/"

if ($raw -match '<Error>') {
    $code = if ($raw -match '<Code>(.*?)</Code>') { $matches[1] } else { 'unknown' }
    $msg = if ($raw -match '<Message>(.*?)</Message>') { $matches[1] } else { '' }
    Write-Output ("AUTH_FAILED code={0} message={1}" -f $code, $msg)
    exit 1
}

$found = [regex]::Matches($raw, '<Name>(.*?)</Name>\s*<Location>(.*?)</Location>')
if ($found.Count -eq 0) {
    Write-Output "NO_BUCKET"
    exit 0
}

foreach ($m in $found) {
    Write-Output ("BUCKET {0} {1}" -f $m.Groups[1].Value, $m.Groups[2].Value)
}
