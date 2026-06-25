#!/usr/bin/env pwsh
# Data-gated MSX/MSX2 firmware and media boot smoke runner.
#
# BIOS, firmware, cartridges, disks, and tapes are never committed. Point this
# script at local artifacts through parameters or the matching MNEMOS_MSX*
# environment variables; outputs stay under build/scratch/.

param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string]$MsxBios = "",
    [string]$MsxRom = "",
    [string]$MsxRom2 = "",
    [string]$MsxRomDir = "",
    [string]$MsxDiskRom = "",
    [string]$MsxDsk = "",
    [string]$MsxCas = "",
    [string]$MsxKanjiRom = "",
    [string]$MsxLogoRom = "",
    [string]$MsxMapper = "",
    [string]$MsxMapper2 = "",
    [string]$MsxExpandedSlots = "",
    [string]$MsxRamSlot = "",
    [string]$MsxDiskSlot = "",
    [string]$MsxCart2Slot = "",
    [string]$MsxRegion = "",
    [string]$MsxBootKeys = "",
    [string]$MsxBootSha256 = "",
    [string]$Msx2Bios = "",
    [string]$Msx2Firmware = "",
    [string]$Msx2SubRom = "",
    [string]$Msx2LogoRom = "",
    [string]$Msx2Rom = "",
    [string]$Msx2Rom2 = "",
    [string]$Msx2RomDir = "",
    [string]$Msx2DiskRom = "",
    [string]$Msx2Dsk = "",
    [string]$Msx2Cas = "",
    [string]$Msx2KanjiRom = "",
    [string]$Msx2Mapper = "",
    [string]$Msx2Mapper2 = "",
    [string]$Msx2ExpandedSlots = "",
    [string]$Msx2RamSlot = "",
    [string]$Msx2SubSlot = "",
    [string]$Msx2DiskSlot = "",
    [string]$Msx2Cart2Slot = "",
    [string]$Msx2RamSize = "",
    [string]$Msx2Region = "",
    [string]$Msx2BootKeys = "",
    [string]$Msx2BootSha256 = "",
    [string]$CaseManifest = "",
    [string]$RomProfileManifest = "",
    [int]$Frames = 200,
    [int]$RetryFrames = 0,
    [int]$MaxRoms = 0,
    [int]$SkipRoms = 0,
    [switch]$Recurse,
    [switch]$RequireData
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$configuredRetryFrames = $RetryFrames

$scriptDir = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)

$allEnvNames = @(
    "MNEMOS_MSX_BIOS",
    "MNEMOS_MSX_ROM",
    "MNEMOS_MSX_ROM2",
    "MNEMOS_MSX_CART2",
    "MNEMOS_MSX_DISK_ROM",
    "MNEMOS_MSX_DSK",
    "MNEMOS_MSX_CAS",
    "MNEMOS_MSX_KANJI_ROM",
    "MNEMOS_MSX_LOGO_ROM",
    "MNEMOS_MSX_MAPPER",
    "MNEMOS_MSX_MAPPER2",
    "MNEMOS_MSX_EXPANDED_SLOTS",
    "MNEMOS_MSX_RAM_SLOT",
    "MNEMOS_MSX_DISK_SLOT",
    "MNEMOS_MSX_CART2_SLOT",
    "MNEMOS_MSX_REGION",
    "MNEMOS_MSX_BOOT_KEYS",
    "MNEMOS_MSX_BOOT_FRAMES",
    "MNEMOS_MSX_BOOT_SHA256",
    "MNEMOS_MSX2_BIOS",
    "MNEMOS_MSX2_FIRMWARE",
    "MNEMOS_MSX2_SUB_ROM",
    "MNEMOS_MSX2_SUBROM",
    "MNEMOS_MSX2_LOGO_ROM",
    "MNEMOS_MSX2_ROM",
    "MNEMOS_MSX2_ROM2",
    "MNEMOS_MSX2_CART2",
    "MNEMOS_MSX2_DISK_ROM",
    "MNEMOS_MSX2_DISKROM",
    "MNEMOS_MSX2_DSK",
    "MNEMOS_MSX2_CAS",
    "MNEMOS_MSX2_KANJI_ROM",
    "MNEMOS_MSX2_MAPPER",
    "MNEMOS_MSX2_MAPPER2",
    "MNEMOS_MSX2_EXPANDED_SLOTS",
    "MNEMOS_MSX2_RAM_SLOT",
    "MNEMOS_MSX2_SUB_SLOT",
    "MNEMOS_MSX2_DISK_SLOT",
    "MNEMOS_MSX2_CART2_SLOT",
    "MNEMOS_MSX2_RAM_SIZE",
    "MNEMOS_MSX2_REGION",
    "MNEMOS_MSX2_BOOT_KEYS",
    "MNEMOS_MSX2_BOOT_FRAMES",
    "MNEMOS_MSX2_BOOT_SHA256",
    "MNEMOS_MSX_ROM_PROFILE_MANIFEST"
)

$msxBiosBytes = 0x8000
$msxDiskRomBytes = 0x4000
$msxLogoRomBytes = 0x4000
$msx2MainBiosBytes = 0x8000
$msx2SubRomBytes = 0x4000
$msx2LogoRomBytes = 0x4000
$msx2DiskRomBytes = 0x4000
$msx2PackedMainSubBytes = $msx2MainBiosBytes + $msx2SubRomBytes
$msx2PackedMainSubDiskBytes = $msx2PackedMainSubBytes + $msx2DiskRomBytes

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repoRoot $Path
}

function Get-ConfiguredValue {
    param(
        [string]$Value,
        [string]$EnvName,
        [string]$FallbackEnvName = ""
    )
    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        return $Value
    }
    $envValue = [Environment]::GetEnvironmentVariable($EnvName)
    if (-not [string]::IsNullOrWhiteSpace($envValue)) {
        return $envValue
    }
    if (-not [string]::IsNullOrWhiteSpace($FallbackEnvName)) {
        return [Environment]::GetEnvironmentVariable($FallbackEnvName)
    }
    return ""
}

function Get-ObjectStringProperty {
    param(
        [object]$Object,
        [string]$Name
    )
    if ($null -eq $Object) {
        return ""
    }
    $property = $Object.PSObject.Properties | Where-Object { $_.Name -eq $Name } | Select-Object -First 1
    if ($null -eq $property -or $null -eq $property.Value) {
        return ""
    }
    return [string]$property.Value
}

function Get-ObjectFirstStringProperty {
    param(
        [object]$Object,
        [string[]]$Names
    )
    foreach ($name in $Names) {
        $value = Get-ObjectStringProperty -Object $Object -Name $name
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
    }
    return ""
}

function Resolve-ExistingFile {
    param(
        [string]$Path,
        [string]$Label
    )
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    $resolved = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Label path not found: $Path"
    }
    return (Resolve-Path -LiteralPath $resolved).Path
}

function Resolve-ExistingDirectory {
    param(
        [string]$Path,
        [string]$Label
    )
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    $resolved = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "$Label directory not found: $Path"
    }
    return (Resolve-Path -LiteralPath $resolved).Path
}

