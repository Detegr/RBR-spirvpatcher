#include <format>
#include <optional>
#include <ranges>
#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>
#include <sstream>

#include <iostream>

#ifdef _WIN32
    #define EXPORT __declspec(dllexport)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #define EXPORT
#endif

enum ShaderType {
    VS,
    FF_VS,
    BTB,
};

constexpr auto find_it = [](auto& it, const auto& needle) {
    return std::find_if(it.begin(), it.end(), [&needle](const std::string& x) {
        return x.contains(needle);
    });
};

constexpr auto find_idx = [](auto& it, const auto& needle) {
    return std::distance(it.begin(), find_it(it, needle));
};

ShaderType detectType(const std::vector<std::string>& a)
{
    const auto ver_idx = find_idx(a, "OpString");
    const auto ver = a[ver_idx];
    if (ver.contains("OpString \"VS_")) {
        return VS;
    } else if (ver.contains("OpString \"FF_VS")) {
        return FF_VS;
    } else {
        return BTB;
    }
}

static std::optional<std::vector<std::string>> disassembleShader(const spvtools::SpirvTools& t, const std::vector<uint32_t> spv)
{
    std::string as;
    if (!t.Disassemble(spv, &as,
            SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES)) {
        return std::nullopt;
    }

    std::vector<std::string> a;
    std::stringstream sas(as);
    std::string l;

    while (std::getline(sas, l, '\n')) {
        if (l[0] == ';') {
            // Skip comments
            continue;
        }
        a.push_back(l);
    }

    return a;
}

static bool addMultiViewCapability(std::vector<std::string>& a)
{
    if (a.front() == "OpCapability MultiView") {
        #ifdef _WIN32
            OutputDebugStringA("MultiView capability found, not attempting to patch multiple times");
        #endif
        return false;
    }

    a.insert(a.begin(), "OpCapability MultiView");

    return true;
}

static void patchEntryPoint(std::vector<std::string>& a, bool includeViewIndex = true)
{
    // Collect all OpVariables before first OpFunction and put them in
    // OpEntryPoint list.
    // SPIR-V validator requires all used variables to be present in OpEntryPoint
    // DXVK seems to generate shaders that don't have all of them. I don't know if
    // it matters or not, but better do things in a valid way while we can.
    const auto fun_idx = find_idx(a, " = OpFunction");
    auto vars = a | std::views::filter([](const std::string& x) {
        return x.contains("= OpVariable");
    }) | std::views::transform([](const std::string& x) {
        return x.substr(0, x.find("="));
    });
    auto& entry = a[find_idx(a, "OpEntryPoint")];
    entry = std::format("OpEntryPoint Vertex %main \"main\" {} ", includeViewIndex ? "%ViewIndex" : "");
    for (const auto& var : vars) {
        entry += var;
    }
}

