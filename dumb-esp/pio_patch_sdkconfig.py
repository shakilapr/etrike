"""Force N16R8-safe RT settings into PlatformIO/ESP-IDF sdkconfig artifacts.

Root cause we hit on ESP32-S3-N16R8 modules:
  - Stale env sdkconfig kept CONFIG_FREERTOS_HZ=100
  - pdMS_TO_TICKS(5) then rounds to 0 → xTaskDelayUntil asserts and reboot-loops
  - PSRAM at 80 MHz often fails MSPI timing on these modules; prefer 40 MHz

This pre script runs every build and rewrites both:
  - project-level sdkconfig.<env> (if present)
  - build-dir config/sdkconfig.h (when already generated)
"""

Import("env")

import re
from pathlib import Path


def force_n16r8_text(content: str) -> tuple[str, int]:
    patches = 0
    new, n = re.subn(r"CONFIG_FREERTOS_HZ=\d+", "CONFIG_FREERTOS_HZ=1000", content)
    content, patches = new, patches + n
    new, n = re.subn(r"#define CONFIG_FREERTOS_HZ\s+\d+", "#define CONFIG_FREERTOS_HZ 1000", content)
    content, patches = new, patches + n

    new, n = re.subn(r"CONFIG_ESP_MAIN_TASK_STACK_SIZE=\d+", "CONFIG_ESP_MAIN_TASK_STACK_SIZE=6144", content)
    content, patches = new, patches + n
    new, n = re.subn(
        r"#define CONFIG_ESP_MAIN_TASK_STACK_SIZE\s+\d+",
        "#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 6144",
        content,
    )
    content, patches = new, patches + n

    # Prefer 40 MHz octal PSRAM
    if "CONFIG_SPIRAM_SPEED_40M=y" not in content and "CONFIG_SPIRAM_SPEED_40M 1" not in content:
        content2 = content
        content2 = re.sub(r"CONFIG_SPIRAM_SPEED_80M=y", "# CONFIG_SPIRAM_SPEED_80M is not set", content2)
        content2 = re.sub(r"# CONFIG_SPIRAM_SPEED_40M is not set", "CONFIG_SPIRAM_SPEED_40M=y", content2)
        content2 = re.sub(r"CONFIG_SPIRAM_SPEED=80", "CONFIG_SPIRAM_SPEED=40", content2)
        content2 = re.sub(r"#define CONFIG_SPIRAM_SPEED_80M\s+1", "/* CONFIG_SPIRAM_SPEED_80M 0 */", content2)
        content2 = re.sub(r"#define CONFIG_SPIRAM_SPEED\s+80", "#define CONFIG_SPIRAM_SPEED 40", content2)
        if "CONFIG_SPIRAM_SPEED_40M" not in content2 and "CONFIG_SPIRAM" in content2:
            content2 += "\nCONFIG_SPIRAM_SPEED_40M=y\n"
        if content2 != content:
            content = content2
            patches += 1

    # Never hard-abort boot on flaky memtest
    new, n = re.subn(r"CONFIG_SPIRAM_MEMTEST=y", "# CONFIG_SPIRAM_MEMTEST is not set", content)
    content, patches = new, patches + n
    new, n = re.subn(r"#define CONFIG_SPIRAM_MEMTEST\s+1", "/* CONFIG_SPIRAM_MEMTEST disabled */", content)
    content, patches = new, patches + n

    if "CONFIG_SPIRAM_IGNORE_NOTFOUND=y" not in content and "#define CONFIG_SPIRAM_IGNORE_NOTFOUND" not in content:
        if "CONFIG_SPIRAM" in content:
            if content.rstrip().endswith(".h") or "#define CONFIG_" in content:
                content = content.rstrip() + "\n#define CONFIG_SPIRAM_IGNORE_NOTFOUND 1\n"
            else:
                content = content.rstrip() + "\nCONFIG_SPIRAM_IGNORE_NOTFOUND=y\n"
            patches += 1

    return content, patches


project_dir = Path(env.subst("$PROJECT_DIR"))
pioenv = env.subst("$PIOENV")
candidates = [
    project_dir / f"sdkconfig.{pioenv}",
    project_dir / "sdkconfig.defaults",
    Path(env.subst("$BUILD_DIR")) / "config" / "sdkconfig.h",
    Path(env.subst("$BUILD_DIR")) / "sdkconfig",
]

for path in candidates:
    if not path.exists():
        print(f"[sdkconfig] skip missing {path}")
        continue
    original = path.read_text(encoding="utf-8", errors="replace")
    updated, n = force_n16r8_text(original)
    if n and updated != original:
        path.write_text(updated, encoding="utf-8")
        print(f"[sdkconfig] Applied {n} N16R8 patch(es) to {path}")
    else:
        print(f"[sdkconfig] OK (no change) {path}")