function Import-CaseManifest {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return @()
    }

    $resolved = Resolve-ExistingFile -Path $Path -Label "MSX/MSX2 case manifest"
    try {
        $document = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "MSX/MSX2 case manifest is not valid JSON: $Path"
    }

    if ($null -eq $document) {
        return @()
    }
    if ($document -is [System.Array]) {
        return @($document)
    }

    $casesProperty = $document.PSObject.Properties | Where-Object { $_.Name -eq "cases" } | Select-Object -First 1
    if ($null -eq $casesProperty -or $null -eq $casesProperty.Value) {
        throw "MSX/MSX2 case manifest must be an array or an object with a 'cases' array: $Path"
    }
    return @($casesProperty.Value)
}

function Import-RomProfileManifest {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return @()
    }

    $resolved = Resolve-ExistingFile -Path $Path -Label "MSX/MSX2 ROM profile manifest"
    try {
        $document = Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
        throw "MSX/MSX2 ROM profile manifest is not valid JSON: $Path"
    }

    if ($null -eq $document) {
        return @()
    }
    if ($document -is [System.Array]) {
        return @($document)
    }

    $profilesProperty = $document.PSObject.Properties | Where-Object { $_.Name -eq "profiles" } | Select-Object -First 1
    if ($null -eq $profilesProperty -or $null -eq $profilesProperty.Value) {
        throw "MSX/MSX2 ROM profile manifest must be an array or an object with a 'profiles' array: $Path"
    }
    return @($profilesProperty.Value)
}

function Get-ObjectBoolProperty {
    param(
        [object]$Object,
        [string]$Name
    )
    if ($null -eq $Object) {
        return $false
    }
    $property = $Object.PSObject.Properties | Where-Object { $_.Name -eq $Name } | Select-Object -First 1
    if ($null -eq $property -or $null -eq $property.Value) {
        return $false
    }
    if ($property.Value -is [bool]) {
        return [bool]$property.Value
    }
    $text = ([string]$property.Value).Trim().ToLowerInvariant()
    return $text -eq "true" -or $text -eq "yes" -or $text -eq "1"
}

function Normalize-HexString {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }
    $text = $Value.Trim().ToLowerInvariant()
    if ($text.StartsWith("0x")) {
        $text = $text.Substring(2)
    }
    return $text -replace "[^0-9a-f]", ""
}

function Add-RomDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Paths,
        [string]$Directory
    )
    if ([string]::IsNullOrWhiteSpace($Directory)) {
        return
    }

    $childArgs = @{
        LiteralPath = $Directory
        File = $true
        ErrorAction = "SilentlyContinue"
    }
    if ($Recurse) {
        $childArgs.Recurse = $true
    }

    foreach ($file in Get-ChildItem @childArgs | Sort-Object FullName) {
        $extension = $file.Extension.ToLowerInvariant()
        if ($extension -eq ".rom" -or $extension -eq ".mx1" -or $extension -eq ".mx2") {
            if ($file.Length -lt 0x2000) {
                Write-Host ("[skip] {0} is {1} bytes; not a standalone cartridge image" -f $file.FullName, $file.Length) -ForegroundColor DarkGray
                continue
            }
            $Paths.Add($file.FullName)
        }
    }
}

function Add-OptionalEnvPath {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Env,
        [string]$Name,
        [string]$Path,
        [string]$Label
    )
    $resolved = Resolve-ExistingFile -Path $Path -Label $Label
    if (-not [string]::IsNullOrWhiteSpace($resolved)) {
        $Env[$Name] = $resolved
    }
}

function Add-OptionalEnvValue {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Env,
        [string]$Name,
        [string]$Value
    )
    if (-not [string]::IsNullOrWhiteSpace($Value)) {
        $Env[$Name] = $Value
    }
}

function Add-ManifestEnvPath {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Env,
        [object]$Case,
        [string]$Name,
        [string[]]$Properties,
        [string]$Label
    )

    $path = Get-ObjectFirstStringProperty -Object $Case -Names $Properties
    if (-not [string]::IsNullOrWhiteSpace($path)) {
        Add-OptionalEnvPath -Env $Env -Name $Name -Path $path -Label $Label
    }
}

function Add-ManifestEnvValue {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Env,
        [object]$Case,
        [string]$Name,
        [string[]]$Properties
    )

    $value = Get-ObjectFirstStringProperty -Object $Case -Names $Properties
    if (-not [string]::IsNullOrWhiteSpace($value)) {
        $Env[$Name] = $value
    }
}

function Resolve-ProfileFile {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Env,
        [string]$Path,
        [string]$Label,
        [string]$RelativeToEnvName = ""
    )
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return Resolve-ExistingFile -Path $Path -Label $Label
    }

    $repoCandidate = Resolve-RepoPath $Path
    if (Test-Path -LiteralPath $repoCandidate -PathType Leaf) {
        return (Resolve-Path -LiteralPath $repoCandidate).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($RelativeToEnvName) -and
        $Env.ContainsKey($RelativeToEnvName) -and
        -not [string]::IsNullOrWhiteSpace([string]$Env[$RelativeToEnvName])) {
        $baseDir = Split-Path -Parent ([string]$Env[$RelativeToEnvName])
        $baseCandidate = Join-Path $baseDir $Path
        if (Test-Path -LiteralPath $baseCandidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $baseCandidate).Path
        }
    }

    throw "$Label path not found: $Path"
}

function Add-ProfileEnvPath {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Env,
        [object]$Profile,
        [string]$Name,
        [string[]]$Properties,
        [string]$Label,
        [string]$RelativeToEnvName = ""
    )

    $path = Get-ObjectFirstStringProperty -Object $Profile -Names $Properties
    if (-not [string]::IsNullOrWhiteSpace($path)) {
        $Env[$Name] = Resolve-ProfileFile -Env $Env -Path $path -Label $Label -RelativeToEnvName $RelativeToEnvName
    }
}