static void patchMatrixAccesses(std::vector<std::string>& a, uint32_t f_idx, uint32_t offset)
{
    static int i = 0;

    a.insert(find_it(a, "OpDecorate"), "OpDecorate %ViewIndex BuiltIn ViewIndex");

    // Calculate offsets to the given matrix
    // c_idx is the index in c.f float constant array that's provided to the shader
    // offset is the offset to the c.f array where the data for other views start

    const auto fun_idx = find_idx(a, " = OpLabel") - 2;
    // Define ViewIndex variable and shader_data_begin constant
    a.insert(a.begin() + fun_idx, "%ptr = OpTypePointer Input %uint");
    a.insert(a.begin() + fun_idx + 1, "%ViewIndex = OpVariable %ptr Input");
    a.insert(a.begin() + fun_idx + 2, std::format("%shader_data_begin = OpConstant %uint {}", offset));
    a.insert(a.begin() + fun_idx + 3, std::format("%f_idx = OpConstant %uint {}", f_idx));

    const auto code_idx = find_idx(a, " = OpLabel");
    a.insert(a.begin() + code_idx + 1, "%vi = OpLoad %uint %ViewIndex");
    a.insert(a.begin() + code_idx + 2, "%view_offset = OpIMul %uint %vi %uint_4");
    a.insert(a.begin() + code_idx + 3, "%data_offset = OpIAdd %uint %shader_data_begin %view_offset");
    if (f_idx > 0) {
        a.insert(a.begin() + code_idx + 4, std::format("%fadd_0 = OpIAdd %uint %uint_0 %f_idx", f_idx));
        a.insert(a.begin() + code_idx + 5, std::format("%fadd_1 = OpIAdd %uint %uint_1 %f_idx", f_idx));
        a.insert(a.begin() + code_idx + 6, std::format("%fadd_2 = OpIAdd %uint %uint_2 %f_idx", f_idx));
        a.insert(a.begin() + code_idx + 7, std::format("%fadd_3 = OpIAdd %uint %uint_3 %f_idx", f_idx));
        a.insert(a.begin() + code_idx + 8, std::format("%i_f{}_0 = OpIAdd %uint %data_offset %fadd_0", f_idx));
        a.insert(a.begin() + code_idx + 9, std::format("%i_f{}_1 = OpIAdd %uint %data_offset %fadd_1", f_idx));
        a.insert(a.begin() + code_idx + 10, std::format("%i_f{}_2 = OpIAdd %uint %data_offset %fadd_2", f_idx));
        a.insert(a.begin() + code_idx + 11, std::format("%i_f{}_3 = OpIAdd %uint %data_offset %fadd_3", f_idx));
    } else {
        a.insert(a.begin() + code_idx + 4, std::format("%i_f{}_0 = OpIAdd %uint %data_offset %uint_0", f_idx));
        a.insert(a.begin() + code_idx + 5, std::format("%i_f{}_1 = OpIAdd %uint %data_offset %uint_1", f_idx));
        a.insert(a.begin() + code_idx + 6, std::format("%i_f{}_2 = OpIAdd %uint %data_offset %uint_2", f_idx));
        a.insert(a.begin() + code_idx + 7, std::format("%i_f{}_3 = OpIAdd %uint %data_offset %uint_3", f_idx));
    }

    // Patch accesses to the matrix data to the shifted location
    for (int i = 0; i < 4; ++i) {
        auto use = a.begin();
        while (use != a.end()) {
            use = std::find_if(use, a.end(), [f_idx, i](const auto& x) {
                return x.ends_with(std::format(
                    "OpAccessChain %_ptr_Uniform_v4float %c %uint_1 %int_{}", i + f_idx));
            });
            if (use != a.end()) {
                auto& x = a[std::distance(a.begin(), use)];
                const auto begin = x.substr(0, x.find("="));
                x = std::format(
                    "{}= OpAccessChain %_ptr_Uniform_v4float %c %uint_1 %i_f{}_{}", begin, f_idx, i);
            }
        }
    }
}

static void patchVertexShader(std::vector<std::string>& a, uint32_t f_idx, uint32_t offset)
{
    if (addMultiViewCapability(a)) {
        patchEntryPoint(a);
        patchMatrixAccesses(a, f_idx, offset);
    }
}

extern "C" EXPORT int OptimizeSPIRV(uint32_t* data, uint32_t size, uint32_t* data_out, uint32_t* size_out)
{
    std::vector<uint32_t> in;
    std::vector<uint32_t> optimized;

    in.assign(data, data + size);
    spvtools::Optimizer opt(SPV_ENV_VULKAN_1_3);
    opt.RegisterPerformancePasses();
    if (!opt.Run(data, size, &optimized)) {
        return -1;
    }
    if (data_out) {
        for (int i = 0; i < optimized.size(); ++i) {
            data_out[i] = optimized[i];
        }
    }
    *size_out = optimized.size();

    return 0;
}

