# Haiku OS Codebase Guide for AI Coding Agents

## Project Overview
Haiku is an open-source operating system inspired by BeOS. The codebase spans kernel, bootloaders, system libraries, applications, and drivers. It targets multiple CPU architectures (x86, x86_64, ARM, ARM64, PPC, M68K, SPARC, RISCV64) with sophisticated multi-architecture build support.

---

# PART 1: BUILD SYSTEM & COMPILATION

## Critical Build System Knowledge

### Jam Build Tool (NOT Make)
- Haiku uses **Jam 2.5-haiku**, a custom build tool (not GNU Make)
- All build instructions are in Jamfiles (case-sensitive), not Makefiles
- Build rules defined in `build/jam/` directory
- Run builds from project root or `generated.*` directories

### Multi-Architecture Builds
- **Key variable**: `TARGET_PACKAGING_ARCH` (singular - current architecture)
- **Key variable**: `HAIKU_PACKAGING_ARCHS` (plural - all configured architectures)
- Architecture variables suffixed: `TARGET_GCC_x86`, `TARGET_DEFINES_x86_64`, etc.
- Configure supports x86_gcc2/x86 hybrid (legacy C++ ABI compatibility)
- Each architecture may need separate `.hpkg` (Haiku Package) files

### Common Jam Rules (NOT Functions)
- `Application <name> : <sources> : <libraries> : <resources>`
- `Addon <target> : <sources> : <libraries> : <isExecutable>`
- `StdBinCommands <sources> : <libs> : <resources>` - bulk application builder
- `LinkAgainst <target> : <libraries>` - add library dependencies
- `AddResources <target> : <resources>` - attach .rsrc files

## Directory Organization & Code Locations

```
src/kits/           → Public C++ APIs (libbe, libmedia, libgame, etc.)
src/libs/           → Static/shared libraries (non-API)
src/apps/           → User applications (terminal, tracker, debugger, etc.)
src/servers/        → System servers (app_server, input_server, net_server)
src/system/         → Core OS: kernel/, boot/, libroot, runtime_loader
src/add-ons/        → Driver and extension modules
src/preferences/    → Preference applications
src/bin/            → Command-line tools
src/tests/          → Test suites (mirror src/ structure)
headers/            → Public/private headers by category
build/              → Build infrastructure (jam/*, scripts/*)
data/               → System data files, configs, artwork
```

## Essential Workflows

### Configure & Build
```bash
# First-time setup (from project root):
./configure --cross-tools-source ../../buildtools --build-cross-tools x86_64

# Then build:
cd generated.x86_64 && jam -q @nightly-anyboot
# OR build individual component:
jam -q Debugger
```

### Building in Component Directory
```bash
cd src/apps/terminal
jam -q -sHAIKU_OUTPUT_DIR=/path/to/generated
```

### Hybrid Build (x86 + x86_gc2)
```bash
./configure --target-arch x86_gcc2 --target-arch x86
```

## Architecture-Specific Considerations

### Platform-Specific Code
- Platform code under `src/system/kernel/arch/<arch>/`
- Check `IsPlatformSupportedForTarget()` in Jamfiles to conditionally build
- Private arch headers: `headers/private/kernel/arch/<arch>/`

### Multi-Architecture Compilation
- Use `MultiArchSubDirSetup` in Jamfiles to iterate architectures
- Invoke rules inside `on $(architectureObject) { ... }` blocks
- Reference `$(TARGET_PACKAGING_ARCH)` for current architecture

## Package Management Insights

### .PackageInfo Files
- Defines package metadata, dependencies, provides, requires
- Parsed from `.PackageInfo` files in package recipes
- Tools: `package` command-line tool, `pkgman` package manager
- Architecture specificity: packages named `<name>-<version>-<arch>.hpkg`

### Build Packages
- Build packages go to `generated/build_packages/`
- Repository generation: `src/tools/generate_build_packages_repo.py`
- Dependencies resolved at build time via `packagefs` virtual filesystem

## Application Build & Glue Code

### How Applications Are Built
Jamfiles define applications using the `Application` rule:
```
Application MyApp : source1.cpp source2.cpp : library1 library2 : resources.rsrc ;
```
The rule automatically:
1. Compiles sources into `.o` files
2. Links against specified libraries and glue code
3. Attaches resources (icons, strings, etc.)
4. Creates final executable with proper entry point

