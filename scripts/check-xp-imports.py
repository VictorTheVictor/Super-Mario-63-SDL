#!/usr/bin/env python3
"""Static Windows XP compatibility check for a PE executable.

Lists the DLLs and functions the exe imports (via MinGW objdump) and flags:
  - DLLs that do not exist on Windows XP,
  - well-known functions that only exist on Windows Vista or later,
  - a PE header that asks for a newer OS/subsystem version than XP (5.1; 5.2 for x64).
Functions SDL2 looks up at runtime (GetProcAddress) are not imports and are fine.

usage: scripts/check-xp-imports.py sm63.exe [objdump]
"""
import re
import subprocess
import sys

# DLLs that are not part of Windows XP.
NOT_ON_XP_DLLS = {
    'dwmapi.dll', 'd3d11.dll', 'd3d12.dll', 'dxgi.dll', 'd2d1.dll', 'dwrite.dll', 'xinput1_4.dll',
    'xinput9_1_0.dll', 'api-ms-win-core-synch-l1-2-0.dll', 'bcryptprimitives.dll', 'mfplat.dll',
    'propsys.dll', 'shcore.dll', 'ucrtbase.dll', 'vcruntime140.dll', 'powrprof.dll',
}
NOT_ON_XP_PREFIXES = ('api-ms-win-', 'ext-ms-win-')

# Functions introduced in Windows Vista or later (a non-exhaustive list of the ones C/C++
# runtimes, thread libraries and SDL commonly pull in).
VISTA_PLUS = set('''
GetTickCount64 InitializeConditionVariable SleepConditionVariableCS SleepConditionVariableSRW
WakeConditionVariable WakeAllConditionVariable InitializeSRWLock AcquireSRWLockExclusive
AcquireSRWLockShared ReleaseSRWLockExclusive ReleaseSRWLockShared TryAcquireSRWLockExclusive
TryAcquireSRWLockShared InitOnceExecuteOnce InitOnceBeginInitialize InitOnceComplete
InitOnceInitialize CreateEventExA CreateEventExW CreateMutexExA CreateMutexExW
CreateSemaphoreExA CreateSemaphoreExW InitializeCriticalSectionEx GetFileInformationByHandleEx
SetFileInformationByHandle GetFinalPathNameByHandleA GetFinalPathNameByHandleW
CreateSymbolicLinkA CreateSymbolicLinkW GetSystemTimePreciseAsFileTime CompareStringEx
LCMapStringEx GetLocaleInfoEx LocaleNameToLCID LCIDToLocaleName GetUserDefaultLocaleName
ResolveLocaleName QueryFullProcessImageNameA QueryFullProcessImageNameW GetErrorMode
SetThreadErrorMode CancelIoEx CreateFile2 WaitOnAddress WakeByAddressSingle WakeByAddressAll
FlsAlloc FlsFree FlsGetValue FlsSetValue GetCurrentProcessorNumber GetThreadId GetProcessId
RaiseFailFastException SetDefaultDllDirectories AddDllDirectory GetNumaNodeProcessorMaskEx
GetLogicalProcessorInformationEx GetActiveProcessorCount GetMaximumProcessorCount
SHGetKnownFolderPath SetProcessDPIAware SetProcessDpiAwareness SetProcessDpiAwarenessContext
GetDpiForWindow ChangeWindowMessageFilter ChangeWindowMessageFilterEx RegisterTouchWindow
GetTouchInputInfo CloseTouchInputHandle DwmFlush DwmIsCompositionEnabled
GetQueuedCompletionStatusEx SetFileCompletionNotificationModes GetOverlappedResultEx
QueryUnbiasedInterruptTime
'''.split())


def main():
    exe = sys.argv[1]
    objdump = sys.argv[2] if len(sys.argv) > 2 else 'x86_64-w64-mingw32-objdump'
    out = subprocess.run([objdump, '-p', exe], capture_output=True, text=True, check=True).stdout
    dll = None
    imports = {}
    for line in out.splitlines():
        m = re.match(r'\s*DLL Name: (\S+)', line)
        if m:
            dll = m.group(1).lower()
            imports.setdefault(dll, [])
            continue
        m = re.match(r'\s+[0-9a-f]+\s+(?:<none>|[0-9a-f]+)\s+[0-9a-f]+\s+(\S+)$', line)  # "vma  ordinal  hint  name"
        if dll and m:
            imports[dll].append(m.group(1))
    machine = re.search(r'Magic\s+(\w+)', out).group(1)  # 010b = PE32, 020b = PE32+
    is64 = machine == '020b'
    versions = {k: re.search(rf'{k}\s+(\d+)', out) for k in
                ('MajorOSystemVersion', 'MinorOSystemVersion', 'MajorSubsystemVersion', 'MinorSubsystemVersion')}
    os_v = (int(versions['MajorOSystemVersion'].group(1)), int(versions['MinorOSystemVersion'].group(1)))
    sub_v = (int(versions['MajorSubsystemVersion'].group(1)), int(versions['MinorSubsystemVersion'].group(1)))
    limit = (5, 2) if is64 else (5, 1)

    problems = []
    for name, funcs in sorted(imports.items()):
        if name in NOT_ON_XP_DLLS or name.startswith(NOT_ON_XP_PREFIXES):
            problems.append(f'DLL not on Windows XP: {name}')
        for f in funcs:
            if f in VISTA_PLUS:
                problems.append(f'{name}: {f} needs Windows Vista or later')
    for label, v in (('OS', os_v), ('subsystem', sub_v)):
        if v > limit:
            problems.append(f'PE header {label} version {v[0]}.{v[1]} is newer than XP ({limit[0]}.{limit[1]})')

    print(f'{exe}: {"64" if is64 else "32"}-bit, OS {os_v[0]}.{os_v[1]}, subsystem {sub_v[0]}.{sub_v[1]}')
    for name, funcs in sorted(imports.items()):
        print(f'  {name}: {len(funcs)} functions')
    if problems:
        print('NOT XP-compatible:')
        for p in problems:
            print('  ' + p)
        return 1
    print('no Windows Vista+ DLLs or known Vista+ functions imported')
    return 0


sys.exit(main())
