# Dynamic ELF Loader on FreeRTOS

A microkernel-style system that dynamically loads and runs ELF relocatable
object files (`.o`) on a FreeRTOS-based STM32 board, with WiFi OTA deployment
and a browser-based management UI.

## Architecture

```
Browser                         STM32F407G-DISC1
  |  HTTP                       +------------------+
  v                             | FreeRTOS kernel  |
Flask bridge  --- TCP:8080 ---> | wifi serve       |
(server.py)                     |   +-- ELF loader |
                                |   +-- task mgr   |
                                |   +-- SD card    |
                                +------------------+
                                | ESP8266 (WiFi)   |
                                | SD card (SPI2)   |
                                +------------------+
```

## Features

- **Dynamic ELF loading**: Load ET_REL `.o` files at runtime. Supports
  the relocation types used by `apps/Makefile` (primarily R_ARM_ABS32 via
  `-mlong-calls`, plus limited THM_CALL/JUMP24/MOVW/MOVT handling).
  No reflashing required.
- **Multi-task management**: Up to 4 concurrent dynamic tasks with
  `task_create` / `task_kill` / `ps`. Race-free lifecycle via claim-slot
  pattern.
- **WiFi OTA transfer**: ESP8266 AT command interface over USART3+DMA.
  ACK-based flow control for reliable file upload (~1s for 1KB).
- **Browser UI**: Single-page app to list/run/delete SD-deployed apps,
  kill running tasks, and upload or build-and-deploy `.o` files.
- **SD card storage**: FatFs over SPI2 for persistent app storage.
- **UART shell**: Interactive command line for direct board control.

## Hardware

| Component | Connection |
|-----------|-----------|
| STM32F407G-DISC1 | Main board |
| ESP8266 (ESP-01) | USART3 (PD8 TX, PB11 RX), PB14 CH_PD, PB15 RST |
| SD card (SPI) | SPI2 (PC3 MOSI, PC2 MISO, PB13 SCK, PB1 CS) |
| UART shell | USART2 (PA2 TX, PA3 RX), 115200 baud |

## Prerequisites

- `arm-none-eabi-gcc` toolchain
- `make`
- `openocd` (for flashing via ST-Link)
- [uv](https://docs.astral.sh/uv/) (Python package manager)

Install uv if not already installed:

```bash
# macOS / Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
```

Verify:

```bash
uv --version
arm-none-eabi-gcc --version
```

## Quick Start

### 1. Build and flash firmware

```bash
# Build apps + firmware + flash (requires OpenOCD)
./flash.sh

# Or manually:
make -C apps          # Build demo .o apps + embedded headers
make -j8              # Build firmware
openocd -f board/stm32f4discovery.cfg \
    -c "program build/elf_loader.elf verify reset exit"
```

### 2. Connect via UART shell

```bash
# macOS
screen /dev/cu.usbmodem* 115200

# Linux
screen /dev/ttyACM0 115200
```

### 3. Start WiFi

```
> wifi init <SSID> <PASSWORD>
> wifi serve
```

SSID and password must not contain spaces or double quotes (shell
tokenizer and AT command limitation).

### 4. Start the web UI

```bash
cd scripts
uv sync
uv run server.py --board-host <BOARD_IP> --port 8000
```

`BOARD_IP` is shown in the `wifi init` output (AT+CIFSR response).
The PC running `server.py` and the ESP8266 must be on the same WiFi network.

Open `http://localhost:8000` in a browser.
Keep the board in `wifi serve` mode while using the web UI.

### 5. Deploy an app

From the web UI, enter `ledshow.c` in "Build & Deploy" and click Deploy.
Or upload a pre-built `.o` file directly.

## Writing Apps

Apps are compiled as relocatable `.o` files using the toolchain in `apps/Makefile`.
Each app must provide:

```c
#include "kernel.h"

void app_main(void *args)
{
    // Your app code (runs as a FreeRTOS task)
}

void app_cleanup(void)
{
    // Called on task_kill, turn off LEDs etc.
}
```

### Kernel API

| Function | Description |
|----------|-------------|
| `kernel_printf(fmt, ...)` | Print to UART shell |
| `kernel_gpio_write(pin, val)` | Write GPIO on GPIOD |
| `kernel_gpio_read(pin)` | Read GPIO on GPIOD |
| `kernel_delay_ms(ms)` | FreeRTOS-friendly delay |

### Build

```bash
cd apps
make ledshow.o    # Single app
make              # All apps
```

### LED Pins (GPIOD)

| LED | Pin | Define |
|-----|-----|--------|
| Green | PD12 | `0x1000` |
| Orange | PD13 | `0x2000` |
| Red | PD14 | `0x4000` |
| Blue | PD15 | `0x8000` |

## Shell Commands

| Command | Description |
|---------|-------------|
| `help` | List commands |
| `ls` | List available apps (embedded + SD) |
| `ll` | List all files on SD card |
| `run <name>` | Load and run an app |
| `ps` | Show running tasks |
| `kill <id>` | Kill a task |
| `rm <file>` | Delete file from SD card |
| `wifi init <SSID> <PASS>` | Connect to WiFi and start TCP server |
| `wifi serve` | Enter persistent serve mode (press any key to stop) |
| `wifi rxfile` | Receive a single file via WiFi |
| `wifi rxmsg` | Receive messages via WiFi |
| `exit` | Kill all tasks and reset |

## Project Structure

```
elf_loader/
  Core/Src/
    elf_loader.c      -- ELF parser, section loader, relocation engine
    task_manager.c    -- Dynamic task create/kill/ps
    kernel_api.c      -- Kernel API (printf, GPIO, delay) + symbol table
    wifi.c            -- ESP8266 driver, AT commands, serve mode
    shell.c           -- UART shell loop
    shell_cmds.c      -- Command implementations
    sd_card.c         -- FatFs SD card operations
  Core/Inc/
    app_config.h      -- Shared constants (APP_MAX_FILE_SIZE)
  apps/
    kernel.h          -- Header for dynamic apps
    Makefile          -- Cross-compiler flags (-mlong-calls)
    *.c               -- Demo apps (blink_green, ledshow, etc.)
  scripts/
    server.py         -- Flask bridge (browser <-> board)
    index.html        -- Web UI
    board_client.py   -- CLI test client
    wifi_send_file.py -- Standalone file sender
    wifi_send_msg.py  -- Standalone message sender
```
