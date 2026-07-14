"""Patch PlatformIO's generated sdkconfig for RT scheduler requirements."""

Import("env")

import re
from pathlib import Path


sdkconfig = Path(env.subst("$BUILD_DIR")) / "config" / "sdkconfig.h"
if sdkconfig.exists():
    content = sdkconfig.read_text()
    content, hz_patches = re.subn(
        r"#define CONFIG_FREERTOS_HZ\s+\d+",
        "#define CONFIG_FREERTOS_HZ 1000",
        content,
    )
    content, stack_patches = re.subn(
        r"#define CONFIG_ESP_MAIN_TASK_STACK_SIZE\s+\d+",
        "#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 6144",
        content,
    )
    patches = hz_patches + stack_patches
    if patches:
        sdkconfig.write_text(content)
        print(f"[sdkconfig] Applied {patches} patch(es) to {sdkconfig}")
    else:
        print(f"[sdkconfig] No patches needed for {sdkconfig}")
else:
    print(f"[sdkconfig] {sdkconfig} not found -- skipping patch")
