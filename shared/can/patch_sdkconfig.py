"""PlatformIO pre-build script — patches sdkconfig.h to set CONFIG_FREERTOS_HZ=1000.

ESP-IDF Kconfig defaults to 100 Hz, and sdkconfig.defaults is not reliably picked up
by PlatformIO's CMake wrapper. The -D CONFIG_FREERTOS_HZ=1000 build flag is overridden
by sdkconfig.h's unconditional #define. This script fixes it post-CMake-configure.
"""
Import("env")
from pathlib import Path
import re

pio_build_dir = Path(env.subst("$BUILD_DIR"))
sdkconfig = pio_build_dir / "config" / "sdkconfig.h"
if sdkconfig.exists():
    content = sdkconfig.read_text()
    patches = 0

    # CONFIG_FREERTOS_HZ: 100 → 1000 (100 Hz tick → 1 ms tick)
    new_content, n = re.subn(
        r'#define CONFIG_FREERTOS_HZ\s+\d+',
        '#define CONFIG_FREERTOS_HZ 1000',
        content
    )
    patches += n
    content = new_content

    # CONFIG_ESP_MAIN_TASK_STACK_SIZE: increase from 3584 → 6144
    new_content, n = re.subn(
        r'#define CONFIG_ESP_MAIN_TASK_STACK_SIZE\s+\d+',
        '#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 6144',
        content
    )
    patches += n
    content = new_content

    if patches:
        sdkconfig.write_text(content)
        print(f"[sdkconfig] Applied {patches} patch(es) to {sdkconfig}")
    else:
        print(f"[sdkconfig] No patches needed for {sdkconfig}")
else:
    print(f"[sdkconfig] {sdkconfig} not found -- skipping patch")
