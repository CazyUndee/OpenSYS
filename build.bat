@echo off
setlocal enabledelayedexpansion

echo Building OpenCode OS...
echo.

:: Create directories
if not exist obj mkdir obj
if not exist bin mkdir bin

:: Object files
set OBJS=

:: Compile boot
echo [1/13] Compiling boot_c.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c boot\boot_c.c -o obj\boot.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\boot.o

:: Assemble interrupt stubs
echo [2/13] Assembling interrupts.asm...
nasm -f elf32 boot\interrupts.asm -o obj\interrupts.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\interrupts.o

:: Compile core sources
echo [3/13] Compiling gdt.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\gdt.c -o obj\gdt.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\gdt.o

echo [4/13] Compiling gdt_flush.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\gdt_flush.c -o obj\gdt_flush.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\gdt_flush.o

echo [5/13] Compiling idt.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\idt.c -o obj\idt.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\idt.o

echo [6/13] Compiling interrupt_handlers.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\interrupt_handlers.c -o obj\interrupt_handlers.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\interrupt_handlers.o

echo [7/13] Compiling io.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\io.c -o obj\io.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\io.o

echo [8/13] Compiling kernel.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\kernel.c -o obj\kernel.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\kernel.o

echo [9/13] Compiling keyboard.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\keyboard.c -o obj\keyboard.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\keyboard.o

echo [10/13] Compiling paging.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\paging.c -o obj\paging.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\paging.o

echo [11/13] Compiling pmm.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\pmm.c -o obj\pmm.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\pmm.o

echo [12/13] Compiling vga.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\vga.c -o obj\vga.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\vga.o

:: Link kernel
echo [13/13] Linking kernel.bin...
ld -m elf_i386 -T linker\linker.ld -nostdlib -o bin\kernel.bin !OBJS!
if errorlevel 1 goto error

echo.
echo ========================================
echo Build successful!
echo ========================================
echo.
echo Files: bin\kernel.bin
echo Size: 
for %%F in ("bin\kernel.bin") do echo %%~zF bytes
echo.
echo To test: qemu-system-i386 -kernel bin\kernel.bin
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
