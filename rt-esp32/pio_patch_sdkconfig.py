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
    # N16R8: never abort boot on flaky SPIRAM memtest
    content2 = content
    content2 = re.sub(
        r"#define CONFIG_SPIRAM_MEMTEST\s+1",
        "/* CONFIG_SPIRAM_MEMTEST disabled for N16R8 boot reliability */",
        content2,
    )
    if "CONFIG_SPIRAM_IGNORE_NOTFOUND" not in content2:
        content2 += "\n#define CONFIG_SPIRAM_IGNORE_NOTFOUND 1\n"
    patches = hz_patches + stack_patches + (1 if content2 != content else 0)
    if patches:
        sdkconfig.write_text(content2)
        print(f"[sdkconfig] Applied {patches} patch(es) to {sdkconfig}")
    else:
        print(f"[sdkconfig] No patches needed for {sdkconfig}")
else:
    print(f"[sdkconfig] {sdkconfig} not found -- skipping patch")
