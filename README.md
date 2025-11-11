# SPIR-V shader patcher utility for RBR

This little utility reads a SPIR-V fragment shader binary, disassembles it and adds [MultiView](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_multiview.html) support to the shader. Afterwards the shader is recompiled to binary again. This is by no means a generic approach, the library makes assumptions that are true for the DXVK generated SPIR-V shaders that Richard Burns Rally uses.

## Build instructions

```
git submodule update --init --recursive
cd spirv-tools
python3 utils/git-sync-deps
cd ..
cmake -B build -G Ninja
cd build
ninja
```
