<#
.SYNOPSIS
    Build all dlls of psqlodbc project using MSbuild.
.DESCRIPTION
    Build psqlodbc35w.dll, psqlodbc30a.dll, pgenlist.dll, pgenlista.dll
    and pgxalib.dll for both x86 and x64 platforms.
.PARAMETER Target
    Specify the target of MSBuild. "Build"(default), "Rebuild" or
    "Clean" is available.
.PARAMETER VCVersion
    Visual Studio version is determined automatically unless this
    option is specified.
.PARAMETER Platform
    Specify build platforms, "both"(default), "Win32" or "x64" is
    available.
.PARAMETER AlongWithInstallers
    Specify when you'd like build installers after building drivers.
.PARAMETER Toolset
    MSBuild PlatformToolset is determined automatically unless this
    option is specified. Currently "v100", "Windows7.1SDK", "v110",
    "v110_xp", "v120" or "v120_xp" is available.
.PARAMETER MSToolsVersion
    MSBuild ToolsVersion is detemrined automatically unless this
    option is specified.  Currently "4.0" or "12.0" is available.
.PARAMETER Configuration
    Specify "Release"(default) or "Debug".
.PARAMETER BuildConfigPath
    Specify the configuration xml file name if you want to use
    the configuration file other than standard one.
    The relative path is relative to the current directory.
.EXAMPLE
    > .\BuildAll
	Build with default or automatically selected parameters.
.EXAMPLE
    > .\BuildAll Clean
	Clean all generated files.
.EXAMPLE
    > .\BuildAll -V(CVersion) 11.0
	Build using Visual Studio 11.0 environment.
.EXAMPLE
    > .\BuildAll -P(latform) x64
	Build only 64bit dlls.
.EXAMPLE
    > .\BuildAll -A(longWithInstallers)
	Build installers as well after building drivers.
.NOTES
    Author: Hiroshi Inoue
    Date:   Febrary 1, 2014
#>

#
#	build 32bit & 64bit dlls for VC10 or later
#
Param(
[ValidateSet("Build", "Rebuild", "Clean", "info")]
[string]$Target="Build",
[string]$VCVersion,
[ValidateSet("Win32", "x64", "both")]
[string]$Platform="both",
[string]$Toolset,
[ValidateSet("", "4.0", "12.0")]
[string]$MSToolsVersion,
[ValidateSet("Debug", "Release")]
[String]$Configuration="Release",
[string]$BuildConfigPath,
[switch]$AlongWithInstallers
)

