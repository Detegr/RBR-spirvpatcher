#include <format>
#include <optional>
#include <ranges>
#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>
#include <sstream>

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

void patchOgVertexShader(std::vector<std::string>& a)
{
    // Add MultiView extension and builtin decoration for the provided ViewIndex variable
    a.insert(a.begin(), "OpCapability MultiView");
    a.insert(find_it(a, "OpDecorate"), "OpDecorate %ViewIndex BuiltIn ViewIndex");

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
    entry = "OpEntryPoint Vertex %main \"main\" %ViewIndex ";
    for (const auto& var : vars) {
        entry += var;
    }

    // Define ViewIndex variable
    a.insert(a.begin() + fun_idx, "%ptr = OpTypePointer Input %uint");
    a.insert(a.begin() + fun_idx + 1, "%ViewIndex = OpVariable %ptr Input");

    // Calculate offsets to the projection matrix
    const auto code_idx = find_idx(a, " = OpLabel");
    a.insert(a.begin() + code_idx + 1, "%vi = OpLoad %uint %ViewIndex");
    a.insert(a.begin() + code_idx + 2, "%view_offset = OpIMul %uint %vi %uint_4");
    a.insert(a.begin() + code_idx + 3, "%i0 = OpIAdd %uint %view_offset %uint_0");
    a.insert(a.begin() + code_idx + 4, "%i1 = OpIAdd %uint %view_offset %uint_1");
    a.insert(a.begin() + code_idx + 5, "%i2 = OpIAdd %uint %view_offset %uint_2");
    a.insert(a.begin() + code_idx + 6, "%i3 = OpIAdd %uint %view_offset %uint_3");

    // Patch accesses to the projection matrix to take ViewIndex into account
    for (int i = 0; i < 4; ++i) {
        auto use = a.begin();
        while (use != a.end()) {
            use = std::find_if(use, a.end(), [i](const auto& x) {
                return x.contains(std::format(
                    "OpAccessChain %_ptr_Uniform_v4float %c %uint_1 %int_{}", i));
            });
            if (use != a.end()) {
                auto& x = a[std::distance(a.begin(), use)];
                const auto begin = x.substr(0, x.find("="));
                x = std::format(
                    "{}= OpAccessChain %_ptr_Uniform_v4float %c %uint_1 %i{}", begin,
                    i);
            }
        }
    }
}

int AddMultiViewSupportToSPIRV(uint32_t* data, uint32_t size, uint32_t* data_out, uint32_t* size_out)
{
    spvtools::SpirvTools t(SPV_ENV_VULKAN_1_3);

    std::vector<uint32_t> in;
    in.assign(data, data + size);

    std::string as;
    if (!t.Disassemble(in, &as,
            SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES)) {
        return -1;
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

    const auto typ = detectType(a);
    if (typ == FF_VS) {
        // Pass through, nothing to do here
        // as the modifications are done on DXVK side
        if (data_out) {
            for (int i = 0; i < *size_out; ++i) {
                data_out[i] = data[i];
            }
        }
        *size_out = size;
        return 0;
    } else if (typ == VS) {
        // Patch RBR base game shader
        patchOgVertexShader(a);
        std::ostringstream as;
        for (const auto& l : a) {
            as << l << "\n";
        }
        std::vector<uint32_t> out;
        std::vector<uint32_t> optimized;
        t.Assemble(as.str(), &out);

        spvtools::Optimizer opt(SPV_ENV_VULKAN_1_3);
        opt.RegisterPerformancePasses();
        if (!opt.Run(out.data(), out.size(), &optimized)) {
            return -1;
        }

        if (data_out) {
            for (int i = 0; i < optimized.size(); ++i) {
                data_out[i] = optimized[i];
            }
        }
        *size_out = optimized.size();

        return 0;
    } else if (typ == BTB) {
        // Treat other shaders as BTB shaders
        // Multiview patching not yet supported
        return -1;
    }

    return true;
}
