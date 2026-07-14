"""Apply SYS timing settings after PlatformIO generates sdkconfig.h."""

Import("env")

import re
from pathlib import Path

sdkconfig = Path(env.subst("$BUILD_DIR")) / "config" / "sdkconfig.h"
if sdkconfig.exists():
    content = sdkconfig.read_text()
    content = re.sub(r"#define CONFIG_FREERTOS_HZ\s+\d+",
                     "#define CONFIG_FREERTOS_HZ 1000", content)
    content = re.sub(r"#define CONFIG_ESP_MAIN_TASK_STACK_SIZE\s+\d+",
                     "#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 6144", content)
    sdkconfig.write_text(content)
