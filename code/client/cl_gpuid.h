#ifndef CL_GPUID_H
#define CL_GPUID_H

// [QL] E129. See cl_gpuid.c.
typedef struct {
    unsigned short vendor, device;
} gpuId_t;

// Fills out[] with the PCI IDs of this machine's hardware GPUs; returns how
// many. 0 when the platform has no way to ask (macOS) or nothing answered.
int CL_ReadGpuIds(gpuId_t* out, int max);

#endif
