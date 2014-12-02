<#
.SYNOPSIS
    Build all installers of psqlodbc project.
.DESCRIPTION
    Build psqlodbc_x86.msi(msm), psqlodbc_x64.msi(msm).
.PARAMETER cpu
    Specify build cpu type, "both"(default), "x86" or "x64" is
    available.
.PARAMETER AlongWithDrivers
    Specify when you'd like to build drivers before building installers.
.PARAMETER ExcludeRuntime
    Specify when you'd like to exclude a msvc runtime dll from the installer.
.PARAMETER BuildConfigPath
    Specify the configuration xml file name if you want to use
    the configuration file other than standard one.
    The relative path is relative to the current directory.
.EXAMPLE
    > .\buildInstallers
	Build 32bit and 64bit installers.
.EXAMPLE
    > .\buildInstallers x86
	Build 32bit installers.
.NOTES
    Author: Hiroshi Inoue
    Date:   July 4, 2014
#>
#	build 32bit and/or 64bit installers
#
Param(
[ValidateSet("x86", "x64", "both")]
[string]$cpu="both",
[switch]$AlongWithDrivers,
[switch]$ExcludeRuntime,
[string]$BuildConfigPath
)

function buildInstaller($CPUTYPE)
{
	$VERSION = $configInfo.Configuration.version

	$archinfo = $configInfo.Configuration.$CPUTYPE

	$LIBPQVER=$archinfo.libpq.version
	if ($LIBPQVER -eq "") {
		$LIBPQVER=$LIBPQ_VERSION
	}

	if ($CPUTYPE -eq "x64")
	{
		$LIBPQBINDIR=$archinfo.libpq.bin
		if ($LIBPQBINDIR -eq "default") {
			if ($env:PROCESSOR_ARCHITECTURE -ne "x86") {
				$pgmfs = "$env:ProgramFiles"
				$LIBPQBINDIR = "$pgmfs\PostgreSQL\$LIBPQVER\bin"
			}
			elseif ("${env:ProgramW6432}" -ne "") {
				$pgmfs = "$env:ProgramW6432"
				$LIBPQBINDIR = "$pgmfs\PostgreSQL\$LIBPQVER\bin"
			}
		}
	}
	elseif ($CPUTYPE -eq "x86")
	{
		$LIBPQBINDIR=$archinfo.libpq.bin
		if ($env:PROCESSOR_ARCHITECTURE -eq "x86") {
			$pgmfs = "$env:ProgramFiles"
		} else {
			$pgmfs = "${env:ProgramFiles(x86)}"
		}
		if ($LIBPQBINDIR -eq "default") {
			$LIBPQBINDIR = "$pgmfs\PostgreSQL\$LIBPQVER\bin"
		}
	}
	else
	{
		throw "Unknown CPU type $CPUTYPE";
	}
	# msvc runtime
	$MSVCRUNTIMEDLL = ""
	if (-not $ExcludeRuntime) {
		$toolset = $configInfo.Configuration.BuildResult.PlatformToolset
		if ($toolset -match "^v(\d+)0") {
			$runtime_version = $matches[1]
		} else {
			$runtime_version = "10"
		}
		# runtime dll required 
		$rt_dllname = "msvcr${runtime_version}0.dll"
		# where's the dll? 
		$pgmvc = $archinfo.runtime_folder
		if ("$pgmvc" -eq "") {
			if ($env:PROCESSOR_ARCHITECTURE -eq "x86") {
				$pgmvc = "$env:ProgramFiles"
			} else {
				$pgmvc = "${env:ProgramFiles(x86)}"
			}
			$dllinredist = "$pgmvc\Microsoft Visual Studio ${runtime_version}.0\VC\redist\${CPUTYPE}\Microsoft.VC${runtime_version}0.CRT\${rt_dllname}"
			if (Test-Path -Path $dllinredist) {
				$MSVCRUNTIMEDLL = $dllinredist
			} else {
				$messageSpec = "Please specify Configuration.$CPUTYPE.runtime_folder element of the configuration file where msvc runtime dll $rt_dllname can be found"
				if ($CPUTYPE -eq "x86") {
					if ($env:PROCESSOR_ARCHITECTURE -eq "x86") {
						$pgmvc = "${env:SystemRoot}\system32"
					} else {
						$pgmvc = "${env:SystemRoot}\syswow64"
					}
				} else {
					if ($env:PROCESSOR_ARCHITECTURE -eq "AMD64") {
						$pgmvc = "${env:SystemRoot}\system32"
					} elseif ($env:PROCESSOR_ARCHITEW6432 -eq "AMD64") {
						$pgmvc = "${env:SystemRoot}\sysnative"
					} else {
    						throw "${messageSpec}`n$dllinredist doesn't exist unfortunately"
					}
				}
				$dllinsystem = "${pgmvc}\${rt_dllname}"
				if (Test-Path -Path $dllinsystem) {
					$MSVCRUNTIMEDLL = $dllinsystem
				} else {
    					throw "${messageSpec}`nneither $dllinredist nor $dllinsystem exists unfortunately"
				}
			}
		} else {
			$dllspecified = "${pgmvc}\${rt_dllname}"
			if (Test-Path -Path $dllspecified) {
				$MSVCRUNTIMEDLL = $dllspecified
			} else {
    				throw "specified msvc runtime $dllspecified doesn't exist"
			} 
		}
	}

	Write-Host "CPUTYPE    : $CPUTYPE"
	Write-Host "VERSION    : $VERSION"
	Write-Host "LIBPQBINDIR: $LIBPQBINDIR"

	if ($env:WIX -ne "")
	{
		$wix = "$env:WIX"
		$env:Path += ";$WIX/bin"
	}
	# The subdirectory to install into
	$SUBLOC=$VERSION.substring(0, 2) + $VERSION.substring(3, 2)

	if (-not(Test-Path -Path $CPUTYPE)) {
    		New-Item -ItemType directory -Path $CPUTYPE | Out-Null
	}

	$PRODUCTCODE = [GUID]::NewGuid();
	Write-Host "PRODUCTCODE: $PRODUCTCODE"

	try {
		pushd "$scriptPath"

		Write-Host ".`nBuilding psqlODBC/$SUBLOC merge module..."

		invoke-expression "candle -nologo -dPlatform=$CPUTYPE `"-dVERSION=$VERSION`" -dSUBLOC=$SUBLOC `"-dLIBPQBINDIR=$LIBPQBINDIR`" `"-dGSSBINDIR=$GSSBINDIR`" `"-dMSVCRUNTIMEDLL=$MSVCRUNTIMEDLL`" -o $CPUTYPE\psqlodbcm.wixobj psqlodbcm_cpu.wxs"
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to build merge module"
		}

		Write-Host ".`nLinking psqlODBC merge module..."
		invoke-expression "light -nologo -o $CPUTYPE\psqlodbc_$CPUTYPE.msm $CPUTYPE\psqlodbcm.wixobj"
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to link merge module"
		}

		Write-Host ".`nBuilding psqlODBC installer database..."

		invoke-expression "candle -nologo -dPlatform=$CPUTYPE `"-dVERSION=$VERSION`" -dSUBLOC=$SUBLOC `"-dPRODUCTCODE=$PRODUCTCODE`" -o $CPUTYPE\psqlodbc.wixobj psqlodbc_cpu.wxs"
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to build installer database"
		}

		Write-Host ".`nLinking psqlODBC installer database..."
		invoke-expression "light -nologo -ext WixUIExtension -cultures:en-us -o $CPUTYPE\psqlodbc_$CPUTYPE.msi $CPUTYPE\psqlodbc.wixobj"
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to link installer database"
		}

		Write-Host ".`nModifying psqlODBC installer database..."
		invoke-expression "cscript modify_msi.vbs $CPUTYPE\psqlodbc_$CPUTYPE.msi"
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to modify installer database"
		}

		Write-Host ".`nDone!`n"
	}
	catch [Exception] {
		Write-Host ".`Aborting build!"
		throw $error[0]
	}
	finally {
		popd
	}
}

$scriptPath = (Split-Path $MyInvocation.MyCommand.Path)
$configInfo = & "$scriptPath\..\winbuild\configuration.ps1" "$BuildConfigPath"

if ($AlongWithDrivers) {
	try {
		pushd "$scriptpath"
		$platform = $cpu
		if ($cpu -eq "x86") {
			$platform = "win32"
		}
		invoke-expression "..\winbuild\BuildAll.ps1 -Platform $platform -BuildConfigPath `"$BuildConfigPath`"" -ErrorAction Stop
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to build binaries"
		}
	} catch [Exception] {
		throw $error[0]
	} finally {
		popd
	} 
}

if ($cpu -eq "both") {
	buildInstaller "x86"
	buildInstaller "x64"
	$VERSION = $configInfo.Configuration.version
	try {
		pushd "$scriptPath"
		invoke-expression "psqlodbc-setup\buildBootstrapper.ps1 -version $VERSION" -ErrorAction Stop
		if ($LASTEXITCODE -ne 0) {
			throw "Failed to build bootstrapper"
		}
	} catch [Exception] {
		throw $error[0]
	} finally {
		popd
	} 
}
else {
	buildInstaller $cpu
}
