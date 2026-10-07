@echo off
setlocal

pushd %~dp0

set Example=%1
if "%Example%"=="" set Example=blink

set StartupFile=src/startup_stm32f446retx.s
set LinkerScript=src/STM32F446RETX_FLASH.ld

set Cpu=-mcpu=cortex-m4 -mthumb -mfloat-abi=soft
set Warn=-Wall -Wextra
set Opt=-Os -g3 -ffunction-sections -fdata-sections
set Link=-T %LinkerScript% -Wl,--gc-sections -Wl,-Map=bin/%Example%.map -nostartfiles --specs=nano.specs --specs=nosys.specs

set Defines=
if "%3"=="noinherit" set Defines=-DREFTOS_PRIORITY_INHERITANCE_ENABLED=0

if not exist bin mkdir bin

arm-none-eabi-gcc %Cpu% %Warn% %Opt% %Defines% -Isrc examples/%Example%.c %StartupFile% %Link% -o bin/%Example%.elf
set BuildResult=%errorlevel%
if not %BuildResult%==0 goto Done

arm-none-eabi-size bin/%Example%.elf

if "%2"=="flash" openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program bin/%Example%.elf verify reset exit"

:Done
popd
exit /b %BuildResult%