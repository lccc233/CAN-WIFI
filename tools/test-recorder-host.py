"""Run the actual recorder on Windows using deterministic platform stubs.

Requires a native x86_64-capable clang and lld-link. The bundled Espressif
16.0.1 compiler has these; newer builds may only support MCU targets.
Pass --clang /path/to/clang.exe if needed. All build artifacts use a temp dir.
"""
import argparse
import ctypes
import pathlib
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--clang", default=None)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
candidates = [args.clang, shutil.which("clang"),
              r"C:\Espressif\tools\esp-clang\16.0.1-fe4f10a809\esp-clang\bin\clang.exe"]
clang = next((pathlib.Path(x) for x in candidates if x and pathlib.Path(x).is_file()), None)
if not clang:
    raise SystemExit("Native clang unavailable: pass --clang (this is not a passing/skipped test).")
linker = clang.parent / "lld-link.exe"
if not linker.is_file():
    raise SystemExit("lld-link.exe must be installed beside clang.")

headers = {
    "stdio.h": "#include <stddef.h>\nint snprintf(char *, size_t, const char *, ...);\n",
    "string.h": "#include <stddef.h>\nvoid *memcpy(void *,const void *,size_t);\nvoid *memset(void *,int,size_t);\nsize_t strlen(const char *);\nsize_t strnlen(const char *,size_t);\nint strcmp(const char *,const char *);\n",
    "ctype.h": "static inline int isxdigit(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }\n",
    "freertos/FreeRTOS.h": "#pragma once\n#include <stdint.h>\ntypedef void *SemaphoreHandle_t;\n#define portMAX_DELAY UINT32_MAX\n#define portTICK_PERIOD_MS 1\n",
    "freertos/semphr.h": "#include \"FreeRTOS.h\"\nSemaphoreHandle_t xSemaphoreCreateMutex(void);\nint xSemaphoreTake(SemaphoreHandle_t,uint32_t);\nint xSemaphoreGive(SemaphoreHandle_t);\n",
    "freertos/task.h": "#include \"FreeRTOS.h\"\nuint32_t xTaskGetTickCount(void);\n",
    "esp_err.h": "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 1\n#define ESP_ERR_NOT_SUPPORTED 2\n#define ESP_ERR_INVALID_ARG 3\n#define ESP_ERR_INVALID_STATE 4\n#define ESP_ERR_NOT_FOUND 5\n#define ESP_ERR_INVALID_SIZE 6\n",
    "esp_log.h": "#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n",
    "esp_heap_caps.h": "#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\nsize_t heap_caps_get_free_size(unsigned);\nsize_t heap_caps_get_largest_free_block(unsigned);\nvoid *heap_caps_malloc(size_t,unsigned);\n",
    "esp_random.h": "#include <stdint.h>\nuint32_t esp_random(void);\n",
    "driver/twai.h": "#pragma once\n#include <stdint.h>\n#include \"esp_err.h\"\n#include \"freertos/FreeRTOS.h\"\ntypedef struct {uint32_t rx_missed_count,rx_overrun_count;} twai_status_info_t;\nesp_err_t twai_get_status_info(twai_status_info_t *);\n",
}
labels = ["capacity retains all same-time frames; protected readers and stale sessions",
          "invalid arguments preserve source and copied configuration",
          "driver receive loss, quality uncertainty and duration at boot zero",
          "PSRAM allocation reserve and unavailable recorder",
          "frozen configuration and copied custom CAN ID list",
          "browser synchronization leases: final ACK, competing readers, bounded slots, renewal and expiry",
          "lost ACK response can be retried after record replacement without mutating new source"]
with tempfile.TemporaryDirectory(prefix="can-recorder-test-") as tmp:
    temp = pathlib.Path(tmp)
    for name, text in headers.items():
        path = temp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    obj, dll = temp / "recorder.obj", temp / "recorder.dll"
    subprocess.run([str(clang), "--target=x86_64-pc-windows-msvc", "-ffreestanding", "-fno-builtin",
                    "-fno-stack-protector", "-O1", "-I", str(temp), "-c",
                    str(root / "tools/recorder-host-harness.c"), "-o", str(obj)], check=True)
    subprocess.run([str(linker), "/dll", "/noentry", "/nodefaultlib", "/out:" + str(dll), str(obj)], check=True)
    lib = ctypes.CDLL(str(dll))
    test = lib.recorder_test_case
    test.argtypes, test.restype = [ctypes.c_int], ctypes.c_int
    try:
        for i, label in enumerate(labels):
            line = test(i)
            if line:
                raise AssertionError(f"FAIL {label}: recorder-host-harness.c:{line}")
            print("PASS " + label)
    finally:
        # ctypes retains DLLs until explicitly unloaded, preventing temporary cleanup.
        ctypes.windll.kernel32.FreeLibrary(ctypes.c_void_p(lib._handle))
print("Recorder host checks passed; actual CAN scheduling and WiFi stress require hardware.")