### Glue Code System
**Purpose**: Bridges between ELF executable format and C++ runtime
- **Begin glue** (`LINK_BEGIN_GLUE`): Initializes runtime before `main()`
- **End glue** (`LINK_END_GLUE`): Calls static destructors after `main()` returns
- Located in `src/system/glue/` for each architecture
- Variables set per-architecture: `HAIKU_EXECUTABLE_BEGIN_GLUE_CODE_$(TARGET_PACKAGING_ARCH)`

### Key Build Variables
- `NEEDLIBS`: Libraries to link into target
- `LINKFLAGS`: Compiler flags (`-nostdlib`, `-Xlinker`, etc.)
- `TARGET_HAIKU_COMPATIBILITY_LIBS`: Legacy ABI libs for backwards compatibility
- `AddSharedObjectGlueCode`: Rule that injects glue and standard libraries

### Dependency & Library Linking
- `LinkAgainst <target> : <libs>`: Adds libraries to linker command
- `Depends <target> : <file>`: Creates build dependency
- Architecture-aware: `MultiArchDefaultGristFiles` filters libs per-architecture
- See `build/jam/MainBuildRules` for complete rule definitions

### Application Resource Integration
- Resources compiled from `.rdef` files into `.rsrc` binary format
- `AddResources <target> : <rsrc_file>` embeds resources in executable
- Resources accessed via `BResources` class at runtime
- Common resources: icons, application signatures, message catalogs

---

# PART 2: OS DEVELOPMENT & CODING PATTERNS

## Code Conventions & Patterns

### Copyright & License Headers
```cpp
/*
 * Copyright YEAR, Author Name <email>.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Name One <email>
 *		Name Two <email>
 */
```

### C++ API Patterns
- Use **full class namespaces**: `BPackageKit`, `BPrivate`, etc.
- **Status codes** via `status_t` return value (not exceptions)
- **B*-prefix** for public API classes (BeOS compatibility): `BApplication`, `BWindow`, `BMessage`
- **Private headers** under `headers/private/` parallel public `headers/`
- Use `BArchivable` for message serialization in APIs

### Package Architecture Constants
- Defined in `headers/os/package/PackageArchitecture.h`
- Valid values: `B_PACKAGE_ARCHITECTURE_X86`, `B_PACKAGE_ARCHITECTURE_X86_64`, `B_PACKAGE_ARCHITECTURE_ARM64`, etc.
- Architecture names: "x86", "x86_64", "x86_gcc2", "arm", "arm64", "ppc", "m68k", "sparc", "riscv64"

### Error Handling Pattern
```cpp
// Use status_t, not exceptions
status_t result = operation();
if (result != B_OK) {
    // Handle error - see OS.h for status codes
    return result;  // or B_ERROR, B_NO_MEMORY, etc.
}
```

## Common Constants for Development

### Status Codes (see `headers/os/support/Errors.h`)
- `B_OK` (0): Success
- `B_ERROR`: Generic error
- `B_NO_MEMORY`: Memory allocation failed
- `B_IO_ERROR`: I/O operation failed
- `B_PERMISSION_DENIED`: Insufficient privileges
- `B_BAD_VALUE`: Invalid parameter
- `B_BUSY`: Resource in use
- `B_NOT_SUPPORTED`: Operation not supported

### Message Codes (4-character constants)
- Standard format: `'ABCD'` creates uint32 with ASCII values
- App Kit codes: `B_MOUSE_DOWN`, `B_KEY_DOWN`, `B_WINDOW_ACTIVATED`
- Custom codes in messages: `enum { MSG_CUSTOM = 'MYCM' };`
- Reply required: `messenger.SendMessage(msg, reply_handler);`

### Type Codes (for typed data in messages/archives)
- `B_INT32_TYPE`, `B_INT64_TYPE`, `B_FLOAT_TYPE`, `B_DOUBLE_TYPE`
- `B_STRING_TYPE`, `B_MESSAGE_TYPE`, `B_MESSENGER_TYPE`
- `B_RECT_TYPE`, `B_POINT_TYPE`, `B_SIZE_TYPE`
- `B_RGB_COLOR_TYPE`: rgb_color struct (4 bytes: R, G, B, alpha)
- Custom types: `make_type(fourCC)` from `SupportDefs.h`

### Thread & Team Constants
- `B_NORMAL_PRIORITY`: Standard thread priority (100)
- `B_LOW_PRIORITY`, `B_HIGH_PRIORITY`: Relative to normal
- `B_SYSTEM_PRIORITY`: Kernel thread priority (max 200)
- `B_TEAM_* `: For team-wide operations (process groups)

