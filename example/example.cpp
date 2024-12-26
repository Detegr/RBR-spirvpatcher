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
    if (argc < 2) {
        std::cout << "Usage: ./example input.spv" << std::endl;
        return -1;
    }

    auto in = loadSPIRVBinary(argv[1]);
    if (!in) {
        return -1;
    }

    uint32_t outsize = 0;
    AddMultiViewSupportToSPIRV(in->data(), in->size(), nullptr, &outsize);

    std::vector<uint32_t> out;
    out.resize(outsize);
    if (AddMultiViewSupportToSPIRV(in->data(), in->size(), out.data(), &outsize)) {
        std::cerr << "Failed" << std::endl;
        return 0;
    }

    std::ofstream outfile(std::format("{}-patched.spv", argv[1]), std::ofstream::binary);
    outfile.write(reinterpret_cast<char*>(out.data()), out.size() * sizeof(uint32_t));
}
