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
echo [1/9] Compiling boot_c.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c boot\boot_c.c -o obj\boot.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\boot.o

:: Assemble interrupt stubs
echo [2/9] Assembling interrupts.asm...
nasm -f elf32 boot\interrupts.asm -o obj\interrupts.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\interrupts.o

:: Compile kernel
echo [3/9] Compiling kernel.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\kernel.c -o obj\kernel.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\kernel.o

:: Compile VGA driver
echo [4/9] Compiling vga.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\vga.c -o obj\vga.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\vga.o

:: Compile GDT
echo [5/9] Compiling gdt.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\gdt.c -o obj\gdt.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\gdt.o

:: Compile IDT
echo [6/9] Compiling idt.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\idt.c -o obj\idt.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\idt.o

:: Compile interrupt handlers
echo [7/9] Compiling interrupt_handlers.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\interrupt_handlers.c -o obj\interrupt_handlers.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\interrupt_handlers.o

:: Compile keyboard driver
echo [8/9] Compiling keyboard.c...
gcc -m32 -ffreestanding -O0 -g -Wall -Wextra -fno-exceptions -nostdlib -fno-builtin -Iinclude -c src\keyboard.c -o obj\keyboard.o
if errorlevel 1 goto error
set OBJS=!OBJS! obj\keyboard.o

:: Link kernel
echo [9/9] Linking kernel.bin...
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
