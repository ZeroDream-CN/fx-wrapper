#Requires -Version 5.1
<#
.SYNOPSIS
  Upload build artifacts to the forge release for CI_COMMIT_TAG.
  Supports Gitea, Forgejo, and GitHub. Uses RELEASE_TOKEN from the environment.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = "Stop"

function Get-RequiredEnv([string]$Name) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) {
        throw "Required environment variable '$Name' is not set."
    }
    return $value
}

function Invoke-ForgeJson {
    param(
        [Parameter(Mandatory = $true)][string]$Method,
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][hashtable]$Headers,
        [object]$Body = $null
    )

    $params = @{
        Method      = $Method
        Uri         = $Uri
        Headers     = $Headers
        ContentType = "application/json"
    }
    if ($null -ne $Body) {
        $params.Body = ($Body | ConvertTo-Json -Compress)
    }

    try {
        return Invoke-RestMethod @params
    } catch {
        $status = $null
        if ($_.Exception.Response) {
            $status = [int]$_.Exception.Response.StatusCode
        }
        if ($Method -eq "GET" -and $status -eq 404) {
            return $null
        }
        throw
    }
}

function Publish-MultipartFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][hashtable]$Headers,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$FieldName
    )

    Add-Type -AssemblyName System.Net.Http
    $client = [System.Net.Http.HttpClient]::new()
    try {
        foreach ($key in $Headers.Keys) {
            [void]$client.DefaultRequestHeaders.TryAddWithoutValidation($key, $Headers[$key])
        }

        $multipart = [System.Net.Http.MultipartFormDataContent]::new()
        $stream = [System.IO.File]::OpenRead($FilePath)
        try {
            $fileContent = [System.Net.Http.StreamContent]::new($stream)
            $fileName = [System.IO.Path]::GetFileName($FilePath)
            $fileContent.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/octet-stream")
            $multipart.Add($fileContent, $FieldName, $fileName)

            $response = $client.PostAsync($Uri, $multipart).GetAwaiter().GetResult()
            $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            if (-not $response.IsSuccessStatusCode) {
                throw "Upload failed ($([int]$response.StatusCode)): $text"
            }
        } finally {
            $stream.Dispose()
            $multipart.Dispose()
        }
    } finally {
        $client.Dispose()
    }
}

$token = Get-RequiredEnv "RELEASE_TOKEN"
$forgeType = (Get-RequiredEnv "CI_FORGE_TYPE").ToLowerInvariant()
$forgeUrl = (Get-RequiredEnv "CI_FORGE_URL").TrimEnd("/")
$owner = Get-RequiredEnv "CI_REPO_OWNER"
$repo = Get-RequiredEnv "CI_REPO_NAME"
$tag = Get-RequiredEnv "CI_COMMIT_TAG"

$files = @(Get-ChildItem -LiteralPath $Path -File -ErrorAction Stop)
if ($files.Count -eq 0) {
    throw "No files found to upload under: $Path"
}

$headers = @{
    Authorization = "token $token"
    Accept        = "application/json"
}