function buildPlatform($platinfo, $Platform)
{
	$LIBPQVER=$platinfo.libpq.version
	if ($LIBPQVER -eq "") {
		$LIBPQVER = $LIBPQ_VERSION
	}
	$PG_INC=$platinfo.libpq.include
	$PG_LIB=$platinfo.libpq.lib
	$BUILD_MACROS=$platinfo.build_macros
	if ($Platform -eq "x64") {
		if ($env:PROCESSOR_ARCHITECTURE -eq "x86") {
			$pgmfs = "$env:ProgramW6432"
		} else {
			$pgmfs = "$env:ProgramFiles"
		}
	} else {
		if ($env:PROCESSOR_ARCHITECTURE -eq "x86") {
			$pgmfs = "$env:ProgramFiles"
		} else {
			$pgmfs = "${env:ProgramFiles(x86)}"
		}
	}
	if ($PG_INC -eq "default") {
		if ($pgmfs -eq "") {
			PG_INC=""
		} else {
			$PG_INC = "$pgmfs\PostgreSQL\$LIBPQVER\include"
		}
	}
	if ($PG_LIB -eq "default") {
		if ($pgmfs -eq "") {
			PG_LIB=""
		} else {
			$PG_LIB = "$pgmfs\PostgreSQL\$LIBPQVER\lib"
		}
	}

	Write-Host "USE LIBPQ  : ($PG_INC $PG_LIB)"

	$MACROS=@"
/p:PG_LIB="$PG_LIB"``;PG_INC="$PG_INC"``"
"@
	if ($BUILD_MACROS -ne "") {
		$BUILD_MACROS = $BUILD_MACROS -replace ';', '`;'
		$BUILD_MACROS = $BUILD_MACROS -replace '"', '`"'
		$MACROS="$MACROS $BUILD_MACROS"
	}
	Write-Debug "MACROS in function = $MACROS"

	invoke-expression -Command "& `"${msbuildexe}`" ./platformbuild.vcxproj /tv:$MSToolsVersion /p:Platform=$Platform``;Configuration=$Configuration``;PlatformToolset=${Toolset} /t:$target /p:VisualStudioVersion=${VisualStudioVersion} /p:DRIVERVERSION=$DRIVERVERSION ${MACROS}"
}

$scriptPath = (Split-Path $MyInvocation.MyCommand.Path)
$configInfo = & "$scriptPath\configuration.ps1" "$BuildConfigPath"
$DRIVERVERSION=$configInfo.Configuration.version
pushd $scriptPath
$path_save = ${env:PATH}

$WSDK71Set="Windows7.1SDK"
$refnum=""
Write-Debug "VCVersion=$VCVersion $env:VisualStudioVersion"
#
#	Determine VisualStudioVersion
#
if ("$VCVersion" -eq "") {
	$VCVersion=$configInfo.Configuration.vcversion
}
$VisualStudioVersion=$VCVersion
if ("$VisualStudioVersion" -eq "") {
	$VisualStudioVersion = $env:VisualStudioVersion # VC11 or later version of C++ command prompt sets this variable
}
if ("$VisualStudioVersion" -eq "") {
	if ("${env:WindowsSDKVersionOverride}" -eq "v7.1") { # SDK7.1+ command prompt
		$VisualStudioVersion = "10.0"
	} elseif ("${env:VCInstallDir}" -ne "") { # C++ command prompt
		if ("${env:VCInstallDir}" -match "Visual Studio\s*(\S+)\\VC\\$") {
			$VisualStudioVersion = $matches[1]
		}
	}
}
#	neither C++ nor SDK prompt
if ("$VisualStudioVersion" -eq "") {
	if ("${env:VS100COMNTOOLS}" -ne "") { # VC10 or SDK 7.1 is installed (current official)
		$VisualStudioVersion = "10.0"
	} elseif ("${env:VS120COMNTOOLS}" -ne "") { # VC12 is installed
		$VisualStudioVersion = "12.0"
	} elseif ("${env:VS110COMNTOOLS}" -ne "") { # VC11 is installed
		$VisualStudioVersion = "11.0"
	} else {
		Write-Error "Visual Studio >= 10.0 not found" -Category InvalidArgument;return
	}
} elseif ([int]::TryParse($VisualStudioVersion, [ref]$refnum)) {
	$VisualStudioVersion="${refnum}.0"
}
#	Check VisualStudioVersion and prepare for ToolsVersion
switch ($VisualStudioVersion) {
 "10.0"	{ $tv = "4.0" }
 "11.0"	{ $tv = "4.0" }
 "12.0"	{ $tv = "12.0" }
 default { Write-Error "Selected Visual Stuidio is Version ${VisualStudioVersion}. Please use VC10 or later" -Category InvalidArgument; return }
}
#
#	Determine ToolsVersion
#
if ("$MSToolsVersion" -eq "") {
	$MSToolsVersion=$env:ToolsVersion
}
if ("$MSToolsVersion" -eq "") {
	 $MSToolsVersion = $tv
} elseif ([int]::TryParse($MSToolsVersion, [ref]$refnum)) {
	$MSToolsVersion="${refnum}.0"
}
#
#	Determine MSBuild executable
#
Write-Debug "ToolsVersion=$MSToolsVersion VisualStudioVersion=$VisualStudioVersion"
try {
	$msbver = invoke-expression "msbuild /ver /nologo"
	if ("$msbver" -match "^(\d+)\.(\d+)") {
		$major1 = [int] $matches[1]
		$minor1 = [int] $matches[2]
		if ($MSToolsVersion -match "^(\d+)\.(\d+)") {
			$bavail = $false
			$major2 = [int] $matches[1]
			$minor2 = [int] $matches[2]
			if ($major1 -gt $major2) {
				Write-Debug "$major1 > $major2"
				$bavail = $true
			}
			elseif ($major1 -eq $major2 -And $minor1 -ge $minor2) {
				Write-Debug "($major1, $minor1) >= ($major2, $minor2)"
				$bavail = $true
			}
			if ($bavail) {
				$msbuildexe = "MSBuild"
			}
		}
	}
} catch {}
if ("$msbuildexe" -eq "") {
	$msbindir=""
	$regKey="HKLM:\Software\Wow6432Node\Microsoft\MSBuild\ToolsVersions\${MSToolsVersion}"
	if (Test-Path -path $regkey) {
		$msbitem=Get-ItemProperty $regKey
		if ($msbitem -ne $null) {
			$msbindir=$msbitem.MSBuildToolsPath
		}
	} else {
		$regKey="HKLM:\Software\Microsoft\MSBuild\ToolsVersions\${MSToolsVersion}"
		if (Test-Path -path $regkey) {
			$msbitem=Get-ItemProperty $regKey
			if ($msbitem -ne $null) {
				$msbindir=$msbitem.MSBuildToolsPath
			}
		} else {
			Write-Error "MSBuild ToolsVersion $MSToolsVersion not Found" -Category NotInstalled; return
		}
	}
	$msbuildexe = "$msbindir\msbuild"
}
#
#	Determine PlatformToolset
#
if ("$Toolset" -eq "") {
	$Toolset=$configInfo.Configuration.toolset
}
if ("$Toolset" -eq "") {
	$Toolset=$env:PlatformToolset
}
if ("$Toolset" -eq "") {
	switch ($VisualStudioVersion) {
	 "10.0"	{
			if (Test-path "HKLM:\Software\Microsoft\Microsoft SDKs\Windows\v7.1") {
				$Toolset=$WSDK71Set
			} else {
				$Toolset="v100"
			}
		}
	 "11.0"	{$Toolset="v110_xp"}
	 "12.0"	{$Toolset="v120_xp"}
	}
}
#	avoid a bug of Windows7.1SDK PlatformToolset
if ($Toolset -eq $WSDK71Set) {
	$env:TARGET_CPU=""
}

$recordResult = $true
try {
#
#	build 32bit dlls
#
	if ($Platform -ieq "Win32" -or $Platform -ieq "both") {
		buildPlatform $configInfo.Configuration.x86 "Win32"
		if ($LastExitCode -ne 0) {
			$recordResult = $false
		}
	}
#
#	build 64bit dlls
#
	if ($Platform -ieq "x64" -or $Platform -ieq "both") {
		buildPlatform $configInfo.Configuration.x64 "x64"
		if ($LastExitCode -ne 0) {
			$recordResult = $false
		}
	}
#
#	Write the result to configuration xml
#
	$resultText="successful"
	if ($recordResult) {
		$configInfo.Configuration.BuildResult.Date=[string](Get-Date)
		$configInfo.Configuration.BuildResult.VisualStudioVersion=$VisualStudioVersion
		$configInfo.Configuration.BuildResult.PlatformToolset=$Toolset
		$configInfo.Configuration.BuildResult.ToolsVersion=$MSToolsVersion
		$configInfo.Configuration.BuildResult.Platform=$Platform
		SaveConfiguration $configInfo
	} else {
		$resultText="failed"
	} 
	Write-Host "ToolsVersion=$MSToolsVersion VisualStudioVersion=$VisualStudioVersion PlatformToolset=$Toolset Platform=$Platform $resultText`n"
#
#	build installers as well
#
	if ($AlongWithInstallers) {
		if (-not $recordResult) {
			throw("compilation failed")
		} 
                $cpu = $Platform
                if ($Platform -eq "win32") {
                        $cpu = "x86"
                }
                invoke-expression "..\installer\buildInstallers.ps1 -cpu $cpu -BuildConfigPath `"$BuildConfigPath`"" -ErrorAction Stop
                if ($LASTEXITCODE -ne 0) {
                        throw "Failed to build installers"
                }
	}
} catch {
	$error[0] | Format-List -Force
} finally {
	$env:PATH = $path_save
	popd
}