$script:romProfileSha256Cache = @{}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if (-not $script:romProfileSha256Cache.ContainsKey($resolved)) {
        $script:romProfileSha256Cache[$resolved] =
            (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return [string]$script:romProfileSha256Cache[$resolved]
}

function Find-RomProfile {
    param(
        [object[]]$Profiles,
        [string]$System,
        [string]$RomPath
    )
    if ($null -eq $Profiles -or $Profiles.Count -eq 0) {
        return $null
    }

    $leafName = [System.IO.Path]::GetFileName($RomPath)
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($RomPath)
    $romSha256 = ""
    foreach ($profile in $Profiles) {
        $profileSystem = (Get-ObjectFirstStringProperty -Object $profile -Names @("system")).ToLowerInvariant()
        if (-not [string]::IsNullOrWhiteSpace($profileSystem) -and $profileSystem -ne $System) {
            continue
        }

        $profileName = Get-ObjectFirstStringProperty -Object $profile -Names @("name", "filename", "file")
        if (-not [string]::IsNullOrWhiteSpace($profileName) -and
            -not $profileName.Equals($leafName, [System.StringComparison]::OrdinalIgnoreCase) -and
            -not $profileName.Equals($baseName, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        $profileSha256 = Normalize-HexString (Get-ObjectFirstStringProperty -Object $profile -Names @("sha256", "rom_sha256", "file_sha256"))
        if (-not [string]::IsNullOrWhiteSpace($profileSha256)) {
            if ([string]::IsNullOrWhiteSpace($romSha256)) {
                $romSha256 = Get-FileSha256 -Path $RomPath
            }
            if ($profileSha256 -ne $romSha256) {
                continue
            }
        }

        if ([string]::IsNullOrWhiteSpace($profileName) -and [string]::IsNullOrWhiteSpace($profileSha256)) {
            continue
        }
        return $profile
    }
    return $null
}

function Apply-RomProfileEnv {
    param(
        [hashtable]$Env,
        [object]$Profile,
        [string]$System
    )
    if ($null -eq $Profile) {
        return
    }

    if ($System -eq "msx2") {
        Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX2_FIRMWARE" -Properties @("firmware") -Label "MSX2 firmware" -RelativeToEnvName "MNEMOS_MSX2_FIRMWARE"
        Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX2_BIOS" -Properties @("bios", "main_bios", "mainBios") -Label "MSX2 BIOS" -RelativeToEnvName "MNEMOS_MSX2_BIOS"
        Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX2_SUB_ROM" -Properties @("sub_rom", "subRom", "sub_bios", "subBios") -Label "MSX2 sub-ROM" -RelativeToEnvName "MNEMOS_MSX2_SUB_ROM"
        Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX2_LOGO_ROM" -Properties @("logo_rom", "logoRom") -Label "MSX2 logo ROM" -RelativeToEnvName "MNEMOS_MSX2_LOGO_ROM"
        Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX2_DISK_ROM" -Properties @("disk_rom", "diskRom") -Label "MSX2 disk ROM" -RelativeToEnvName "MNEMOS_MSX2_DISK_ROM"
        Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX2_KANJI_ROM" -Properties @("kanji_rom", "kanjiRom") -Label "MSX2 Kanji ROM" -RelativeToEnvName "MNEMOS_MSX2_KANJI_ROM"
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_BOOT_KEYS" -Properties @("boot_keys", "bootKeys", "keys")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_MAPPER" -Properties @("mapper")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_MAPPER2" -Properties @("mapper2")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_BOOT_FRAMES" -Properties @("frames", "boot_frames", "bootFrames")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_BOOT_SHA256" -Properties @("boot_sha256", "bootSha256", "framebuffer_sha256", "framebufferSha256", "golden_hash", "goldenHash")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_REGION" -Properties @("region")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_EXPANDED_SLOTS" -Properties @("expanded_slots", "expandedSlots")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_RAM_SLOT" -Properties @("ram_slot", "ramSlot")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_SUB_SLOT" -Properties @("sub_slot", "subSlot")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_DISK_SLOT" -Properties @("disk_slot", "diskSlot")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_CART2_SLOT" -Properties @("cart2_slot", "cart2Slot")
        Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX2_RAM_SIZE" -Properties @("ram_size", "ramSize")
        return
    }

    Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX_BIOS" -Properties @("bios") -Label "MSX BIOS" -RelativeToEnvName "MNEMOS_MSX_BIOS"
    Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX_DISK_ROM" -Properties @("disk_rom", "diskRom") -Label "MSX disk ROM" -RelativeToEnvName "MNEMOS_MSX_DISK_ROM"
    Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX_KANJI_ROM" -Properties @("kanji_rom", "kanjiRom") -Label "MSX Kanji ROM" -RelativeToEnvName "MNEMOS_MSX_KANJI_ROM"
    Add-ProfileEnvPath -Env $Env -Profile $Profile -Name "MNEMOS_MSX_LOGO_ROM" -Properties @("logo_rom", "logoRom") -Label "MSX logo ROM" -RelativeToEnvName "MNEMOS_MSX_LOGO_ROM"
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_BOOT_KEYS" -Properties @("boot_keys", "bootKeys", "keys")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_MAPPER" -Properties @("mapper")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_MAPPER2" -Properties @("mapper2")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_BOOT_FRAMES" -Properties @("frames", "boot_frames", "bootFrames")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_BOOT_SHA256" -Properties @("boot_sha256", "bootSha256", "framebuffer_sha256", "framebufferSha256", "golden_hash", "goldenHash")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_REGION" -Properties @("region")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_EXPANDED_SLOTS" -Properties @("expanded_slots", "expandedSlots")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_RAM_SLOT" -Properties @("ram_slot", "ramSlot")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_DISK_SLOT" -Properties @("disk_slot", "diskSlot")
    Add-ManifestEnvValue -Env $Env -Case $Profile -Name "MNEMOS_MSX_CART2_SLOT" -Properties @("cart2_slot", "cart2Slot")
}

function New-RomSmokeCase {
    param(
        [string]$System,
        [string]$RomPath,
        [hashtable]$BaseEnv,
        [object[]]$Profiles
    )
    $profile = Find-RomProfile -Profiles $Profiles -System $System -RomPath $RomPath
    if (Get-ObjectBoolProperty -Object $profile -Name "skip") {
        $reason = Get-ObjectFirstStringProperty -Object $profile -Names @("reason", "skip_reason", "skipReason")
        if ([string]::IsNullOrWhiteSpace($reason)) {
            $reason = "profile marked as skip"
        }
        Write-Host ("[skip] {0}/{1}: {2}" -f $System, [System.IO.Path]::GetFileName($RomPath), $reason) -ForegroundColor DarkGray
        return $null
    }

    $env = @{} + $BaseEnv
    if ($System -eq "msx2") {
        $env["MNEMOS_MSX2_ROM"] = $RomPath
    } else {
        $env["MNEMOS_MSX_ROM"] = $RomPath
    }
    Apply-RomProfileEnv -Env $env -Profile $profile -System $System
    return New-SmokeCase -System $System -Name ("rom-" + [System.IO.Path]::GetFileNameWithoutExtension($RomPath)) -Env $env
}

function Test-EnvFileMinBytes {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Env,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][long]$MinimumBytes
    )

    if (-not $Env.ContainsKey($Name)) {
        return $false
    }
    $path = [string]$Env[$Name]
    if ([string]::IsNullOrWhiteSpace($path) -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
        return $false
    }
    return ((Get-Item -LiteralPath $path).Length -ge $MinimumBytes)
}

function Test-Msx2BootDataBacked {
    param([Parameter(Mandatory = $true)][hashtable]$Env)

    $hasPackedFirmware = Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_FIRMWARE" -MinimumBytes $msx2PackedMainSubBytes
    $hasPackedFirmwareDisk = Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_FIRMWARE" -MinimumBytes $msx2PackedMainSubDiskBytes
    $hasMainBios = Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_BIOS" -MinimumBytes $msx2MainBiosBytes
    $hasPackedBios = Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_BIOS" -MinimumBytes $msx2PackedMainSubBytes
    $hasPackedBiosDisk = Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_BIOS" -MinimumBytes $msx2PackedMainSubDiskBytes
    $hasSubRom = (Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_SUB_ROM" -MinimumBytes $msx2SubRomBytes) -or
        (Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_SUBROM" -MinimumBytes $msx2SubRomBytes)
    $hasFirmware = $hasPackedFirmware -or ($hasMainBios -and ($hasPackedBios -or $hasSubRom))
    if (-not $hasFirmware) {
        return $false
    }

    if ($Env.ContainsKey("MNEMOS_MSX2_DSK")) {
        return $hasPackedFirmwareDisk -or $hasPackedBiosDisk -or
            (Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_DISK_ROM" -MinimumBytes $msx2DiskRomBytes) -or
            (Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX2_DISKROM" -MinimumBytes $msx2DiskRomBytes)
    }

    return $true
}

function Test-MsxBootDataBacked {
    param([Parameter(Mandatory = $true)][hashtable]$Env)

    if (-not (Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX_BIOS" -MinimumBytes $msxBiosBytes)) {
        return $false
    }
    if ($Env.ContainsKey("MNEMOS_MSX_DSK")) {
        return Test-EnvFileMinBytes -Env $Env -Name "MNEMOS_MSX_DISK_ROM" -MinimumBytes $msxDiskRomBytes
    }
    return $true
}

function New-SmokeCase {
    param(
        [string]$System,
        [string]$Name,
        [hashtable]$Env
    )
    $dataBacked = if ($System -eq "msx2") {
        Test-Msx2BootDataBacked -Env $Env
    } else {
        Test-MsxBootDataBacked -Env $Env
    }
    return [pscustomobject]@{
        system = $System
        name = $Name
        env = $Env
        dataBacked = $dataBacked
    }
}

function New-ManifestSmokeCase {
    param(
        [Parameter(Mandatory = $true)][object]$Case,
        [int]$Index,
        $MsxBaseEnv = $null,
        $Msx2BaseEnv = $null
    )

    $system = (Get-ObjectFirstStringProperty -Object $Case -Names @("system")).ToLowerInvariant()
    if ($system -ne "msx" -and $system -ne "msx2") {
        throw "MSX/MSX2 case manifest entry $Index has unsupported system '$system' (expected 'msx' or 'msx2')."
    }

    $name = Get-ObjectFirstStringProperty -Object $Case -Names @("name")
    if ([string]::IsNullOrWhiteSpace($name)) {
        $name = "manifest-{0:D3}" -f $Index
    }

    if ($system -eq "msx2") {
        $env = if ($null -ne $Msx2BaseEnv) { @{} + $Msx2BaseEnv } else { @{} }
        $env["MNEMOS_MSX2_BOOT_FRAMES"] = $Frames.ToString()
        $frames = Get-ObjectFirstStringProperty -Object $Case -Names @("frames", "boot_frames", "bootFrames")
        if (-not [string]::IsNullOrWhiteSpace($frames)) {
            $frameCount = 0
            if (-not [int]::TryParse($frames, [ref]$frameCount) -or $frameCount -le 0) {
                throw "MSX/MSX2 case manifest entry $Index has invalid frames value '$frames'."
            }
            $env["MNEMOS_MSX2_BOOT_FRAMES"] = $frameCount.ToString()
        }

        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_FIRMWARE" -Properties @("firmware") -Label "MSX2 firmware"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_BIOS" -Properties @("bios") -Label "MSX2 BIOS"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_SUB_ROM" -Properties @("sub_rom", "subRom", "sub_bios", "subBios") -Label "MSX2 sub-ROM"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_LOGO_ROM" -Properties @("logo_rom", "logoRom") -Label "MSX2 logo ROM"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_ROM" -Properties @("rom", "cartridge") -Label "MSX2 cartridge"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_ROM2" -Properties @("rom2", "cartridge2", "cart2") -Label "MSX2 second cartridge"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_DISK_ROM" -Properties @("disk_rom", "diskRom") -Label "MSX2 disk ROM"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_DSK" -Properties @("dsk", "disk") -Label "MSX2 disk image"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_CAS" -Properties @("cas", "cassette") -Label "MSX2 cassette image"
        Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX2_KANJI_ROM" -Properties @("kanji_rom", "kanjiRom") -Label "MSX2 Kanji ROM"
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_MAPPER" -Properties @("mapper")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_MAPPER2" -Properties @("mapper2")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_EXPANDED_SLOTS" -Properties @("expanded_slots", "expandedSlots")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_RAM_SLOT" -Properties @("ram_slot", "ramSlot")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_SUB_SLOT" -Properties @("sub_slot", "subSlot", "sub_bios_slot", "subBiosSlot")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_DISK_SLOT" -Properties @("disk_slot", "diskSlot")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_CART2_SLOT" -Properties @("cart2_slot", "cart2Slot", "cartridge2_slot", "cartridge2Slot")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_RAM_SIZE" -Properties @("ram_size", "ramSize")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_REGION" -Properties @("region")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_BOOT_KEYS" -Properties @("boot_keys", "bootKeys", "keys")
        Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX2_BOOT_SHA256" -Properties @("boot_sha256", "bootSha256", "sha256", "hash")
        return New-SmokeCase -System "msx2" -Name $name -Env $env
    }

    $env = if ($null -ne $MsxBaseEnv) { @{} + $MsxBaseEnv } else { @{} }
    $env["MNEMOS_MSX_BOOT_FRAMES"] = $Frames.ToString()
    $frames = Get-ObjectFirstStringProperty -Object $Case -Names @("frames", "boot_frames", "bootFrames")
    if (-not [string]::IsNullOrWhiteSpace($frames)) {
        $frameCount = 0
        if (-not [int]::TryParse($frames, [ref]$frameCount) -or $frameCount -le 0) {
            throw "MSX/MSX2 case manifest entry $Index has invalid frames value '$frames'."
        }
        $env["MNEMOS_MSX_BOOT_FRAMES"] = $frameCount.ToString()
    }

    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_BIOS" -Properties @("bios") -Label "MSX BIOS"
    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_ROM" -Properties @("rom", "cartridge") -Label "MSX cartridge"
    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_ROM2" -Properties @("rom2", "cartridge2", "cart2") -Label "MSX second cartridge"
    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_DISK_ROM" -Properties @("disk_rom", "diskRom") -Label "MSX disk ROM"
    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_DSK" -Properties @("dsk", "disk") -Label "MSX disk image"
    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_CAS" -Properties @("cas", "cassette") -Label "MSX cassette image"
    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_KANJI_ROM" -Properties @("kanji_rom", "kanjiRom") -Label "MSX Kanji ROM"
    Add-ManifestEnvPath -Env $env -Case $Case -Name "MNEMOS_MSX_LOGO_ROM" -Properties @("logo_rom", "logoRom") -Label "MSX logo ROM"
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_MAPPER" -Properties @("mapper")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_MAPPER2" -Properties @("mapper2")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_EXPANDED_SLOTS" -Properties @("expanded_slots", "expandedSlots")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_RAM_SLOT" -Properties @("ram_slot", "ramSlot")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_DISK_SLOT" -Properties @("disk_slot", "diskSlot")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_CART2_SLOT" -Properties @("cart2_slot", "cart2Slot", "cartridge2_slot", "cartridge2Slot")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_REGION" -Properties @("region")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_BOOT_KEYS" -Properties @("boot_keys", "bootKeys", "keys")
    Add-ManifestEnvValue -Env $env -Case $Case -Name "MNEMOS_MSX_BOOT_SHA256" -Properties @("boot_sha256", "bootSha256", "sha256", "hash")
    return New-SmokeCase -System "msx" -Name $name -Env $env
}

function Get-SafeName {
    param([string]$Name)
    return ($Name -replace "[^A-Za-z0-9_.-]", "_")
}

function Invoke-BootTest {
    param(
        [Parameter(Mandatory = $true)][string]$TestExe,
        [Parameter(Mandatory = $true)][pscustomobject]$Case,
        [Parameter(Mandatory = $true)][string]$LogPath
    )

    $saved = @{}
    foreach ($name in $allEnvNames) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name)
        [Environment]::SetEnvironmentVariable($name, $null, "Process")
    }

    try {
        foreach ($entry in $Case.env.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable($entry.Key, [string]$entry.Value, "Process")
        }

        $filter = if ($Case.system -eq "msx2") { "[golden][msx2]" } else { "[golden][msx]" }
        & $TestExe $filter *> $LogPath
        return $LASTEXITCODE
    } finally {
        foreach ($name in $allEnvNames) {
            [Environment]::SetEnvironmentVariable($name, $saved[$name], "Process")
        }
    }
}

function Get-CaseBootFramesEnvName {
    param([Parameter(Mandatory = $true)][pscustomobject]$Case)
    if ($Case.system -eq "msx2") {
        return "MNEMOS_MSX2_BOOT_FRAMES"
    }
    return "MNEMOS_MSX_BOOT_FRAMES"
}

function Get-CaseBootFrames {
    param([Parameter(Mandatory = $true)][pscustomobject]$Case)

    $name = Get-CaseBootFramesEnvName -Case $Case
    if (-not $Case.env.ContainsKey($name)) {
        return 0
    }

    $frames = 0
    if (-not [int]::TryParse([string]$Case.env[$name], [ref]$frames)) {
        return 0
    }
    return $frames
}

function Set-CaseBootFrames {
    param(
        [Parameter(Mandatory = $true)][pscustomobject]$Case,
        [Parameter(Mandatory = $true)][int]$FrameCount
    )

    $Case.env[(Get-CaseBootFramesEnvName -Case $Case)] = $FrameCount.ToString()
}

function Get-CaseBootKeysEnvName {
    param([Parameter(Mandatory = $true)][pscustomobject]$Case)
    if ($Case.system -eq "msx2") {
        return "MNEMOS_MSX2_BOOT_KEYS"
    }
    return "MNEMOS_MSX_BOOT_KEYS"
}

function Get-CaseBootKeys {
    param([Parameter(Mandatory = $true)][pscustomobject]$Case)

    $name = Get-CaseBootKeysEnvName -Case $Case
    if (-not $Case.env.ContainsKey($name)) {
        return ""
    }
    return [string]$Case.env[$name]
}

function Get-BootHashFromLog {
    param([string]$LogPath)
    if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        return $null
    }
    $text = Get-Content -LiteralPath $LogPath -Raw
    $matches = [regex]::Matches(
        $text,
        "(?:boot framebuffer sha256:|computed MSX2? boot framebuffer sha256 =)\s*([0-9a-f]{64})"
    )
    if ($matches.Count -eq 0) {
        return $null
    }
    return $matches[$matches.Count - 1].Groups[1].Value
}

function Get-MediaEnvNames {
    param([Parameter(Mandatory = $true)][string]$System)

    if ($System -eq "msx2") {
        return @(
            "MNEMOS_MSX2_ROM",
            "MNEMOS_MSX2_ROM2",
            "MNEMOS_MSX2_CART2",
            "MNEMOS_MSX2_DSK",
            "MNEMOS_MSX2_CAS",
            "MNEMOS_MSX2_MAPPER",
            "MNEMOS_MSX2_MAPPER2",
            "MNEMOS_MSX2_BOOT_SHA256"
        )
    }

    return @(
        "MNEMOS_MSX_ROM",
        "MNEMOS_MSX_ROM2",
        "MNEMOS_MSX_CART2",
        "MNEMOS_MSX_DSK",
        "MNEMOS_MSX_CAS",
        "MNEMOS_MSX_MAPPER",
        "MNEMOS_MSX_MAPPER2",
        "MNEMOS_MSX_BOOT_SHA256"
    )
}

function Copy-BaselineEnv {
    param([Parameter(Mandatory = $true)][pscustomobject]$Case)

    $env = @{} + $Case.env
    foreach ($name in (Get-MediaEnvNames -System $Case.system)) {
        $env.Remove($name)
    }
    return $env
}

function Get-BaselineCacheKey {
    param(
        [Parameter(Mandatory = $true)][string]$System,
        [Parameter(Mandatory = $true)][hashtable]$Env
    )

    $parts = [System.Collections.Generic.List[string]]::new()
    $parts.Add("system=$System")
    foreach ($key in ($Env.Keys | Sort-Object)) {
        $parts.Add(("{0}={1}" -f $key, [string]$Env[$key]))
    }
    return ($parts -join "`n")
}

$knownInvalidBootHashes = @{
    # C-BIOS MSX1 startup banner/header with no visible cartridge takeover.
    "326e35a4b84e1fbed55653072ce6d2695e2206ef7522e1727713b2618d4dd612" =
        "framebuffer remained on the C-BIOS MSX1 startup banner"
    # C-BIOS MSX1 cartridge initialization diagnostic screen.
    "e9488409d963dc9f08cf5956e1b4bdbd83bc5a96c193e80c00dda7ff9fc80bd5" =
        "framebuffer matched the C-BIOS MSX1 cartridge initialization diagnostic screen"
    "a4fd9e711efa94a8ddc758f939e66cf0d86ceb8ed90c888b1dffb3a034530aa3" =
        "framebuffer matched the C-BIOS MSX1 cartridge initialization diagnostic screen"
    # C-BIOS MSX2 cartridge initialization diagnostic/error screen. It is non-uniform,
    # so the framebuffer smoke check passes, but the cartridge did not boot.
    "e3a049bb139a79ae2aa46f249d19a4313b491de5b060828833a52f9ed0deecf0" =
        "framebuffer matched the C-BIOS cartridge initialization diagnostic screen"
    "19c38151896ffb8ce69589fa3aaafb1acaef97982d5c508faf979b4968f25247" =
        "framebuffer matched the C-BIOS cartridge initialization diagnostic screen"
    "dec855724fb2101cc65a59c6e99593fe3577279e79c11907d5c397f11caae536" =
        "framebuffer matched the C-BIOS cartridge initialization diagnostic screen"
    # C-BIOS MSX2 startup/logo screen reached after a cartridge was present but
    # not accepted by the firmware loader, for example after a wrong mapper guess.
    "c2198f7d31f9cbd17c54dbbb2d93c620af9ad9f5de2b49d68dc2487f8ffac4c2" =
        "framebuffer matched the C-BIOS MSX2 startup/logo screen"
}

function Get-InvalidBootHashReason {
    param([string]$Hash)
    if ([string]::IsNullOrWhiteSpace($Hash)) {
        return ""
    }
    $normalized = $Hash.Trim().ToLowerInvariant()
    if ($knownInvalidBootHashes.ContainsKey($normalized)) {
        return [string]$knownInvalidBootHashes[$normalized]
    }
    return ""
}

function Get-BootFailureReason {
    param(
        [string]$CaseName,
        [string]$Hash,
        [string]$BaselineHash
    )

    if ($CaseName -eq "firmware" -or [string]::IsNullOrWhiteSpace($Hash)) {
        return ""
    }
    if (-not [string]::IsNullOrWhiteSpace($BaselineHash) -and $Hash -eq $BaselineHash) {
        return "framebuffer matched firmware baseline; media did not visibly affect boot"
    }
    return Get-InvalidBootHashReason -Hash $Hash
}

function Get-CachedBaselineResult {
    param(
        [Parameter(Mandatory = $true)][string]$TestExe,
        [Parameter(Mandatory = $true)][pscustomobject]$Case,
        [Parameter(Mandatory = $true)][string]$OutDir,
        [Parameter(Mandatory = $true)][hashtable]$BaselineCache,
        [Parameter(Mandatory = $true)][int]$Index,
        [Parameter(Mandatory = $true)][string]$SafeCaseName
    )

    $baselineEnv = Copy-BaselineEnv -Case $Case
    $key = Get-BaselineCacheKey -System $Case.system -Env $baselineEnv
    if ($BaselineCache.ContainsKey($key)) {
        return $BaselineCache[$key]
    }

    $logPath = Join-Path $OutDir ("{0:D3}-{1}-baseline.log" -f $Index, $SafeCaseName)
    $baselineCase = [pscustomobject]@{
        system = $Case.system
        name = "firmware-baseline"
        env = $baselineEnv
        dataBacked = $Case.dataBacked
    }

    $exitCode = Invoke-BootTest -TestExe $TestExe -Case $baselineCase -LogPath $logPath
    $hash = Get-BootHashFromLog -LogPath $logPath
    $result = [pscustomobject]@{
        hash = $hash
        exit = $exitCode
        log = $logPath
    }
    $BaselineCache[$key] = $result
    return $result
}

$buildRoot = Resolve-RepoPath $BuildDir
$testExe = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "mnemos_msx_boot_test.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($testExe)) {
    throw "mnemos_msx_boot_test.exe not found under '$buildRoot'. Build mnemos_msx_boot_test first."
}

$msxBiosPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxBios "MNEMOS_MSX_BIOS") -Label "MSX BIOS"
$msxRomPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxRom "MNEMOS_MSX_ROM") -Label "MSX cartridge"
$msxRom2Path = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxRom2 "MNEMOS_MSX_ROM2" "MNEMOS_MSX_CART2") -Label "MSX second cartridge"
$msxRomDirPath = Resolve-ExistingDirectory -Path (Get-ConfiguredValue $MsxRomDir "MNEMOS_MSX_ROM_DIR") -Label "MSX cartridge corpus"
$msxDiskRomPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxDiskRom "MNEMOS_MSX_DISK_ROM") -Label "MSX disk ROM"
$msxDskPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxDsk "MNEMOS_MSX_DSK") -Label "MSX disk image"
$msxCasPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxCas "MNEMOS_MSX_CAS") -Label "MSX cassette image"
$msxKanjiPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxKanjiRom "MNEMOS_MSX_KANJI_ROM") -Label "MSX Kanji ROM"
$msxLogoRomPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $MsxLogoRom "MNEMOS_MSX_LOGO_ROM") -Label "MSX logo ROM"
$msxMapperValue = Get-ConfiguredValue $MsxMapper "MNEMOS_MSX_MAPPER"
$msxMapper2Value = Get-ConfiguredValue $MsxMapper2 "MNEMOS_MSX_MAPPER2"
$msxExpandedSlotsValue = Get-ConfiguredValue $MsxExpandedSlots "MNEMOS_MSX_EXPANDED_SLOTS"
$msxRamSlotValue = Get-ConfiguredValue $MsxRamSlot "MNEMOS_MSX_RAM_SLOT"
$msxDiskSlotValue = Get-ConfiguredValue $MsxDiskSlot "MNEMOS_MSX_DISK_SLOT"
$msxCart2SlotValue = Get-ConfiguredValue $MsxCart2Slot "MNEMOS_MSX_CART2_SLOT"
$msxRegionValue = Get-ConfiguredValue $MsxRegion "MNEMOS_MSX_REGION"
$msxBootKeysValue = Get-ConfiguredValue $MsxBootKeys "MNEMOS_MSX_BOOT_KEYS"
$msxHashValue = Get-ConfiguredValue $MsxBootSha256 "MNEMOS_MSX_BOOT_SHA256"