### Semaphore & Port Constants
- Semaphore: `sem_id sem = create_sem(count, "name");`
- Port: `port_id port = create_port(msg_count, "name");`
- Area: `area_id area = create_area("name", &addr, B_ANY_ADDRESS, size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);`

### File System Constants (sys/stat.h)
- `S_IFREG`: Regular file
- `S_IFDIR`: Directory
- `S_IFLNK`: Symbolic link
- `S_IFIFO`: Named pipe
- Permissions: `S_IRUSR`, `S_IWUSR`, `S_IXUSR`, etc.

### Memory Protection Flags (areas)
- `B_READ_AREA`: Readable
- `B_WRITE_AREA`: Writable
- `B_EXECUTE_AREA`: Executable
- `B_STACK_AREA`: Designated stack
- `B_SWAPPABLE_AREA`: Can be swapped to disk
- `B_CLONEABLE_AREA`: Can be cloned by other teams

### Application Signatures & MIME Types
- Format: `application/x-vnd.<company>-<app>`
- Examples: `application/x-vnd.haiku-terminal`, `application/x-vnd.Be-TRAK`
- Registered with Registrar for app launching and icon lookup
- See `headers/private/app/RegistrarDefs.h` for protocol

### Color & Drawing Constants
- `rgb_color`: struct with red, green, blue, alpha (0-255 each)
- Named colors: `B_TRANSPARENT_BLACK`, `B_TRANSPARENT_WHITE`
- System colors: `ui_color()` function for theme-aware colors
- Drawing modes: `B_OP_COPY`, `B_OP_BLEND`, `B_OP_ADD`, `B_OP_SUBTRACT`, etc.

## Error Handling Pattern
```cpp
// Use status_t, not exceptions
status_t result = operation();
if (result != B_OK) {
    // Handle error - see OS.h for status codes
    return result;  // or B_ERROR, B_NO_MEMORY, etc.
}
```

## Testing Patterns

### Test Location & Structure
- Tests mirror source structure: `src/tests/kits/app/`, `src/tests/system/kernel/`
- Build with same Jamfile rules as apps
- Test apps often in subdirectory `testapps/`

### Common Test Patterns
```cpp
#include <Application.h> // Public API headers
int main() {
    BApplication app("application/x-vnd.test-app");
    // Test code...
    return 0;
}
```

## Kernel to Application Interaction

### System Call (Syscall) Interface
- Applications access kernel services via syscalls (e.g., `_kern_create_thread()`, `_kern_create_port()`)
- Syscalls defined in `headers/private/kernel/syscalls.h`
- Implemented in `src/system/libroot/` (userspace wrappers) and kernel
- BeOS-style naming convention: `_kern_*` for syscall names

### Kernel Architecture Overview
- **Kernel core**: `src/system/kernel/` - scheduling, memory management, IPC
- **Architecture-specific code**: `src/system/kernel/arch/<arch>/`
  - CPU initialization, interrupts, memory management per-arch
  - Bootloaders: `src/system/boot/` for each platform (BIOS, EFI, OpenFirmware, etc.)
- **File systems**: `src/add-ons/kernel/file_systems/` - ext2, bfs, packagefs, etc.
- **Drivers**: `src/add-ons/kernel/drivers/` - device drivers as kernel modules

### Key Kernel Services
- **Threading**: `_kern_spawn_thread()`, thread scheduling via `src/system/kernel/thread.cpp`
- **Ports/IPC**: `_kern_create_port()` - message queues for inter-process communication
- **Semaphores**: `_kern_create_sem()` - synchronization primitives
- **Areas/Memory**: `_kern_create_area()` - virtual memory management, shared memory
- **Device Manager**: `src/system/kernel/device_manager/` - hardware device enumeration
- **Module System**: Dynamic loadable kernel modules (drivers, filesystems)

### Kernel Initialization
- Entry point: `src/system/kernel/main.cpp` - initializes all kernel subsystems
- Stages:
  1. Architecture-specific init (CPU, MMU)
  2. VM system and allocators
  3. Threading system
  4. Device manager and drivers
  5. Module system
  6. Launch daemon to start system servers and user apps
- See `src/system/kernel/arch/<arch>/arch_vm.cpp` for architecture-specific VM setup

### How Apps Access Kernel Services
```cpp
// Example: Create a thread from userspace
thread_id tid = spawn_thread(thread_function, "my_thread", B_NORMAL_PRIORITY, NULL);
resume_thread(tid);  // Actually _kern_resume_thread() syscall underneath

// Create a semaphore for synchronization
sem_id sem = create_sem(1, "my_sem");
acquire_sem(sem);    // Block until available (_kern_acquire_sem syscall)

// Create a port for IPC
port_id port = create_port(8, "my_port");
write_port(port, MSG_CODE, buffer, size);
```

