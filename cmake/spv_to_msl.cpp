/// @file spv_to_msl.cpp
/// @brief SPIR-V → MSL transpiler with SDL3 GPU-compatible resource bindings.
///
/// Replaces `spirv-cross --msl` + the Python fixup script with a single tool
/// that uses the spirv-cross C++ API to set explicit Metal resource bindings
/// matching SDL3 GPU's Metal backend slot model:
///
///   Textures:  samplers(0..S)  →  RO-storage(S..)  →  RW-storage(S+RO..)
///   Buffers:   UBOs(0..U)     →  RO-storage(U..)  →  RW-storage(U+RO..)
///   Samplers:  0..S-1
///
/// Build:  cmake adds this as a utility target; see CMakeLists.txt.
/// Usage:  spv_to_msl <input.spv> <output.msl>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <spirv_cross/spirv_msl.hpp>
#include <vector>

static std::vector<uint32_t> read_spirv(const char* path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        fprintf(stderr, "spv_to_msl: cannot open %s\n", path);
        exit(1);
    }
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint32_t> data(static_cast<size_t>(size) / 4);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.spv> <output.msl>\n", argv[0]);
        return 1;
    }

    auto spirv = read_spirv(argv[1]);
    spirv_cross::CompilerMSL compiler(std::move(spirv));

    // Query all shader resources.
    auto resources = compiler.get_shader_resources();
    auto model = compiler.get_execution_model();

    // Sort helper: sort resources by their SPIR-V binding number.
    auto by_binding = [&](const spirv_cross::Resource& a, const spirv_cross::Resource& b) {
        return compiler.get_decoration(a.id, spv::DecorationBinding) <
               compiler.get_decoration(b.id, spv::DecorationBinding);
    };

    // --- Assign texture indices: samplers first, then RO storage, then RW storage ---

    // Collect and sort sampler textures (combined image/sampler).
    auto sampler_texs = resources.sampled_images;
    std::sort(sampler_texs.begin(), sampler_texs.end(), by_binding);

    // Separate images: spirv-cross puts read-only and read-write storage images here.
    auto storage_imgs = resources.storage_images;
    std::sort(storage_imgs.begin(), storage_imgs.end(), by_binding);

    // Separate into read-only vs read-write.
    std::vector<spirv_cross::Resource> ro_images, rw_images;
    for (auto& img : storage_imgs) {
        auto type = compiler.get_type(img.type_id);
        // If the image is declared readonly in SPIR-V, it's read-only storage.
        bool is_readonly = (compiler.get_decoration(img.id, spv::DecorationNonWritable) != 0);
        if (is_readonly)
            ro_images.push_back(img);
        else
            rw_images.push_back(img);
    }

    uint32_t tex_idx = 0;

    for (auto& r : sampler_texs) {
        spirv_cross::MSLResourceBinding binding;
        binding.stage = model;
        binding.desc_set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.msl_texture = tex_idx;
        binding.msl_sampler = tex_idx; // samplers use same sequential index
        binding.msl_buffer = 0;        // unused
        compiler.add_msl_resource_binding(binding);
        tex_idx++;
    }

    for (auto& r : ro_images) {
        spirv_cross::MSLResourceBinding binding;
        binding.stage = model;
        binding.desc_set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.msl_texture = tex_idx;
        binding.msl_sampler = 0;
        binding.msl_buffer = 0;
        compiler.add_msl_resource_binding(binding);
        tex_idx++;
    }

    for (auto& r : rw_images) {
        spirv_cross::MSLResourceBinding binding;
        binding.stage = model;
        binding.desc_set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.msl_texture = tex_idx;
        binding.msl_sampler = 0;
        binding.msl_buffer = 0;
        compiler.add_msl_resource_binding(binding);
        tex_idx++;
    }

    // --- Assign buffer indices: UBOs first, then RO SSBO, then RW SSBO ---

    auto ubos = resources.uniform_buffers;
    std::sort(ubos.begin(), ubos.end(), by_binding);

    auto ssbos = resources.storage_buffers;
    std::sort(ssbos.begin(), ssbos.end(), by_binding);

    std::vector<spirv_cross::Resource> ro_ssbos, rw_ssbos;
    for (auto& buf : ssbos) {
        bool is_readonly = (compiler.get_decoration(buf.id, spv::DecorationNonWritable) != 0);
        if (is_readonly)
            ro_ssbos.push_back(buf);
        else
            rw_ssbos.push_back(buf);
    }

    uint32_t buf_idx = 0;

    for (auto& r : ubos) {
        spirv_cross::MSLResourceBinding binding;
        binding.stage = model;
        binding.desc_set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.msl_buffer = buf_idx;
        binding.msl_texture = 0;
        binding.msl_sampler = 0;
        compiler.add_msl_resource_binding(binding);
        buf_idx++;
    }

    for (auto& r : ro_ssbos) {
        spirv_cross::MSLResourceBinding binding;
        binding.stage = model;
        binding.desc_set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.msl_buffer = buf_idx;
        binding.msl_texture = 0;
        binding.msl_sampler = 0;
        compiler.add_msl_resource_binding(binding);
        buf_idx++;
    }

    for (auto& r : rw_ssbos) {
        spirv_cross::MSLResourceBinding binding;
        binding.stage = model;
        binding.desc_set = compiler.get_decoration(r.id, spv::DecorationDescriptorSet);
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.msl_buffer = buf_idx;
        binding.msl_texture = 0;
        binding.msl_sampler = 0;
        compiler.add_msl_resource_binding(binding);
        buf_idx++;
    }

    // --- MSL options ---
    auto opts = compiler.get_msl_options();
    opts.set_msl_version(2, 0);
    compiler.set_msl_options(opts);

    // --- Compile and write ---
    std::string msl = compiler.compile();

    std::ofstream out(argv[2]);
    if (!out) {
        fprintf(stderr, "spv_to_msl: cannot write %s\n", argv[2]);
        return 1;
    }
    out << msl;
    return 0;
}
