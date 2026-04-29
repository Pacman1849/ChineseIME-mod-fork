#include <stdio.h>

// Define the DLL's interface
__declspec(dllexport) void hello() {
    printf("Hello, World!\n");
}