extern "C" EXPORT int AddSPIRVMultiViewCapability(uint32_t* data, uint32_t size, uint32_t* data_out, uint32_t* size_out)
{
    std::vector<uint32_t> in;
    in.assign(data, data + size);

    spvtools::SpirvTools t(SPV_ENV_VULKAN_1_3);
    t.SetMessageConsumer([](spv_message_level_t, const char*, const spv_position_t& position, const char* message) {
        #ifdef _WIN32
            OutputDebugStringA(std::format("SPIRV-Tools: {}:{}: {}", position.line, position.column, message).c_str());
         #else
            std::cout << std::format("SPIRV-Tools: {}:{}: {}", position.line, position.column, message) << std::endl;;
         #endif
    });

    auto disassembled = disassembleShader(t, in);
    if (!disassembled) {
        return -1;
    }

    auto a = disassembled.value();
    const auto typ = detectType(a);

    if (typ == VS) {
        if (addMultiViewCapability(a)) {
            patchEntryPoint(a, false);

            std::ostringstream as;
            for (const auto& l : a) {
                as << l << "\n";
            }

            std::vector<uint32_t> out;
            t.Assemble(as.str(), &out);

            if (!t.Validate(out)) {
                return -1;
            }

            if (data_out) {
                for (int i = 0; i < out.size(); ++i) {
                    data_out[i] = out[i];
                }
            }
            *size_out = out.size();

            return 0;
        }
    } else {
        return -1;
    }
}

extern "C" EXPORT int ChangeSPIRVMultiViewDataAccessLocation(uint32_t* data, uint32_t size, uint32_t* data_out, uint32_t* size_out, uint32_t f_idx, uint32_t offset, int8_t optimize)
{
    std::vector<uint32_t> in;
    in.assign(data, data + size);

    spvtools::SpirvTools t(SPV_ENV_VULKAN_1_3);
    t.SetMessageConsumer([](spv_message_level_t, const char*, const spv_position_t& position, const char* message) {
        #ifdef _WIN32
            OutputDebugStringA(std::format("SPIRV-Tools: {}:{}: {}", position.line, position.column, message).c_str());
         #else
            std::cout << std::format("SPIRV-Tools: {}:{}: {}", position.line, position.column, message) << std::endl;;
         #endif
    });

    auto disassembled = disassembleShader(t, in);
    if (!disassembled) {
        return -1;
    }

    auto a = disassembled.value();
    const auto typ = detectType(a);

    if (typ == FF_VS) {
        // Pass through, nothing to do here as the modifications are done on DXVK side
        // Just run the optimizer if requested
        std::vector<uint32_t> optimized;
        std::vector<uint32_t>& outvec = in;
        if (optimize != 0) {
            spvtools::Optimizer opt(SPV_ENV_VULKAN_1_3);
            opt.RegisterPerformancePasses();
            if (!opt.Run(data, size, &optimized)) {
                return -1;
            }
            outvec = optimized;
        } else if (!t.Validate(outvec)) {
            return -1;
        }

        if (data_out) {
            for (int i = 0; i < outvec.size(); ++i) {
                data_out[i] = outvec[i];
            }
        }
        *size_out = outvec.size();
        return 0;
    } else if (typ == VS) {
        patchVertexShader(a, f_idx, offset);
        std::ostringstream as;
        for (const auto& l : a) {
            as << l << "\n";
        }

        std::vector<uint32_t> out;
        std::vector<uint32_t> optimized;

        t.Assemble(as.str(), &out);
        std::vector<uint32_t>& outvec = out;

        if (optimize != 0) {
            spvtools::Optimizer opt(SPV_ENV_VULKAN_1_3);
            opt.RegisterPerformancePasses();
            if (!opt.Run(out.data(), out.size(), &optimized)) {
                return -1;
            }
            outvec = optimized;
        } else if (!t.Validate(outvec)) {
            return -1;
        }

        if (data_out) {
            for (int i = 0; i < outvec.size(); ++i) {
                data_out[i] = outvec[i];
            }
        }
        *size_out = outvec.size();

        return 0;
    }

    return -1;
}
