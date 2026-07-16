# How to Share Code Outside the Main Codebase Folder

When structuring a monorepo containing multiple distinct firmware projects (e.g., `rt-esp32`, `sys-esp32`), you often need to share code (like the `shared/` and `protocol/` folders) that exists *outside* of the individual project directories. 

Here is how you achieve this in PlatformIO and CMake without copying files or using symlinks.

## Method 1: PlatformIO (`platformio.ini`)

PlatformIO makes this extremely simple via `build_flags` and `lib_extra_dirs`. 

### For Header-Only Sharing (e.g., `shared/`)
If the shared folder only contains `.h` or `.hpp` files (like our `shared_config.h`), you just need to tell the compiler where to look for includes.

Add the following to your `platformio.ini`:
```ini
build_flags =
    -I ../shared
    -I ../protocol/generated/cpp
```
*Note: The path is relative to the directory containing the `platformio.ini` file. `-I` stands for Include.*

Now, in your `main.cpp`, you can simply do:
```cpp
#include "shared_config.h"
```

### For Sharing .cpp Files (Libraries)
If your shared folder contains `.cpp` files that need to be compiled and linked, you should treat it as an external library.

```ini
lib_extra_dirs =
    ../shared_libs
```
PlatformIO will scan `../shared_libs` for libraries (folders containing source code and a `library.json` or just include headers) and compile them into your project.

## Method 2: ESP-IDF CMake (`CMakeLists.txt`)

If you are using ESP-IDF with CMake, you must explicitly register the external directories as components or add them to the include directories of your main component.

### Adding an external Include Directory
In your project's `main/CMakeLists.txt`, append the external path to your `INCLUDE_DIRS`:

```cmake
idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "." "../../shared" "../../protocol/generated/cpp")
```

### Adding an external Component
If the external folder is a full ESP-IDF component (contains its own `CMakeLists.txt`), you can add it to the `EXTRA_COMPONENT_DIRS` in your project-level `CMakeLists.txt` (the one at the root of the project, not inside `main/`):

```cmake
cmake_minimum_required(VERSION 3.16)

# Tell CMake to look for components outside the project folder
set(EXTRA_COMPONENT_DIRS "../../shared_components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_esp_project)
```

## Summary
By using `-I` (Include paths) or registering external component directories, you can keep your `shared/` and `protocol/` logic completely isolated from the individual projects while guaranteeing that all MCUs compile with the exact same rules and configurations.
