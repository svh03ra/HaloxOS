@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "MEM=16"
set "VGA=std"
set "ISO=build\HaloxOS-LiveCD_DEV.iso"

set "QEMU_BIN=qemu-system-i386.exe"
where %QEMU_BIN% >nul 2>&1
if not errorlevel 1 goto :found
for %%C in (
    "C:\Program Files\qemu\qemu-system-i386.exe"
    "C:\Program Files (x86)\qemu\qemu-system-i386.exe"
    "C:\qemu\qemu-system-i386.exe"
    "C:\msys64\mingw64\bin\qemu-system-i386.exe"
    "C:\msys64\usr\bin\qemu-system-i386.exe"
    "%USERPROFILE%\qemu\qemu-system-i386.exe"
) do (
    if not defined QEMU_SET if exist %%~C (
        set "QEMU_BIN=%%~C"
        set "QEMU_SET=1"
    )
)
if defined QEMU_SET goto :found
echo [ERROR] QEMU was not found on this Windows system.
echo         Install QEMU from https://qemu.weilnetz.de/w64/ then retry.
exit /b 1

:found
if not exist "%ISO%" (
    echo [ERROR] ISO not found: %ISO%
    echo         Build it first, e.g. run "make" or "tools\build.bat".
    exit /b 1
)

set "mem_input="
set /p mem_input="Set to Memory Size [default: 16]: "
if not defined mem_input set "mem_input=16"
set "mem=%mem_input%"
echo %mem%|findstr /r "^[1-9][0-9]*$" >nul
if errorlevel 1 (
    echo [ERROR] Invalid memory size: %mem%
    exit /b 1
)
if %mem% GTR 2048 (
    echo [ERROR] Memory size must be between 1 and 2048 MB
    exit /b 1
)

set "vga_input="
set /p vga_input="Set to VGA Type [std, qxl, vmware, virtio, cirrus, none] [default: std]: "
if not defined vga_input set "vga_input=std"
set "vga=%vga_input%"
echo %vga%|findstr /x "std qxl vmware virtio cirrus none" >nul
if errorlevel 1 (
    echo [ERROR] Unknown VGA type: %vga%
    echo Types: std, qxl, vmware, virtio, cirrus, none
    exit /b 1
)

echo [INFO] Launching QEMU: %mem% MB RAM, VGA %vga%
"%QEMU_BIN%" -cdrom "%ISO%" -m %mem% -vga %vga%
endlocal
