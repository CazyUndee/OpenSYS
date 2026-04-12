@echo off
setlocal enabledelayedexpansion

echo Building OpenSYS OS (64-bit)...
echo.

:: Create directories
if not exist obj mkdir obj
if not exist bin mkdir bin

:: Object files
set OBJS=

:: Assemble boot64
echo [1/12] Assembling boot64.asm...
nasm -f elf64 boot\boot64.asm -o obj\boot.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\boot.o

:: Assemble interrupt stubs
echo [2/12] Assembling interrupts.asm...
nasm -f elf64 boot\interrupts.asm -o obj\interrupts.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\interrupts.o

:: Compile 64-bit sources
echo [3/12] Compiling kernel64.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\kernel64.c -o obj\kernel64.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\kernel64.o

echo [4/12] Compiling pmm64.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\pmm64.c -o obj\pmm64.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\pmm64.o

echo [5/12] Compiling paging64.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\paging64.c -o obj\paging64.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\paging64.o

echo [6/12] Compiling kheap64.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\kheap64.c -o obj\kheap64.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\kheap64.o

echo [7/12] Compiling vga.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\vga.c -o obj\vga.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\vga.o

echo [8/12] Compiling io.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\io.c -o obj\io.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\io.o

echo [9/12] Compiling usb.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\usb.c -o obj\usb.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\usb.o

echo [10/12] Compiling hid_keyboard.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\hid_keyboard.c -o obj\hid_keyboard.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\hid_keyboard.o

echo [11/12] Compiling shell.c...
gcc -m64 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -mcmodel=large -c src\shell.c -o obj\shell.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\shell.o

:: Link kernel
echo [12/12] Linking kernel64.bin...
ld -m elf_x86_64 -T linker\linker64.ld -nostdlib -o bin\kernel64.bin !OBJS!
if errorlevel 1 goto error

echo.
echo ========================================
echo Build successful!
echo ========================================
echo.
echo Files: bin\kernel64.bin
echo Size:
for %%F in ("bin\kernel64.bin") do echo %%~zF bytes
echo.
echo To test: qemu-system-x86_64 -kernel bin\kernel64.bin
echo.
goto end

:error
echo.
echo ========================================
echo BUILD FAILED
echo ========================================
exit /b 1

:end
endlocal
