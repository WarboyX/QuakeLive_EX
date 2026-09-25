/*
===========================================================================
[QL] E129. The PCI vendor:device IDs of this machine's GPUs, for the hardware
prompt - read without touching Vulkan, and without asking OpenGL either.

E127 classified the GPU by the name the OpenGL driver reports, and AMD's
Windows driver reports most integrated GPUs as the same generic "AMD
Radeon(TM) Graphics": a Vega 8, a 780M and a 610M all read alike. The device
ID tells them apart (the 780M is 1002:15BF), and cl_gpulist.h - generated from
the PCI ID database by tools/gen-gpu-list.py - says what each one can do.

  Linux    /sys/class/drm/cardN/device/{vendor,device}: plain file reads.
  Windows  DXGI's adapter list - the API every Windows program uses to list
           GPUs. dxgi.dll is loaded dynamically, software adapters (the Basic
           Render Driver) are skipped, nothing is created on any adapter.
  macOS    nothing; the name rules in CL_GuessGPU still apply.

Every GPU is returned, not just the one OpenGL is running on: on a laptop with
integrated and discrete graphics OpenGL may be on the integrated one, while the
Vulkan renderer picks the discrete one - which is the one worth asking about.
===========================================================================
*/

#include "client.h"
#include "cl_gpuid.h"

#if defined(_WIN32)
#define COBJMACROS
#include <windows.h>
#include <dxgi.h>

typedef HRESULT(WINAPI* createFactory1_t)(REFIID, void**);

// IID_IDXGIFactory1, spelled out so nothing has to link dxguid.
static const GUID qIID_IDXGIFactory1 = {
    0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};

int CL_ReadGpuIds(gpuId_t* out, int max) {
    HMODULE dll;
    createFactory1_t create;
    IDXGIFactory1* factory = NULL;
    IDXGIAdapter1* adapter;
    UINT i;
    int n = 0;

    dll = LoadLibraryA("dxgi.dll");
    if (!dll) {
        return 0;
    }
    create = (createFactory1_t)(void*)GetProcAddress(dll, "CreateDXGIFactory1");
    if (!create || FAILED(create(&qIID_IDXGIFactory1, (void**)&factory)) || !factory) {
        FreeLibrary(dll);
        return 0;
    }
    for (i = 0; n < max && IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;

        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc)) &&
            !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            out[n].vendor = (unsigned short)desc.VendorId;
            out[n].device = (unsigned short)desc.DeviceId;
            n++;
        }
        IDXGIAdapter1_Release(adapter);
    }
    IDXGIFactory1_Release(factory);
    FreeLibrary(dll);
    return n;
}

#elif defined(__linux__)
#include <stdio.h>

static qboolean CL_ReadHexFile(const char* path, unsigned* value) {
    FILE* f = fopen(path, "r");
    int ok;

    if (!f) {
        return qfalse;
    }
    ok = fscanf(f, "%x", value) == 1;
    fclose(f);
    return ok;
}

int CL_ReadGpuIds(gpuId_t* out, int max) {
    int card, n = 0, j;

    // cardN are the GPUs; cardN-HDMI-A-1 and friends are its connectors
    for (card = 0; card < 16 && n < max; card++) {
        unsigned vendor, device;
        qboolean dup = qfalse;

        if (!CL_ReadHexFile(va("/sys/class/drm/card%d/device/vendor", card), &vendor) ||
            !CL_ReadHexFile(va("/sys/class/drm/card%d/device/device", card), &device)) {
            continue;
        }
        for (j = 0; j < n; j++) {
            dup |= (out[j].vendor == vendor && out[j].device == device);
        }
        if (!dup) {
            out[n].vendor = (unsigned short)vendor;
            out[n].device = (unsigned short)device;
            n++;
        }
    }
    return n;
}

#else

int CL_ReadGpuIds(gpuId_t* out, int max) {
    (void)out;
    (void)max;
    return 0;
}

#endif