function Get-OrCreateRelease {
    param(
        [Parameter(Mandatory = $true)][scriptblock]$Get,
        [Parameter(Mandatory = $true)][scriptblock]$Create,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $release = & $Get
    if ($release) {
        Write-Host "Using existing $Label release id=$($release.id) for tag $tag"
        return $release
    }

    Write-Host "Creating $Label release for tag $tag"
    try {
        return & $Create
    } catch {
        # Another workflow (e.g. linux release) may have created it first.
        Start-Sleep -Seconds 2
        $release = & $Get
        if ($release) {
            Write-Host "Using existing $Label release id=$($release.id) for tag $tag (after create race)"
            return $release
        }
        throw
    }
}

switch ($forgeType) {
    { $_ -in @("gitea", "forgejo") } {
        $apiBase = "$forgeUrl/api/v1"
        $release = Get-OrCreateRelease -Label $forgeType `
            -Get { Invoke-ForgeJson -Method GET -Uri "$apiBase/repos/$owner/$repo/releases/tags/$tag" -Headers $headers } `
            -Create {
                Invoke-ForgeJson -Method POST -Uri "$apiBase/repos/$owner/$repo/releases" -Headers $headers -Body @{
                    tag_name   = $tag
                    name       = $tag
                    draft      = $false
                    prerelease = $false
                }
            }

        foreach ($file in $files) {
            $name = [uri]::EscapeDataString($file.Name)
            $uploadUri = "$apiBase/repos/$owner/$repo/releases/$($release.id)/assets?name=$name"
            Write-Host "Uploading $($file.Name)"
            # Replace asset with the same name if it already exists.
            if ($release.assets) {
                $existing = @($release.assets | Where-Object { $_.name -eq $file.Name })
                foreach ($asset in $existing) {
                    Write-Host "Removing existing asset id=$($asset.id) ($($asset.name))"
                    Invoke-ForgeJson -Method DELETE -Uri "$apiBase/repos/$owner/$repo/releases/assets/$($asset.id)" -Headers $headers | Out-Null
                }
            }
            Publish-MultipartFile -Uri $uploadUri -Headers $headers -FilePath $file.FullName -FieldName "attachment"
        }
    }
    "github" {
        if ($forgeUrl -match "github\.com") {
            $apiBase = "https://api.github.com"
            $uploadBase = "https://uploads.github.com"
        } else {
            $apiBase = "$forgeUrl/api/v3"
            $uploadBase = "$forgeUrl/api/uploads"
        }

        $headers = @{
            Authorization = "Bearer $token"
            Accept        = "application/vnd.github+json"
            "User-Agent"  = "fx-wrapper-ci"
        }

        $release = Get-OrCreateRelease -Label "github" `
            -Get { Invoke-ForgeJson -Method GET -Uri "$apiBase/repos/$owner/$repo/releases/tags/$tag" -Headers $headers } `
            -Create {
                Invoke-ForgeJson -Method POST -Uri "$apiBase/repos/$owner/$repo/releases" -Headers $headers -Body @{
                    tag_name               = $tag
                    name                   = $tag
                    draft                  = $false
                    prerelease             = $false
                    generate_release_notes = $true
                }
            }

        foreach ($file in $files) {
            if ($release.assets) {
                $existing = @($release.assets | Where-Object { $_.name -eq $file.Name })
                foreach ($asset in $existing) {
                    Write-Host "Removing existing asset id=$($asset.id) ($($asset.name))"
                    Invoke-ForgeJson -Method DELETE -Uri "$apiBase/repos/$owner/$repo/releases/assets/$($asset.id)" -Headers $headers | Out-Null
                }
            }
            $name = [uri]::EscapeDataString($file.Name)
            $uploadUri = "$uploadBase/repos/$owner/$repo/releases/$($release.id)/assets?name=$name"
            Write-Host "Uploading $($file.Name)"

            Add-Type -AssemblyName System.Net.Http
            $client = [System.Net.Http.HttpClient]::new()
            try {
                foreach ($key in $headers.Keys) {
                    [void]$client.DefaultRequestHeaders.TryAddWithoutValidation($key, $headers[$key])
                }
                $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
                $content = [System.Net.Http.ByteArrayContent]::new($bytes)
                $content.Headers.ContentType = [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse("application/octet-stream")
                $response = $client.PostAsync($uploadUri, $content).GetAwaiter().GetResult()
                $text = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
                if (-not $response.IsSuccessStatusCode) {
                    throw "Upload failed ($([int]$response.StatusCode)): $text"
                }
            } finally {
                $client.Dispose()
            }
        }
    }
    default {
        throw "Unsupported CI_FORGE_TYPE '$forgeType'. Expected gitea, forgejo, or github."
    }
}

Write-Host "Release upload complete."