### Kernel Debug Features
- Kernel debugger (`src/system/kernel/debug/`) - triggered on kernel panic
- Module system supports debug output via `dprintf()`
- Architecture-specific debugging: `headers/private/kernel/arch/<arch>/`
- See `ReadMe.Compiling.md` for debug build options

## Cross-Component Communication

### Message-Based Architecture
Haiku uses **port-based messaging** for nearly all inter-process communication (IPC):
- `BMessage`: Data container with typed fields (void*, int32, string, etc.)
- `BMessenger`: Proxy object that sends messages to a target looper/port
- `BLooper`: Thread-safe message queue with dispatcher
- `BHandler`: Message receiver (apps, windows, views, custom handlers)

### Message Flow Pattern
```cpp
// Sender constructs & sends message
BMessage msg(CUSTOM_MESSAGE_CODE);
msg.AddInt32("value", 42);
BMessenger(target_handler).SendMessage(&msg);

// Receiver (in BHandler subclass)
void MyHandler::MessageReceived(BMessage* msg) {
    switch(msg->what) {
        case CUSTOM_MESSAGE_CODE:
            int32 value;
            msg->FindInt32("value", &value);
            break;
    }
}
```

### System Server Communication
Key servers and their communication patterns:
- **app_server**: GUI rendering, managed via `BApplication` connection
- **input_server**: Keyboard/mouse input (drivers post input events)
- **net_server**: Network operations
- **registrar**: Application signature registration and query
- **launch_daemon**: Process lifecycle management

Access via `BRoster`, `BApplication::GetServerPort()`, or direct port messaging.

### Registrar Protocol
- Applications register with MIME type as signature (e.g., `application/x-vnd.haiku-terminal`)
- Used for: app launching, recent apps tracking, icon lookup
- See `headers/private/app/RegistrarDefs.h` for message codes
- Example: `BRoster().Launch()` queries registrar to find executable

### Server Registration & Lifecycle
- System servers registered with Registrar daemon
- Launch via `LaunchDaemon` (see `src/servers/launch/`)
- Configuration in `data/launch/` directories
- Services can advertise capabilities via port names in registrar

### Common Message Codes (Constants)
- App Kit: `B_MOUSE_DOWN`, `B_KEY_DOWN`, `B_WINDOW_ACTIVATED`, etc.
- Custom codes: typically `'XX$$'` format (4-character codes, see `Message.h`)
- Reply messages: sender waits for response via `SendMessage(..., reply_required=true)`

### Nested Messages & Archives
- Messages can contain other messages (via `AddMessage()`)
- `BArchivable` interface for serializing objects to messages
- Used extensively: window layouts, view hierarchies, package metadata

## Best Practices When Modifying Code

1. **Preserve headers**: Don't remove copyright/license blocks
2. **Maintain Jam rules**: Don't change Jamfile patterns without understanding multi-arch impact
3. **Test architectures**: Hybrid builds (gcc2+gcc8) require special attention to glue code
4. **Check private APIs**: Public APIs in `headers/os/`, private in `headers/private/`
5. **Trace includes**: Public includes in `<>`, private in `""`
6. **Respect build variables**: Use architecture-aware variables like `TARGET_PACKAGING_ARCH` for conditional code

## Key Reference Files

**Build System:**
- `build/jam/MainBuildRules` - Core Jam rule definitions
- `build/jam/ArchitectureRules` - Multi-architecture build handling
- `docs/develop/build/jam.rst` - Jam design philosophy

**Kernel & System:**
- `src/system/kernel/main.cpp` - Kernel initialization walkthrough
- `src/system/kernel/arch/<arch>/arch_vm.cpp` - Architecture-specific VM setup
- `headers/private/kernel/syscalls.h` - Syscall interface definitions

**Applications & APIs:**
- `src/kits/app/Application.cpp` - Exemplar of large, well-organized source
- `src/system/glue/` - Glue code for each architecture
- `headers/os/support/Errors.h` - Status code definitions

**Package Management:**
- `docs/develop/packages/Migration.rst` - Multi-arch package migration guide
- `docs/develop/packages/BuildingPackages.rst` - Package format and creation

**Build & Compilation:**
- `ReadMe.Compiling.md` - Detailed build instructions and architecture overview
- `docs/develop/build/sourcecode.rst` - Source tree organization