$msx2BiosPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2Bios "MNEMOS_MSX2_BIOS") -Label "MSX2 BIOS"
$msx2FirmwarePath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2Firmware "MNEMOS_MSX2_FIRMWARE") -Label "MSX2 firmware"
$msx2SubRomPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2SubRom "MNEMOS_MSX2_SUB_ROM" "MNEMOS_MSX2_SUBROM") -Label "MSX2 sub-ROM"
$msx2LogoRomPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2LogoRom "MNEMOS_MSX2_LOGO_ROM") -Label "MSX2 logo ROM"
$msx2RomPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2Rom "MNEMOS_MSX2_ROM") -Label "MSX2 cartridge"
$msx2Rom2Path = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2Rom2 "MNEMOS_MSX2_ROM2" "MNEMOS_MSX2_CART2") -Label "MSX2 second cartridge"
$msx2RomDirPath = Resolve-ExistingDirectory -Path (Get-ConfiguredValue $Msx2RomDir "MNEMOS_MSX2_ROM_DIR") -Label "MSX2 cartridge corpus"
$msx2DiskRomPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2DiskRom "MNEMOS_MSX2_DISK_ROM" "MNEMOS_MSX2_DISKROM") -Label "MSX2 disk ROM"
$msx2DskPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2Dsk "MNEMOS_MSX2_DSK") -Label "MSX2 disk image"
$msx2CasPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2Cas "MNEMOS_MSX2_CAS") -Label "MSX2 cassette image"
$msx2KanjiPath = Resolve-ExistingFile -Path (Get-ConfiguredValue $Msx2KanjiRom "MNEMOS_MSX2_KANJI_ROM") -Label "MSX2 Kanji ROM"
$msx2MapperValue = Get-ConfiguredValue $Msx2Mapper "MNEMOS_MSX2_MAPPER"
$msx2Mapper2Value = Get-ConfiguredValue $Msx2Mapper2 "MNEMOS_MSX2_MAPPER2"
$msx2ExpandedSlotsValue = Get-ConfiguredValue $Msx2ExpandedSlots "MNEMOS_MSX2_EXPANDED_SLOTS"
$msx2RamSlotValue = Get-ConfiguredValue $Msx2RamSlot "MNEMOS_MSX2_RAM_SLOT"
$msx2SubSlotValue = Get-ConfiguredValue $Msx2SubSlot "MNEMOS_MSX2_SUB_SLOT"
$msx2DiskSlotValue = Get-ConfiguredValue $Msx2DiskSlot "MNEMOS_MSX2_DISK_SLOT"
$msx2Cart2SlotValue = Get-ConfiguredValue $Msx2Cart2Slot "MNEMOS_MSX2_CART2_SLOT"
$msx2RamSizeValue = Get-ConfiguredValue $Msx2RamSize "MNEMOS_MSX2_RAM_SIZE"
$msx2RegionValue = Get-ConfiguredValue $Msx2Region "MNEMOS_MSX2_REGION"
$msx2BootKeysValue = Get-ConfiguredValue $Msx2BootKeys "MNEMOS_MSX2_BOOT_KEYS"
$msx2HashValue = Get-ConfiguredValue $Msx2BootSha256 "MNEMOS_MSX2_BOOT_SHA256"
$caseManifestPath = Get-ConfiguredValue $CaseManifest "MNEMOS_MSX_CASE_MANIFEST"
$romProfileManifestPath = Get-ConfiguredValue $RomProfileManifest "MNEMOS_MSX_ROM_PROFILE_MANIFEST"
$romProfiles = Import-RomProfileManifest -Path $romProfileManifestPath

