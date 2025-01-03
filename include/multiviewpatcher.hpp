#pragma once

#include <cstdint>

extern "C" int AddSPIRVMultiViewCapability(uint32_t* data, uint32_t size, uint32_t* data_out, uint32_t* size_out);
extern "C" int OptimizeSPIRV(uint32_t* data, uint32_t size, uint32_t* data_out, uint32_t* size_out);
extern "C" int ChangeSPIRVMultiViewDataAccessLocation(uint32_t* data, uint32_t size, uint32_t* data_out, uint32_t* size_out, uint32_t f_idx, uint32_t offset, int8_t optimize);
