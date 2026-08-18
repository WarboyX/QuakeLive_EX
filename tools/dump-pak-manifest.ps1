<#
.SYNOPSIS
  Write a manifest of every file inside the game's .pk3 archives.

.DESCRIPTION
  Quake Live's pak00.pk3 is not redistributable and must never be committed or
  shipped. A *list of the names inside it* is not the content, and having that
  list in the repository settles a class of bug that has cost several rounds:
  an asset registered under a name the pak does not contain. RE_RegisterShader
  and RE_RegisterModel both return 0 for a name they cannot find, so a wrong
  name renders nothing and reports nothing.

  Example of what this is for: cgame registered three ice models for Freeze Tag
  under invented names. A scan showed pak00 has no ice meshes at all - the ice
  is a shader over the generic gib sphere. Ten seconds of manifest beats an
  afternoon of guessing.

  The manifest lists names only. No file contents are read or copied.

.PARAMETER BaseQ3
  The baseq3 folder holding the .pk3 files. This is the folder next to the
  executable (the basepath), NOT the one in AppData - AppData is fs_homepath,
  where the engine extracts the game modules at runtime.

.PARAMETER Output
  Where to write the manifest.

.EXAMPLE
  .\dump-pak-manifest.ps1 -BaseQ3 "C:\Users\Warboy\Downloads\quakelivewindowsx64\baseq3"

.EXAMPLE
  # just the ice/freeze entries, without writing a file
  .\dump-pak-manifest.ps1 -BaseQ3 "...\baseq3" -Output out.txt
  Select-String -Path out.txt -Pattern 'ice|freeze|frozen'
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BaseQ3,

    [string]$Output = "pak-manifest.txt"
)

Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not (Test-Path -LiteralPath $BaseQ3)) {
    throw "baseq3 folder not found: $BaseQ3"
}

$paks = Get-ChildItem -LiteralPath $BaseQ3 -Filter *.pk3 | Sort-Object Name
if (-not $paks) {
    throw "no .pk3 files in $BaseQ3"
}

# The game keeps its paks open while running, so work on a copy rather than
# telling anyone to close the game first.
$scratch = Join-Path $env:TEMP ("pakscan-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $scratch | Out-Null

$lines = New-Object System.Collections.Generic.List[string]
$total = 0

try {
    foreach ($pak in $paks) {
        $copy = Join-Path $scratch $pak.Name
        Copy-Item -LiteralPath $pak.FullName -Destination $copy -Force

        $zip = [IO.Compression.ZipFile]::OpenRead($copy)
        try {
            foreach ($entry in $zip.Entries) {
                if ($entry.FullName.EndsWith("/")) { continue }   # directories
                $lines.Add(("{0}`t{1}" -f $pak.Name, $entry.FullName))
                $total++
            }
        }
        finally {
            $zip.Dispose()
        }

        Write-Host ("{0,-16} {1,7} files" -f $pak.Name, $zip.Entries.Count)
    }

    $lines | Sort-Object | Set-Content -LiteralPath $Output -Encoding UTF8
    Write-Host ""
    Write-Host ("wrote {0} entries from {1} pak(s) to {2}" -f $total, $paks.Count, $Output)
    Write-Host "names only - no asset content is read or copied, so this is safe to share."
}
finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