$cases = [System.Collections.Generic.List[object]]::new()
$msxBaseEnv = $null
$msx2BaseEnv = $null

if (-not [string]::IsNullOrWhiteSpace($msxBiosPath)) {
    $baseEnv = @{
        MNEMOS_MSX_BIOS = $msxBiosPath
        MNEMOS_MSX_BOOT_FRAMES = $Frames.ToString()
    }
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX_DISK_ROM" -Path $msxDiskRomPath -Label "MSX disk ROM"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX_DSK" -Path $msxDskPath -Label "MSX disk image"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX_CAS" -Path $msxCasPath -Label "MSX cassette image"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX_KANJI_ROM" -Path $msxKanjiPath -Label "MSX Kanji ROM"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX_LOGO_ROM" -Path $msxLogoRomPath -Label "MSX logo ROM"
    if (-not [string]::IsNullOrWhiteSpace($msxRegionValue)) {
        $baseEnv["MNEMOS_MSX_REGION"] = $msxRegionValue
    }
    if (-not [string]::IsNullOrWhiteSpace($msxBootKeysValue)) {
        $baseEnv["MNEMOS_MSX_BOOT_KEYS"] = $msxBootKeysValue
    }
    if (-not [string]::IsNullOrWhiteSpace($msxMapperValue)) {
        $baseEnv["MNEMOS_MSX_MAPPER"] = $msxMapperValue
    }
    if (-not [string]::IsNullOrWhiteSpace($msxMapper2Value)) {
        $baseEnv["MNEMOS_MSX_MAPPER2"] = $msxMapper2Value
    }
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX_EXPANDED_SLOTS" -Value $msxExpandedSlotsValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX_RAM_SLOT" -Value $msxRamSlotValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX_DISK_SLOT" -Value $msxDiskSlotValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX_CART2_SLOT" -Value $msxCart2SlotValue
    if (-not [string]::IsNullOrWhiteSpace($msxHashValue)) {
        $baseEnv["MNEMOS_MSX_BOOT_SHA256"] = $msxHashValue
    }
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX_ROM2" -Path $msxRom2Path -Label "MSX second cartridge"
    $msxBaseEnv = @{} + $baseEnv

    $cases.Add((New-SmokeCase -System "msx" -Name "firmware" -Env $baseEnv))

    $roms = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($msxRomPath)) {
        $roms.Add($msxRomPath)
    }
    Add-RomDirectory -Paths $roms -Directory $msxRomDirPath
    $uniqueRoms = @($roms | Sort-Object -Unique)
    if ($SkipRoms -gt 0) {
        $uniqueRoms = @($uniqueRoms | Select-Object -Skip $SkipRoms)
    }
    if ($MaxRoms -gt 0) {
        $uniqueRoms = @($uniqueRoms | Select-Object -First $MaxRoms)
    }
    foreach ($rom in $uniqueRoms) {
        $case = New-RomSmokeCase -System "msx" -RomPath $rom -BaseEnv $baseEnv -Profiles $romProfiles
        if ($null -ne $case) {
            $cases.Add($case)
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($msx2FirmwarePath) -or -not [string]::IsNullOrWhiteSpace($msx2BiosPath)) {
    $baseEnv = @{
        MNEMOS_MSX2_BOOT_FRAMES = $Frames.ToString()
    }
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_FIRMWARE" -Path $msx2FirmwarePath -Label "MSX2 firmware"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_BIOS" -Path $msx2BiosPath -Label "MSX2 BIOS"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_SUB_ROM" -Path $msx2SubRomPath -Label "MSX2 sub-ROM"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_LOGO_ROM" -Path $msx2LogoRomPath -Label "MSX2 logo ROM"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_DISK_ROM" -Path $msx2DiskRomPath -Label "MSX2 disk ROM"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_DSK" -Path $msx2DskPath -Label "MSX2 disk image"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_CAS" -Path $msx2CasPath -Label "MSX2 cassette image"
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_KANJI_ROM" -Path $msx2KanjiPath -Label "MSX2 Kanji ROM"
    if (-not [string]::IsNullOrWhiteSpace($msx2RegionValue)) {
        $baseEnv["MNEMOS_MSX2_REGION"] = $msx2RegionValue
    }
    if (-not [string]::IsNullOrWhiteSpace($msx2BootKeysValue)) {
        $baseEnv["MNEMOS_MSX2_BOOT_KEYS"] = $msx2BootKeysValue
    }
    if (-not [string]::IsNullOrWhiteSpace($msx2MapperValue)) {
        $baseEnv["MNEMOS_MSX2_MAPPER"] = $msx2MapperValue
    }
    if (-not [string]::IsNullOrWhiteSpace($msx2Mapper2Value)) {
        $baseEnv["MNEMOS_MSX2_MAPPER2"] = $msx2Mapper2Value
    }
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX2_EXPANDED_SLOTS" -Value $msx2ExpandedSlotsValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX2_RAM_SLOT" -Value $msx2RamSlotValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX2_SUB_SLOT" -Value $msx2SubSlotValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX2_DISK_SLOT" -Value $msx2DiskSlotValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX2_CART2_SLOT" -Value $msx2Cart2SlotValue
    Add-OptionalEnvValue -Env $baseEnv -Name "MNEMOS_MSX2_RAM_SIZE" -Value $msx2RamSizeValue
    if (-not [string]::IsNullOrWhiteSpace($msx2HashValue)) {
        $baseEnv["MNEMOS_MSX2_BOOT_SHA256"] = $msx2HashValue
    }
    Add-OptionalEnvPath -Env $baseEnv -Name "MNEMOS_MSX2_ROM2" -Path $msx2Rom2Path -Label "MSX2 second cartridge"
    $msx2BaseEnv = @{} + $baseEnv

    $cases.Add((New-SmokeCase -System "msx2" -Name "firmware" -Env $baseEnv))

    $roms = [System.Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($msx2RomPath)) {
        $roms.Add($msx2RomPath)
    }
    Add-RomDirectory -Paths $roms -Directory $msx2RomDirPath
    $uniqueRoms = @($roms | Sort-Object -Unique)
    if ($SkipRoms -gt 0) {
        $uniqueRoms = @($uniqueRoms | Select-Object -Skip $SkipRoms)
    }
    if ($MaxRoms -gt 0) {
        $uniqueRoms = @($uniqueRoms | Select-Object -First $MaxRoms)
    }
    foreach ($rom in $uniqueRoms) {
        $case = New-RomSmokeCase -System "msx2" -RomPath $rom -BaseEnv $baseEnv -Profiles $romProfiles
        if ($null -ne $case) {
            $cases.Add($case)
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($caseManifestPath)) {
    $manifestCases = Import-CaseManifest -Path $caseManifestPath
    $manifestIndex = 0
    foreach ($manifestCase in $manifestCases) {
        $manifestIndex += 1
        $cases.Add((New-ManifestSmokeCase -Case $manifestCase -Index $manifestIndex -MsxBaseEnv $msxBaseEnv -Msx2BaseEnv $msx2BaseEnv))
    }
}

if ($cases.Count -eq 0) {
    $message = "No MSX/MSX2 firmware configured; set MNEMOS_MSX_BIOS or MNEMOS_MSX2_FIRMWARE/MSX2_BIOS to run this gate."
    if ($RequireData) {
        Write-Host $message -ForegroundColor Red
        exit 2
    }
    Write-Host $message -ForegroundColor DarkGray
    exit 0
}

if ($RequireData) {
    $missingDataCases = @($cases | Where-Object { -not $_.dataBacked })
    if ($missingDataCases.Count -gt 0) {
        Write-Host "MSX/MSX2 proof run contains cases without complete real firmware/media:" -ForegroundColor Red
        foreach ($case in $missingDataCases) {
            Write-Host ("  [{0}] {1}" -f $case.system, $case.name) -ForegroundColor Red
        }
        exit 2
    }
}

$stamp = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss-fff"), $PID
$outDir = Join-Path $repoRoot "build/scratch/msx-boot/$stamp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$results = [System.Collections.Generic.List[object]]::new()
$firmwareHashes = @{}
$baselineCache = @{}
$index = 0
foreach ($case in $cases) {
    $index += 1
    $caseName = Get-SafeName ("{0}-{1}" -f $case.system, $case.name)
    $logPath = Join-Path $outDir ("{0:D3}-{1}.log" -f $index, $caseName)

    Write-Host ("[{0}] {1}" -f $case.system, $case.name) -ForegroundColor Cyan
    $exitCode = Invoke-BootTest -TestExe $testExe -Case $case -LogPath $logPath
    $hash = Get-BootHashFromLog -LogPath $logPath
    $passed = $exitCode -eq 0
    $failureReason = ""
    $baselineHash = $null
    $baselineLog = ""
    $baselineExit = $null
    if ($case.name -ne "firmware") {
        $baselineResult = Get-CachedBaselineResult -TestExe $testExe -Case $case -OutDir $outDir -BaselineCache $baselineCache -Index $index -SafeCaseName $caseName
        $baselineHash = $baselineResult.hash
        $baselineLog = $baselineResult.log
        $baselineExit = $baselineResult.exit
    }

    $bootFailureReason = Get-BootFailureReason -CaseName $case.name -Hash $hash -BaselineHash $baselineHash
    if ($passed -and $case.name -ne "firmware" -and
        ($baselineExit -ne 0 -or [string]::IsNullOrWhiteSpace($baselineHash))) {
        $passed = $false
        $failureReason = "firmware baseline run failed or did not produce a framebuffer hash"
    } elseif ($passed -and -not [string]::IsNullOrWhiteSpace($bootFailureReason)) {
        $passed = $false
        $failureReason = $bootFailureReason
    }

    if ($passed -and $case.name -eq "firmware" -and -not [string]::IsNullOrWhiteSpace($hash)) {
        $firmwareHashes[$case.system] = $hash
        $baselineKey = Get-BaselineCacheKey -System $case.system -Env (Copy-BaselineEnv -Case $case)
        $baselineCache[$baselineKey] = [pscustomobject]@{
            hash = $hash
            exit = $exitCode
            log = $logPath
        }
    }

    $initialExitCode = $exitCode
    $initialHash = $hash
    $initialFailureReason = $failureReason
    $initialFrames = Get-CaseBootFrames -Case $case
    $initialLogPath = $logPath
    $retried = $false
    $retryExitCode = $null
    $retryHash = $null
    $retryFailureReason = ""
    $retryFrameCount = 0
    $retryLogPath = ""

    if (-not $passed -and $case.name -ne "firmware" -and $configuredRetryFrames -gt $initialFrames) {
        $retried = $true
        $retryFrameCount = $configuredRetryFrames
        $retryLogPath = Join-Path $outDir ("{0:D3}-{1}-retry-{2}.log" -f $index, $caseName, $configuredRetryFrames)

        Set-CaseBootFrames -Case $case -FrameCount $configuredRetryFrames
        Write-Host ("[{0}] {1} retry at {2} frames" -f $case.system, $case.name, $configuredRetryFrames) -ForegroundColor DarkCyan
        $retryExitCode = Invoke-BootTest -TestExe $testExe -Case $case -LogPath $retryLogPath
        $retryHash = Get-BootHashFromLog -LogPath $retryLogPath
        $exitCode = $retryExitCode
        $hash = $retryHash
        $passed = $exitCode -eq 0
        $failureReason = ""
        $baselineResult = Get-CachedBaselineResult -TestExe $testExe -Case $case -OutDir $outDir -BaselineCache $baselineCache -Index $index -SafeCaseName ("{0}-retry-{1}" -f $caseName, $configuredRetryFrames)
        $baselineHash = $baselineResult.hash
        $baselineLog = $baselineResult.log
        $baselineExit = $baselineResult.exit

        $bootFailureReason = Get-BootFailureReason -CaseName $case.name -Hash $hash -BaselineHash $baselineHash
        if ($passed -and ($baselineExit -ne 0 -or [string]::IsNullOrWhiteSpace($baselineHash))) {
            $passed = $false
            $failureReason = "firmware baseline run failed or did not produce a framebuffer hash"
        } elseif ($passed -and -not [string]::IsNullOrWhiteSpace($bootFailureReason)) {
            $passed = $false
            $failureReason = $bootFailureReason
        }

        $retryFailureReason = $failureReason
        if ($passed) {
            $failureReason = "passed after retry at $configuredRetryFrames frames"
        }
    }

    $results.Add([pscustomobject]@{
        system = $case.system
        name = $case.name
        exit = $exitCode
        passed = $passed
        dataBacked = [bool]$case.dataBacked
        hash = $hash
        baselineHash = $baselineHash
        baselineExit = $baselineExit
        baselineLog = $baselineLog
        failureReason = $failureReason
        log = $logPath
        frames = Get-CaseBootFrames -Case $case
        bootKeys = Get-CaseBootKeys -Case $case
        retried = $retried
        initialFrames = $initialFrames
        initialExit = $initialExitCode
        initialHash = $initialHash
        initialFailureReason = $initialFailureReason
        initialLog = $initialLogPath
        retryFrames = $retryFrameCount
        retryExit = $retryExitCode
        retryHash = $retryHash
        retryFailureReason = $retryFailureReason
        retryLog = $retryLogPath
    })
}

$summaryPath = Join-Path $outDir "summary.json"
$results | ConvertTo-Json -Depth 4 | Set-Content -Path $summaryPath -Encoding utf8

$failed = @($results | Where-Object { -not $_.passed })
Write-Host ("MSX/MSX2 boot smoke: {0}/{1} passed; summary: {2}" -f ($results.Count - $failed.Count), $results.Count, $summaryPath)
foreach ($row in $results) {
    if (-not [string]::IsNullOrWhiteSpace($row.hash)) {
        Write-Host ("  [{0}] {1} hash={2}" -f $row.system, $row.name, $row.hash)
    }
}
if ($failed.Count -gt 0) {
    foreach ($row in $failed) {
        $reason = if ([string]::IsNullOrWhiteSpace($row.failureReason)) { "" } else { " reason=" + $row.failureReason }
        $retry = if ($row.retried) { " retryLog=" + $row.retryLog } else { "" }
        Write-Host ("  [fail] {0}/{1} exit={2}{3} log={4}{5}" -f $row.system, $row.name, $row.exit, $reason, $row.log, $retry) -ForegroundColor Red
    }
    exit 1
}

exit 0
