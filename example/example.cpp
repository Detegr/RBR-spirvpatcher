#include <algorithm>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

#include <multiviewpatcher.hpp>

std::optional<std::vector<uint32_t>>
loadSPIRVBinary(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint32_t> buffer(size / sizeof(uint32_t));
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }

    return std::nullopt;
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cout << "Usage: ./example input.spv <source index> <destination index> <optimize> <disassemble>" << std::endl;
        return -1;
    }

    auto in = loadSPIRVBinary(argv[1]);
    if (!in) {
        return -1;
    }

    int8_t optimize = 0;
    if (argc > 4) {
    	optimize = std::string(argv[4]) == "1";
    }
    int8_t disassemble = 0;
    if (argc > 5) {
    	disassemble = std::string(argv[5]) == "1";
    }

    uint32_t start_reg = std::atoi(argv[2]);
    uint32_t target_reg = std::atoi(argv[3]);
    bool failed = false;

    std::cout << "Optimizing: " << (optimize > 0 ? "true" : "false") << std::endl;
    std::cout << std::format("Patching c.f[{}] to c.f[{}]", start_reg, target_reg) << std::endl;
    uint32_t outsize = 0;
    if (ChangeSPIRVMultiViewDataAccessLocation(in->data(), in->size(), nullptr, &outsize, start_reg, target_reg, optimize) != 0) {
        failed = true;
    }

    std::cout << "Input size: " << in->size() << " bytes, output size: " << outsize << " bytes." << std::endl;
    std::vector<uint32_t> out;
    out.resize(outsize);
    if (ChangeSPIRVMultiViewDataAccessLocation(in->data(), in->size(), out.data(), &outsize, start_reg, target_reg, optimize) != 0) {
        failed = true;
    }

    if (!failed) {
        std::ofstream outfile(std::format("{}-patched.spv", argv[1]), std::ofstream::binary);
        outfile.write(reinterpret_cast<char*>(out.data()), out.size() * sizeof(uint32_t));
    } else {
        std::cerr << "Failed" << std::endl;
    }

    if (disassemble != 0) {
        std::vector<int8_t> disas;
        uint32_t disas_size = 0;
        DisassembleSPIRV(out.data(), out.size(), nullptr, &disas_size);

        if (disas_size > 0) {
            disas.resize(disas_size);
            if (DisassembleSPIRV(out.data(), out.size(), disas.data(), nullptr) == 0) {
                for (char c : disas) {
                    std::cout << (char)c;
                }
            }
        }
    }
}
