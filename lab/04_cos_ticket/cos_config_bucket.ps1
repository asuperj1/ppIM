# Configure COS bucket: ACL public-read + CORS for pp.xiaojietong.online.
# Reads credentials from cos.env; never prints them.
param(
    [string]$EnvFile = "C:\Users\asuperj\Desktop\密钥\cos.env",
    [string]$Origin = "https://pp.xiaojietong.online"
)

$cfg = @{}
Get-Content -Path $EnvFile -Encoding UTF8 | ForEach-Object {
    if ($_ -match '^\s*([A-Z_]+)\s*=(.*)$') { $cfg[$matches[1]] = $matches[2].Trim() }
}

$sid = $cfg['COS_SECRET_ID']
$skey = $cfg['COS_SECRET_KEY']
$bucket = $cfg['COS_BUCKET']
$region = $cfg['COS_REGION']
if (-not $sid -or -not $skey -or -not $bucket -or -not $region) { throw "cos.env incomplete" }

$cosHost = "$bucket.cos.$region.myqcloud.com"

function HmacSha1Hex([byte[]]$key, [string]$data) {
    $h = New-Object System.Security.Cryptography.HMACSHA1
    $h.Key = $key
    (($h.ComputeHash([Text.Encoding]::UTF8.GetBytes($data)) | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Sha1Hex([string]$data) {
    $s = [System.Security.Cryptography.SHA1]::Create()
    (($s.ComputeHash([Text.Encoding]::UTF8.GetBytes($data)) | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Enc([string]$v) { [uri]::EscapeDataString($v) }

# $headers: ordered hashtable of lowercase header name -> raw value (already the value we will send)
function New-Auth([string]$method, [string]$path, [string]$paramList, $headers) {
    $start = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $end = $start + 600
    $keyTime = "$start;$end"
    $signKey = HmacSha1Hex ([Text.Encoding]::UTF8.GetBytes($skey)) $keyTime

    $names = $headers.Keys | ForEach-Object { $_.ToLower() } | Sort-Object
    $headerPairs = @()
    foreach ($n in $names) { $headerPairs += ("{0}={1}" -f $n, (Enc $headers[$n])) }
    $headerStr = $headerPairs -join '&'

    $params = @()
    if ($paramList) {
        foreach ($p in ($paramList -split ';')) { $params += ("{0}=" -f $p) }
    }
    $paramStr = $params -join '&'

    $httpString = "$method`n$path`n$paramStr`n$headerStr`n"
    $stringToSign = "sha1`n$keyTime`n" + (Sha1Hex $httpString) + "`n"
    $sig = HmacSha1Hex ([Text.Encoding]::UTF8.GetBytes($signKey)) $stringToSign

    $list = ($names -join ';')
    return "q-sign-algorithm=sha1&q-ak=$sid&q-sign-time=$keyTime&q-key-time=$keyTime" +
           "&q-header-list=$list&q-url-param-list=$paramList&q-signature=$sig"
}

function Show-Result([string]$label, [string]$text) {
    Write-Output ("{0} {1}" -f $label, $text)
}

# ---------------- 1) ACL: public-read ----------------
$a1Headers = [ordered]@{ 'host' = $cosHost; 'x-cos-acl' = 'public-read' }
$a1 = New-Auth 'put' '/' 'acl' $a1Headers

$r1 = curl.exe --noproxy "*" -s -w "`nHTTP_STATUS:%{http_code}" -X PUT "https://$cosHost/?acl" `
    -H "x-cos-acl: public-read" -H "Authorization: $a1"
Write-Output ("[ACL]  " + ($r1 -join ' '))

# ---------------- 2) CORS ----------------
$corsXml = @"
<CORSConfiguration>
 <CORSRule>
  <AllowedOrigin>$Origin</AllowedOrigin>
  <AllowedMethod>PUT</AllowedMethod>
  <AllowedMethod>GET</AllowedMethod>
  <AllowedMethod>HEAD</AllowedMethod>
  <AllowedHeader>*</AllowedHeader>
  <ExposeHeader>ETag</ExposeHeader>
  <MaxAgeSeconds>600</MaxAgeSeconds>
 </CORSRule>
 <CORSRule>
  <AllowedOrigin>http://127.0.0.1:8080</AllowedOrigin>
  <AllowedMethod>PUT</AllowedMethod>
  <AllowedMethod>GET</AllowedMethod>
  <AllowedMethod>HEAD</AllowedMethod>
  <AllowedHeader>*</AllowedHeader>
  <ExposeHeader>ETag</ExposeHeader>
  <MaxAgeSeconds>600</MaxAgeSeconds>
 </CORSRule>
</CORSConfiguration>
"@

$corsBytes = [Text.Encoding]::UTF8.GetBytes($corsXml)
$md5 = [System.Security.Cryptography.MD5]::Create().ComputeHash($corsBytes)
$md5B64 = [Convert]::ToBase64String($md5)

$c1Headers = [ordered]@{
    'content-md5'   = $md5B64
    'content-type'  = 'application/xml'
    'host'          = $cosHost
}
$c1 = New-Auth 'put' '/' 'cors' $c1Headers

$tmp = Join-Path $env:TEMP 'cors.xml'
[IO.File]::WriteAllText($tmp, $corsXml, [Text.UTF8Encoding]::new($false))

$r2 = curl.exe --noproxy "*" -s -w "`nHTTP_STATUS:%{http_code}" -X PUT "https://$cosHost/?cors" `
    -H "Content-Type: application/xml" -H "Content-MD5: $md5B64" -H "Authorization: $c1" `
    --data-binary "@$tmp"
Write-Output ("[CORS] " + ($r2 -join ' '))

# ---------------- 3) Lifecycle: chat/ 30 days ----------------
$lifeXml = @"
<LifecycleConfiguration>
 <Rule>
  <ID>expire-chat-30d</ID>
  <Filter><Prefix>chat/</Prefix></Filter>
  <Status>Enabled</Status>
  <Expiration><Days>30</Days></Expiration>
 </Rule>
</LifecycleConfiguration>
"@

$lifeBytes = [Text.Encoding]::UTF8.GetBytes($lifeXml)
$lifeMd5 = [Convert]::ToBase64String([System.Security.Cryptography.MD5]::Create().ComputeHash($lifeBytes))

$l1Headers = [ordered]@{
    'content-md5'  = $lifeMd5
    'content-type' = 'application/xml'
    'host'         = $cosHost
}
$l1 = New-Auth 'put' '/' 'lifecycle' $l1Headers

$tmp2 = Join-Path $env:TEMP 'lifecycle.xml'
[IO.File]::WriteAllText($tmp2, $lifeXml, [Text.UTF8Encoding]::new($false))

$r3 = curl.exe --noproxy "*" -s -w "`nHTTP_STATUS:%{http_code}" -X PUT "https://$cosHost/?lifecycle" `
    -H "Content-Type: application/xml" -H "Content-MD5: $lifeMd5" -H "Authorization: $l1" `
    --data-binary "@$tmp2"
Write-Output ("[LIFE] " + ($r3 -join ' '))
Write-Output "DONE"
