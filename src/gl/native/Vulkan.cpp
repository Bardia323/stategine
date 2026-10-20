// Stategine - the native device on Vulkan 1.3: dynamic rendering, pushed
// descriptors, every picture kept in the GENERAL layout with a barrier
// between what writes it and what reads it next.
#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "Device.hpp"

namespace sg::gl::native {

struct Texture {
    TextureInfo info;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    std::unordered_map<std::uint64_t, VkImageView> views;
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    std::size_t size = 0;
    void* host = nullptr;
};

struct Program {
    VkShaderModule modules[2] = {};
    // Its uniforms as last sent: which making, where, and in which epoch of
    // the frames' uploads (sent again once those are reused).
    VkBuffer sent_buffer = VK_NULL_HANDLE;
    VkDeviceSize sent_at = 0;
    std::uint64_t sent_made = 0, sent_epoch = ~std::uint64_t{0};
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    Translated t;
    std::unordered_map<std::uint64_t, VkPipeline> pipelines;
    VkPipeline compute = VK_NULL_HANDLE;  // a compute program's one
};

namespace {

#define VK_CHECK(x)                                                                                \
    do {                                                                                           \
        const VkResult r_ = (x);                                                                   \
        if (r_ != VK_SUCCESS) {                                                                    \
            std::fprintf(stderr, "stategine vulkan: %s failed (%d)\n", #x, static_cast<int>(r_)); \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)

VkFormat vk_format(Format f, bool d24_ok) {
    switch (f) {
        case Format::rgba8: return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::srgba8: return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::bgra8: return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::rgba16f: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::rgba32f: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::rg16f: return VK_FORMAT_R16G16_SFLOAT;
        case Format::rg32f: return VK_FORMAT_R32G32_SFLOAT;
        case Format::r32f: return VK_FORMAT_R32_SFLOAT;
        case Format::r32ui: return VK_FORMAT_R32_UINT;
        case Format::rg32ui: return VK_FORMAT_R32G32_UINT;
        case Format::bc7: return VK_FORMAT_BC7_UNORM_BLOCK;
        case Format::bc7_srgb: return VK_FORMAT_BC7_SRGB_BLOCK;
        case Format::d24: return d24_ok ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT;
        case Format::d32f: return VK_FORMAT_D32_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkCompareOp vk_compare(Compare c) { return static_cast<VkCompareOp>(c); }  // (the same order)

VkBlendFactor vk_factor(Factor f) {
    switch (f) {
        case Factor::zero: return VK_BLEND_FACTOR_ZERO;
        case Factor::src_alpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case Factor::one_minus_src_alpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case Factor::constant_alpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case Factor::one_minus_constant_alpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkSamplerAddressMode vk_wrap(SamplerInfo::Wrap w) {
    return w == SamplerInfo::repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT
           : w == SamplerInfo::clamp_border ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                                            : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v) { return (h ^ v) * 0x100000001B3ull + (h >> 29); }

// The format an attachment is drawn in (an sRGB one, as it is told).
VkFormat attachment_format(const Attachment& a) {
    return !a.encode && a.tex->format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM : a.tex->format;
}

// (A picture drawn into whose format is a whole number's blends nothing.)
bool integer_format(const Attachment& a) {
    return a.tex && (a.tex->info.format == Format::r32ui || a.tex->info.format == Format::rg32ui);
}

class VulkanDevice final : public Device {
public:
    bool start(GLFWwindow* window, std::string& why);
    ~VulkanDevice() override;

    const char* name() const override { return name_.c_str(); }
    float y_flip() const override { return 1.0f; }
    int frames_in_flight() const override { return kFrames; }

    Buffer* create_buffer(std::size_t bytes, Memory) override;
    void destroy(Buffer*) override;
    void* map(Buffer* b) override { return b->host; }
    void upload(Buffer*, std::size_t at, const void* data, std::size_t bytes) override;
    void download(Buffer*, std::size_t at, void* out, std::size_t bytes) override;

    Texture* create_texture(const TextureInfo&) override;
    void destroy(Texture*) override;
    const TextureInfo& info(const Texture* t) const override { return t->info; }
    void write(Texture*, int level, int x, int y, int z, int w, int h, int d, const void* data) override;
    void read(Texture*, int level, int layer, int x, int y, int w, int h, void* out) override;
    void read_into(Texture*, int level, int layer, int x, int y, int w, int h, Buffer*, std::size_t at) override;
    void copy(Texture* from, int from_level, int from_layer, int fx, int fy, Texture* to, int to_level, int to_layer, int tx, int ty, int w,
              int h) override;

    Program* create_program(const Translated&, std::string& log) override;
    void destroy(Program*) override;

    void target(const Target&) override;
    void clear(const int rect[4], std::uint8_t colors, const float color[4], bool depth, float depth_value) override;
    void draw(const Draw&) override;
    void dispatch(const Dispatch&) override;

    void begin_query(int slot) override;
    void end_query(int slot) override;
    void stamp(int slot) override;
    bool answer(int slot, bool wait, std::uint64_t& out) override;
    void only_if(int slot) override;

    Texture* window_color() override { return window_color_; }
    Texture* window_depth() override { return window_depth_; }
    void present(bool vsync) override;
    void finish() override;

private:
    static constexpr int kFrames = 2;
    static constexpr std::size_t kChunks = 32;  // a frame's uploads before it waits to reuse them

    struct Chunk {
        Buffer* buffer = nullptr;
        std::size_t used = 0;
    };
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> cmds;
        std::size_t cmd_used = 0;
        VkSemaphore acquired = VK_NULL_HANDLE;
        std::uint64_t done_at = 0;  // the timeline's value when its work is done
        std::vector<Chunk> chunks;  // its uploads and uniforms
        std::size_t chunk_now = 0;
        std::vector<Buffer*> dead_buffers;
        std::vector<Texture*> dead_textures;
        std::vector<VkBufferView> dead_views;
        std::unordered_map<std::uint64_t, VkBufferView> texel_views;
    };

    struct Ring {
        VkBuffer buffer;
        std::size_t at;
        std::uint8_t* host;
    };

    Frame& frame() { return frames_[frame_index_ % kFrames]; }
    VkCommandBuffer cmd();
    Ring ring(std::size_t bytes, std::size_t align = 256);
    void recycle();
    void barrier();
    void end_rendering();
    bool begin_rendering();
    void submit(bool wait, VkSemaphore wait_binary = VK_NULL_HANDLE, VkSemaphore signal_binary = VK_NULL_HANDLE);
    void wait_value(std::uint64_t v);
    void retire(Frame&);
    void free_texture(Texture*);
    void free_buffer(Buffer*);
    VkImageView view(Texture*, VkImageViewType, int base_level, int levels, int base_layer, int layers, bool raw = false);
    VkImageView attachment_view(const Attachment&);
    VkSampler sampler(const SamplerInfo&);
    Texture* empty(Sampled kind);
    VkPipeline pipeline(Program*, const Draw&);
    bool make_swapchain(bool vsync);
    void make_window_targets();
    void transition_new(Texture*);
    std::int64_t place(int kind, int slot);
    void reset_places(std::size_t frame);

    GLFWwindow* window_ = nullptr;
    std::string name_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t family_ = 0;
    VmaAllocator vma_ = nullptr;
    VkPhysicalDeviceProperties props_{};
    bool d24_ = true;
    bool multi_draw_ = false;  // many draw commands in one indirect call
    float max_aniso_ = 1;

    VkSemaphore timeline_ = VK_NULL_HANDLE;
    std::uint64_t timeline_value_ = 0;
    std::array<Frame, kFrames> frames_{};
    std::uint64_t frame_index_ = 0;
    VkCommandBuffer recording_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swap_images_;
    std::vector<VkSemaphore> swap_done_;
    VkExtent2D extent_{};
    VkFormat swap_format_ = VK_FORMAT_B8G8R8A8_UNORM;
    bool vsync_ = true;
    Texture* window_color_ = nullptr;
    Texture* window_depth_ = nullptr;

    Target target_;
    bool rendering_ = false;
    bool written_ = true;  // something written since the last barrier
    bool drawing_ = false;  // a draw being recorded: nothing submitted under it
    std::uint64_t epoch_ = 0;  // each time the uploads a draw may point at are reused, another

    // What the commands being recorded have bound, so a draw sets only what
    // differs. Forgotten with each new command buffer.
    struct Pushed {
        const Texture* tex = nullptr;
        int base = 0, top = 0;
        std::uint64_t sampler_key = 0;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize at = 0;
        VkBufferView texel = VK_NULL_HANDLE;
    };
    struct Bound_ {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        bool dynamic = false;
        VkViewport viewport{};
        VkRect2D scissor{};
        VkCullModeFlags cull = 0;
        VkFrontFace front = VK_FRONT_FACE_CLOCKWISE;
        bool test = false, write = false, bias = false;
        Compare op = Compare::less;
        float bias_units = 0, bias_factor = 0, blend[4] = {};
        std::array<Pushed, 64> pushed{};
        std::array<VkBuffer, 16> vbs{};
        std::array<VkDeviceSize, 16> offsets{};
    } bound_;
    struct {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize at = 0;
    } constant_;  // a value nothing reads, for a gap among the inputs
    Buffer* constants_ = nullptr;
    VkBufferView empty_texels_ = VK_NULL_HANDLE;

    // Questions: a pool of each kind (0 occlusion, 1 time), each frame its
    // own part of it, reset when the frame is done with - so a question asked
    // again takes a fresh place, and nothing waits to reset one. For each
    // slot (the front's name for it): where it was last asked, and in which
    // submission (the timeline's value then), or that it is still being
    // recorded.
    static constexpr std::uint32_t kPerFrame = 8192;
    struct Slot {
        int kind = 0;
        std::int64_t place = -1;  // -1: never asked; -2: its answer kept (`kept`), its place reset
        bool recording = false;
        std::uint64_t at = 0, kept = 0;
    };
    VkQueryPool pools_[2] = {};
    std::vector<Slot> slots_;
    std::array<std::array<std::uint32_t, 2>, kFrames> taken_{};  // places taken in each frame's part, by kind
    std::vector<int> recorded_;
    int asking_ = -1;
    float period_ = 1.0f;
    bool conditional_ = false;  // VK_EXT_conditional_rendering
    VkBuffer only_buffer_ = VK_NULL_HANDLE;
    std::size_t only_at_ = 0;
    bool only_on_ = false;

    std::unordered_map<std::uint64_t, VkSampler> samplers_;
    std::array<Texture*, 8> empties_{};
};

VulkanDevice::~VulkanDevice() {
    if (!device_) {
        if (instance_) vkDestroyInstance(instance_, nullptr);
        return;
    }
    vkDeviceWaitIdle(device_);
    end_rendering();
    for (Frame& f : frames_) {
        retire(f);
        for (Chunk& c : f.chunks) free_buffer(c.buffer);
        for (auto& [k, v] : f.texel_views) vkDestroyBufferView(device_, v, nullptr);
        vkDestroyCommandPool(device_, f.pool, nullptr);
        vkDestroySemaphore(device_, f.acquired, nullptr);
    }
    for (Texture* t : empties_)
        if (t) free_texture(t);
    if (empty_texels_) vkDestroyBufferView(device_, empty_texels_, nullptr);
    if (constants_) free_buffer(constants_);
    if (window_color_) free_texture(window_color_);
    if (window_depth_) free_texture(window_depth_);
    for (auto& [k, s] : samplers_) vkDestroySampler(device_, s, nullptr);
    for (VkSemaphore s : swap_done_) vkDestroySemaphore(device_, s, nullptr);
    for (VkQueryPool q : pools_)
        if (q) vkDestroyQueryPool(device_, q, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    vkDestroySemaphore(device_, timeline_, nullptr);
    vmaDestroyAllocator(vma_);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

bool VulkanDevice::start(GLFWwindow* window, std::string& why) {
    window_ = window;
    if (volkInitialize() != VK_SUCCESS) {
        why = "no Vulkan loader (vulkan-1.dll)";
        return false;
    }
    std::uint32_t n_ext = 0;
    const char** glfw_ext = glfwGetRequiredInstanceExtensions(&n_ext);
    if (!glfw_ext) {
        why = "GLFW finds no Vulkan surface support";
        return false;
    }
    std::vector<const char*> exts(glfw_ext, glfw_ext + n_ext);
    std::vector<const char*> layers;
    if (const char* v = std::getenv("SG_VK_VALIDATE"); v && *v == '1') {
        std::uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> all(n);
        vkEnumerateInstanceLayerProperties(&n, all.data());
        for (auto& l : all)
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "stategine";
    app.pEngineName = "stategine";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
    ici.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    ici.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) {
        why = "vkCreateInstance failed (Vulkan 1.3 needed)";
        return false;
    }
    volkLoadInstance(instance_);
    if (glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS) {
        why = "no Vulkan surface for the window (was it made with GLFW_NO_API?)";
        return false;
    }

    std::uint32_t n_dev = 0;
    vkEnumeratePhysicalDevices(instance_, &n_dev, nullptr);
    std::vector<VkPhysicalDevice> devs(n_dev);
    vkEnumeratePhysicalDevices(instance_, &n_dev, devs.data());
    int best = -1;
    for (VkPhysicalDevice d : devs) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_3) continue;
        std::uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
        std::vector<VkQueueFamilyProperties> qs(nq);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qs.data());
        for (std::uint32_t i = 0; i < nq; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present);
            if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            const int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best) best = score, physical_ = d, family_ = i, props_ = p;
            break;
        }
    }
    if (!physical_) {
        why = "no Vulkan 1.3 card that can show the window";
        return false;
    }
    name_ = props_.deviceName;
    max_aniso_ = props_.limits.maxSamplerAnisotropy;
    {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
        d24_ = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    }

    std::uint32_t n_dext = 0;
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n_dext, nullptr);
    std::vector<VkExtensionProperties> dexts(n_dext);
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n_dext, dexts.data());
    auto has = [&](const char* e) {
        return std::any_of(dexts.begin(), dexts.end(), [&](const VkExtensionProperties& p) { return std::strcmp(p.extensionName, e) == 0; });
    };
    if (!has(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
        why = "the card lacks VK_KHR_push_descriptor";
        return false;
    }
    std::vector<const char*> dev_exts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    conditional_ = has(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME);
    if (conditional_) dev_exts.push_back(VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME);

    VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    vkGetPhysicalDeviceFeatures2(physical_, &have);
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.timelineSemaphore = VK_TRUE;
    f12.hostQueryReset = VK_TRUE;
    f12.pNext = &f13;
    VkPhysicalDeviceConditionalRenderingFeaturesEXT fcr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT};
    fcr.conditionalRendering = VK_TRUE;
    if (conditional_) f13.pNext = &fcr;
    VkPhysicalDeviceFeatures2 want{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    want.pNext = &f12;
    want.features.samplerAnisotropy = have.features.samplerAnisotropy;
    want.features.depthClamp = have.features.depthClamp;
    want.features.textureCompressionBC = have.features.textureCompressionBC;
    want.features.shaderClipDistance = have.features.shaderClipDistance;
    want.features.independentBlend = have.features.independentBlend;
    want.features.imageCubeArray = have.features.imageCubeArray;
    want.features.fragmentStoresAndAtomics = have.features.fragmentStoresAndAtomics;
    want.features.multiDrawIndirect = have.features.multiDrawIndirect;
    multi_draw_ = have.features.multiDrawIndirect == VK_TRUE;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &want;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<std::uint32_t>(dev_exts.size());
    dci.ppEnabledExtensionNames = dev_exts.data();
    if (vkCreateDevice(physical_, &dci, nullptr, &device_) != VK_SUCCESS) {
        why = "vkCreateDevice failed";
        return false;
    }
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, family_, 0, &queue_);

    VmaVulkanFunctions fns{};
    fns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    fns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo aci{};
    aci.physicalDevice = physical_;
    aci.device = device_;
    aci.instance = instance_;
    aci.vulkanApiVersion = VK_API_VERSION_1_3;
    aci.pVulkanFunctions = &fns;
    VK_CHECK(vmaCreateAllocator(&aci, &vma_));

    VkSemaphoreTypeCreateInfo tci{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    sci.pNext = &tci;
    VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr, &timeline_));
    for (Frame& f : frames_) {
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex = family_;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &f.pool));
        VkSemaphoreCreateInfo bin{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &bin, nullptr, &f.acquired));
    }
    if (!make_swapchain(true)) {
        why = "no swapchain for the window";
        return false;
    }
    make_window_targets();
    period_ = props_.limits.timestampPeriod;
    for (int k = 0; k < 2; ++k) {
        VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qp.queryType = k ? VK_QUERY_TYPE_TIMESTAMP : VK_QUERY_TYPE_OCCLUSION;
        qp.queryCount = kPerFrame * kFrames;
        VK_CHECK(vkCreateQueryPool(device_, &qp, nullptr, &pools_[k]));
        vkResetQueryPool(device_, pools_[k], 0, kPerFrame * kFrames);
    }
    slots_.assign(kQueries, Slot{});
    constants_ = create_buffer(256, Memory::upload);
    std::memset(constants_->host, 0, 256);
    constant_.buffer = constants_->buffer;
    {
        VkBufferViewCreateInfo bvi{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
        bvi.buffer = constants_->buffer;
        bvi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        bvi.range = 16;
        VK_CHECK(vkCreateBufferView(device_, &bvi, nullptr, &empty_texels_));
    }
    // What an unbound sampler reads, made now: never in the middle of a draw.
    for (Sampled k : {Sampled::tex2d, Sampled::tex2d_array, Sampled::tex3d, Sampled::cube, Sampled::tex2d_ms, Sampled::utex2d}) empty(k);
    return true;
}

// --- commands -------------------------------------------------------------------

VkCommandBuffer VulkanDevice::cmd() {
    if (recording_) return recording_;
    Frame& f = frame();
    if (f.cmd_used == f.cmds.size()) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer c;
        VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &c));
        f.cmds.push_back(c);
    }
    recording_ = f.cmds[f.cmd_used++];
    bound_ = Bound_{};
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(recording_, &bi));
    written_ = true;
    return recording_;
}

void VulkanDevice::submit(bool wait, VkSemaphore wait_binary, VkSemaphore signal_binary) {
    end_rendering();
    VkCommandBuffer c = cmd();
    VK_CHECK(vkEndCommandBuffer(c));
    recording_ = VK_NULL_HANDLE;
    const std::uint64_t value = ++timeline_value_;
    const std::uint64_t waits[1] = {0};
    const std::uint64_t signals[2] = {value, 0};
    VkSemaphore signal_sems[2] = {timeline_, signal_binary};
    VkTimelineSemaphoreSubmitInfo tsi{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    tsi.waitSemaphoreValueCount = wait_binary ? 1 : 0;
    tsi.pWaitSemaphoreValues = waits;
    tsi.signalSemaphoreValueCount = signal_binary ? 2 : 1;
    tsi.pSignalSemaphoreValues = signals;
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.pNext = &tsi;
    si.waitSemaphoreCount = wait_binary ? 1 : 0;
    si.pWaitSemaphores = &wait_binary;
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c;
    si.signalSemaphoreCount = signal_binary ? 2 : 1;
    si.pSignalSemaphores = signal_sems;
    VK_CHECK(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
    frame().done_at = value;
    for (int slot : recorded_) {
        Slot& q = slots_[static_cast<std::size_t>(slot)];
        q.recording = false, q.at = value;
    }
    recorded_.clear();
    if (wait) {
        wait_value(value);
        // (The card idle: what every frame let go of goes now.)
        for (Frame& f : frames_) {
            for (Buffer* b : f.dead_buffers) free_buffer(b);
            for (Texture* t : f.dead_textures) free_texture(t);
            for (VkBufferView v : f.dead_views) vkDestroyBufferView(device_, v, nullptr);
            f.dead_buffers.clear(), f.dead_textures.clear(), f.dead_views.clear();
        }
    }
}

void VulkanDevice::wait_value(std::uint64_t v) {
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wi.semaphoreCount = 1;
    wi.pSemaphores = &timeline_;
    wi.pValues = &v;
    VK_CHECK(vkWaitSemaphores(device_, &wi, UINT64_MAX));
}

void VulkanDevice::barrier() {
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd(), &di);
    written_ = false;
}

VulkanDevice::Ring VulkanDevice::ring(std::size_t bytes, std::size_t align) {
    Frame& f = frame();
    auto fits = [&](Chunk& c) { return ((c.used + align - 1) & ~(align - 1)) + bytes <= c.buffer->size; };
    while (f.chunk_now < f.chunks.size() && !fits(f.chunks[f.chunk_now])) ++f.chunk_now;
    if (f.chunk_now == f.chunks.size() && f.chunks.size() >= kChunks && !drawing_ && asking_ < 0) {
        recycle();
        while (f.chunk_now < f.chunks.size() && !fits(f.chunks[f.chunk_now])) ++f.chunk_now;
    }
    if (f.chunk_now == f.chunks.size()) f.chunks.push_back({create_buffer(std::max<std::size_t>(bytes, std::size_t{8} << 20), Memory::upload), 0});
    Chunk& c = f.chunks[f.chunk_now];
    c.used = (c.used + align - 1) & ~(align - 1);
    Ring r{c.buffer->buffer, c.used, static_cast<std::uint8_t*>(c.buffer->host) + c.used};
    c.used += bytes;
    return r;
}

// A frame long in coming - many drawn and none shown - starts its uploads
// over once the card has used them.
void VulkanDevice::recycle() {
    Frame& f = frame();
    submit(true);
    ++epoch_;
    for (Chunk& c : f.chunks) c.used = 0;
    f.chunk_now = 0;
    for (auto& [k, v] : f.texel_views) f.dead_views.push_back(v);
    f.texel_views.clear();
}

void VulkanDevice::retire(Frame& f) {
    wait_value(f.done_at);
    reset_places(static_cast<std::size_t>(&f - frames_.data()));
    ++epoch_;
    if (f.cmd_used) vkResetCommandPool(device_, f.pool, 0);
    f.cmd_used = 0;
    for (Chunk& c : f.chunks) c.used = 0;
    f.chunk_now = 0;
    for (Buffer* b : f.dead_buffers) free_buffer(b);
    for (Texture* t : f.dead_textures) free_texture(t);
    for (VkBufferView v : f.dead_views) vkDestroyBufferView(device_, v, nullptr);
    for (auto& [k, v] : f.texel_views) vkDestroyBufferView(device_, v, nullptr);
    f.dead_buffers.clear(), f.dead_textures.clear(), f.dead_views.clear(), f.texel_views.clear();
}

// --- buffers --------------------------------------------------------------------

Buffer* VulkanDevice::create_buffer(std::size_t bytes, Memory memory) {
    auto* b = new Buffer;
    b->size = bytes;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = std::max<std::size_t>(bytes, 4);
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo aci{};
    // (Uploads where the card reads them fastest that the processor can
    // write - its own memory, where all of it is open to the processor.)
    aci.usage = memory == Memory::readback ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO;
    if (memory == Memory::upload) aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (memory == Memory::readback) aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo ai{};
    VK_CHECK(vmaCreateBuffer(vma_, &bci, &aci, &b->buffer, &b->alloc, &ai));
    b->host = ai.pMappedData;
    return b;
}

void VulkanDevice::free_buffer(Buffer* b) {
    vmaDestroyBuffer(vma_, b->buffer, b->alloc);
    delete b;
}

void VulkanDevice::destroy(Buffer* b) {
    if (b) frame().dead_buffers.push_back(b);
}

void VulkanDevice::upload(Buffer* b, std::size_t at, const void* data, std::size_t bytes) {
    if (!bytes) return;
    end_rendering();
    const Ring r = ring(bytes, 16);
    std::memcpy(r.host, data, bytes);
    if (written_) barrier();
    VkBufferCopy region{r.at, at, bytes};
    vkCmdCopyBuffer(cmd(), r.buffer, b->buffer, 1, &region);
    written_ = true;
}

void VulkanDevice::download(Buffer* b, std::size_t at, void* out, std::size_t bytes) {
    if (b->host) {
        submit(true);
        vmaInvalidateAllocation(vma_, b->alloc, at, bytes);
        std::memcpy(out, static_cast<std::uint8_t*>(b->host) + at, bytes);
        return;
    }
    Buffer* staging = create_buffer(bytes, Memory::readback);
    end_rendering();
    barrier();
    VkBufferCopy region{at, 0, bytes};
    vkCmdCopyBuffer(cmd(), b->buffer, staging->buffer, 1, &region);
    written_ = true;
    submit(true);
    vmaInvalidateAllocation(vma_, staging->alloc, 0, bytes);
    std::memcpy(out, staging->host, bytes);
    free_buffer(staging);
}

// --- textures -------------------------------------------------------------------

void VulkanDevice::transition_new(Texture* t) {
    end_rendering();
    VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    ib.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    ib.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ib.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    ib.image = t->image;
    ib.subresourceRange = {t->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &ib;
    vkCmdPipelineBarrier2(cmd(), &di);
}

Texture* VulkanDevice::create_texture(const TextureInfo& info) {
    auto* t = new Texture;
    t->info = info;
    t->format = vk_format(info.format, d24_);
    const bool depth = is_depth(info.format);
    t->aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = info.kind == TexKind::tex3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ici.format = t->format;
    ici.extent = {static_cast<std::uint32_t>(info.w), static_cast<std::uint32_t>(info.h),
                  info.kind == TexKind::tex3d ? static_cast<std::uint32_t>(info.depth) : 1u};
    ici.mipLevels = static_cast<std::uint32_t>(info.levels);
    ici.arrayLayers = info.kind == TexKind::tex3d ? 1u : static_cast<std::uint32_t>(info.depth);
    ici.samples = static_cast<VkSampleCountFlagBits>(info.samples);
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!is_packed(info.format))
        ici.usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (info.kind == TexKind::cube) ici.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if (info.kind == TexKind::tex3d) ici.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
    if (info.format == Format::srgba8) ici.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK(vmaCreateImage(vma_, &ici, &aci, &t->image, &t->alloc, nullptr));
    transition_new(t);
    // (Its first contents nothing in particular: GL's are undefined too, but
    // a picture never written reads black there more often than not.)
    if (!is_packed(info.format)) {
        VkImageSubresourceRange range{t->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        if (depth) {
            VkClearDepthStencilValue v{1.0f, 0};
            vkCmdClearDepthStencilImage(cmd(), t->image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
        } else {
            VkClearColorValue v{};
            vkCmdClearColorImage(cmd(), t->image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
        }
        written_ = true;
    }
    return t;
}

void VulkanDevice::free_texture(Texture* t) {
    for (auto& [k, v] : t->views) vkDestroyImageView(device_, v, nullptr);
    vmaDestroyImage(vma_, t->image, t->alloc);
    delete t;
}

void VulkanDevice::destroy(Texture* t) {
    if (!t) return;
    for (int i = 0; i < Target::kColors; ++i)
        if (target_.color[i].tex == t) end_rendering(), target_.color[i] = {};
    if (target_.depth.tex == t) end_rendering(), target_.depth = {};
    frame().dead_textures.push_back(t);
}

VkImageView VulkanDevice::view(Texture* t, VkImageViewType type, int base_level, int levels, int base_layer, int layers, bool raw) {
    const std::uint64_t key = std::uint64_t(type) | std::uint64_t(base_level) << 8 | std::uint64_t(levels) << 16 |
                              std::uint64_t(base_layer) << 24 | std::uint64_t(layers) << 40 | std::uint64_t(raw) << 56;
    auto it = t->views.find(key);
    if (it != t->views.end()) return it->second;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = t->image;
    vci.viewType = type;
    vci.format = raw && t->format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM : t->format;
    vci.subresourceRange = {t->aspect, static_cast<std::uint32_t>(base_level), static_cast<std::uint32_t>(levels),
                            static_cast<std::uint32_t>(base_layer), static_cast<std::uint32_t>(layers)};
    VkImageView v;
    VK_CHECK(vkCreateImageView(device_, &vci, nullptr, &v));
    t->views.emplace(key, v);
    return v;
}

VkImageView VulkanDevice::attachment_view(const Attachment& a) { return view(a.tex, VK_IMAGE_VIEW_TYPE_2D, a.level, 1, a.layer, 1, !a.encode); }

void VulkanDevice::write(Texture* t, int level, int x, int y, int z, int w, int h, int d, const void* data) {
    const bool packed = is_packed(t->info.format);
    const std::size_t row = packed ? static_cast<std::size_t>((w + 3) / 4) * 16 : static_cast<std::size_t>(w) * static_cast<std::size_t>(texel_bytes(t->info.format));
    const std::size_t rows = packed ? static_cast<std::size_t>((h + 3) / 4) : static_cast<std::size_t>(h);
    const std::size_t bytes = row * rows * static_cast<std::size_t>(d);
    end_rendering();
    const Ring r = ring(bytes, 16);
    std::memcpy(r.host, data, bytes);
    if (written_) barrier();
    VkBufferImageCopy c{};
    c.bufferOffset = r.at;
    c.imageSubresource = {t->aspect, static_cast<std::uint32_t>(level), 0, 1};
    if (t->info.kind == TexKind::tex3d) {
        c.imageOffset = {x, y, z};
        c.imageExtent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), static_cast<std::uint32_t>(d)};
    } else {
        c.imageSubresource.baseArrayLayer = static_cast<std::uint32_t>(z);
        c.imageSubresource.layerCount = static_cast<std::uint32_t>(d);
        c.imageOffset = {x, y, 0};
        c.imageExtent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
    }
    vkCmdCopyBufferToImage(cmd(), r.buffer, t->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    written_ = true;
}

void VulkanDevice::read_into(Texture* t, int level, int layer, int x, int y, int w, int h, Buffer* b, std::size_t at) {
    end_rendering();
    if (written_) barrier();
    VkBufferImageCopy c{};
    c.bufferOffset = at;
    c.imageSubresource = {t->aspect, static_cast<std::uint32_t>(level), 0, 1};
    if (t->info.kind == TexKind::tex3d) {
        c.imageOffset = {x, y, layer};
    } else {
        c.imageSubresource.baseArrayLayer = static_cast<std::uint32_t>(layer);
        c.imageOffset = {x, y, 0};
    }
    c.imageExtent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
    vkCmdCopyImageToBuffer(cmd(), t->image, VK_IMAGE_LAYOUT_GENERAL, b->buffer, 1, &c);
    written_ = true;
}

void VulkanDevice::read(Texture* t, int level, int layer, int x, int y, int w, int h, void* out) {
    const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(texel_bytes(t->info.format));
    Buffer* staging = create_buffer(bytes, Memory::readback);
    read_into(t, level, layer, x, y, w, h, staging, 0);
    submit(true);
    vmaInvalidateAllocation(vma_, staging->alloc, 0, bytes);
    std::memcpy(out, staging->host, bytes);
    free_buffer(staging);
}

void VulkanDevice::copy(Texture* from, int from_level, int from_layer, int fx, int fy, Texture* to, int to_level, int to_layer, int tx, int ty,
                        int w, int h) {
    end_rendering();
    if (written_) barrier();
    VkImageCopy c{};
    c.srcSubresource = {from->aspect, static_cast<std::uint32_t>(from_level), 0, 1};
    c.dstSubresource = {to->aspect, static_cast<std::uint32_t>(to_level), 0, 1};
    if (from->info.kind == TexKind::tex3d) c.srcOffset = {fx, fy, from_layer};
    else c.srcSubresource.baseArrayLayer = static_cast<std::uint32_t>(from_layer), c.srcOffset = {fx, fy, 0};
    if (to->info.kind == TexKind::tex3d) c.dstOffset = {tx, ty, to_layer};
    else c.dstSubresource.baseArrayLayer = static_cast<std::uint32_t>(to_layer), c.dstOffset = {tx, ty, 0};
    c.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
    vkCmdCopyImage(cmd(), from->image, VK_IMAGE_LAYOUT_GENERAL, to->image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    written_ = true;
}

VkSampler VulkanDevice::sampler(const SamplerInfo& s) {
    const std::uint64_t key = s.key();
    auto it = samplers_.find(key);
    if (it != samplers_.end()) return it->second;
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = s.mag == SamplerInfo::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.minFilter = s.min == SamplerInfo::linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.mipmapMode = s.mip == SamplerInfo::mip_linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = vk_wrap(s.wrap[0]);
    sci.addressModeV = vk_wrap(s.wrap[1]);
    sci.addressModeW = vk_wrap(s.wrap[2]);
    sci.anisotropyEnable = s.anisotropy > 1 && s.mip != SamplerInfo::none ? VK_TRUE : VK_FALSE;
    sci.maxAnisotropy = std::min(static_cast<float>(s.anisotropy), max_aniso_);
    sci.compareEnable = s.compare ? VK_TRUE : VK_FALSE;
    sci.compareOp = static_cast<VkCompareOp>(s.compare_op);
    sci.minLod = 0;
    // (No mipmaps asked: only the view's first level, as GL's GL_LINEAR.)
    sci.maxLod = s.mip == SamplerInfo::none ? 0.0f : VK_LOD_CLAMP_NONE;
    sci.borderColor = s.border_white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler out;
    VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &out));
    samplers_.emplace(key, out);
    return out;
}

Texture* VulkanDevice::empty(Sampled kind) {
    const auto i = static_cast<std::size_t>(kind);
    if (empties_[i]) return empties_[i];
    TextureInfo info;
    info.format = kind == Sampled::utex2d ? Format::rg32ui : Format::rgba8;
    info.kind = kind == Sampled::tex2d_array ? TexKind::tex2d_array : kind == Sampled::tex3d ? TexKind::tex3d : kind == Sampled::cube ? TexKind::cube : TexKind::tex2d;
    info.depth = kind == Sampled::cube ? 6 : 1;
    info.samples = kind == Sampled::tex2d_ms ? 4 : 1;
    empties_[i] = create_texture(info);
    return empties_[i];
}

// --- programs -------------------------------------------------------------------

Program* VulkanDevice::create_program(const Translated& t, std::string& log) {
    auto* p = new Program;
    p->t = t;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    const VkShaderStageFlags all = t.compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (int s = 0; s < 2; ++s)
        if (t.block_size[s]) bindings.push_back({static_cast<std::uint32_t>(kBlockBinding[s]), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, all, nullptr});
    for (const SamplerSlot& s : t.samplers)
        bindings.push_back({static_cast<std::uint32_t>(s.binding),
                            s.kind == Sampled::buffer ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, all,
                            nullptr});
    for (const StorageSlot& s : t.storage)
        bindings.push_back({static_cast<std::uint32_t>(kFirstStorage + s.binding), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, all, nullptr});
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    lci.bindingCount = static_cast<std::uint32_t>(bindings.size());
    lci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &lci, nullptr, &p->set_layout) != VK_SUCCESS) {
        log = "descriptor set layout";
        delete p;
        return nullptr;
    }
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &p->set_layout;
    VK_CHECK(vkCreatePipelineLayout(device_, &pli, nullptr, &p->layout));
    for (int s = 0; s < (t.compute ? 1 : 2); ++s) {
        VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mci.codeSize = t.spirv[s].size() * 4;
        mci.pCode = t.spirv[s].data();
        if (vkCreateShaderModule(device_, &mci, nullptr, &p->modules[s]) != VK_SUCCESS) {
            log = "shader module";
            destroy(p);
            return nullptr;
        }
    }
    if (t.compute) {
        VkComputePipelineCreateInfo cci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, p->modules[0], "main", nullptr};
        cci.layout = p->layout;
        if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &cci, nullptr, &p->compute) != VK_SUCCESS) {
            log = "compute pipeline";
            destroy(p);
            return nullptr;
        }
    }
    return p;
}

void VulkanDevice::destroy(Program* p) {
    if (!p) return;
    // (What is being recorded may draw with it: done first.)
    submit(true);
    for (auto& [k, pl] : p->pipelines) vkDestroyPipeline(device_, pl, nullptr);
    if (p->compute) vkDestroyPipeline(device_, p->compute, nullptr);
    for (VkShaderModule m : p->modules)
        if (m) vkDestroyShaderModule(device_, m, nullptr);
    if (p->layout) vkDestroyPipelineLayout(device_, p->layout, nullptr);
    if (p->set_layout) vkDestroyDescriptorSetLayout(device_, p->set_layout, nullptr);
    delete p;
}

VkPipeline VulkanDevice::pipeline(Program* p, const Draw& d) {
    const Raster& r = d.raster;
    std::uint64_t key = 1469598103934665603ull;
    key = mix(key, std::uint64_t(r.blend) | std::uint64_t(r.src_rgb) << 1 | std::uint64_t(r.dst_rgb) << 4 | std::uint64_t(r.src_a) << 7 |
                       std::uint64_t(r.dst_a) << 10 | std::uint64_t(r.color_mask) << 13 | std::uint64_t(r.depth_clamp) << 17);
    for (int i = 0; i < Target::kColors; ++i) key = mix(key, target_.color[i].tex ? std::uint64_t(attachment_format(target_.color[i])) + 1 : 0);
    key = mix(key, target_.depth.tex ? std::uint64_t(target_.depth.tex->format) + 1 : 0);
    const Texture* any = target_.depth.tex;
    for (int i = 0; i < Target::kColors && !any; ++i) any = target_.color[i].tex;
    const int samples = any ? any->info.samples : 1;
    key = mix(key, std::uint64_t(samples));
    for (int loc = 0; loc < 16; ++loc) {
        if (!(p->t.inputs & (1u << loc))) continue;
        const VertexInput& in = d.inputs[loc];
        key = mix(key, in.buffer ? (std::uint64_t(in.components) | std::uint64_t(in.stride) << 4 | std::uint64_t(in.per_instance) << 40) : 0xFFFF);
    }
    auto it = p->pipelines.find(key);
    if (it != p->pipelines.end()) return it->second;

    VkPipelineShaderStageCreateInfo stages[2]{};
    for (int s = 0; s < 2; ++s) {
        stages[s].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[s].stage = s ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[s].module = p->modules[s];
        stages[s].pName = "main";
    }
    std::vector<VkVertexInputBindingDescription> vb;
    std::vector<VkVertexInputAttributeDescription> va;
    for (std::uint32_t loc = 0; loc < 16; ++loc) {
        if (!(p->t.inputs & (1u << loc))) continue;
        const VertexInput& in = d.inputs[loc];
        const bool from_buffer = in.buffer != nullptr;
        vb.push_back({loc, from_buffer ? in.stride : 0,
                      from_buffer && in.per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
        const VkFormat formats[] = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
        va.push_back({loc, loc, from_buffer ? formats[std::clamp<int>(in.components, 1, 4) - 1] : VK_FORMAT_R32G32B32A32_SFLOAT, 0});
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vb.size());
    vi.pVertexBindingDescriptions = vb.data();
    vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(va.size());
    vi.pVertexAttributeDescriptions = va.data();
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.depthClampEnable = r.depth_clamp ? VK_TRUE : VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples);
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    std::array<VkPipelineColorBlendAttachmentState, Target::kColors> blends{};
    std::array<VkFormat, Target::kColors> formats{};
    std::uint32_t n_colors = 0;
    for (int i = 0; i < Target::kColors; ++i) {
        formats[static_cast<std::size_t>(i)] = target_.color[i].tex ? attachment_format(target_.color[i]) : VK_FORMAT_UNDEFINED;
        if (target_.color[i].tex) n_colors = static_cast<std::uint32_t>(i + 1);
        VkPipelineColorBlendAttachmentState& b = blends[static_cast<std::size_t>(i)];
        b.blendEnable = r.blend && !integer_format(target_.color[i]) ? VK_TRUE : VK_FALSE;
        b.srcColorBlendFactor = vk_factor(r.src_rgb);
        b.dstColorBlendFactor = vk_factor(r.dst_rgb);
        b.colorBlendOp = VK_BLEND_OP_ADD;
        b.srcAlphaBlendFactor = vk_factor(r.src_a);
        b.dstAlphaBlendFactor = vk_factor(r.dst_a);
        b.alphaBlendOp = VK_BLEND_OP_ADD;
        b.colorWriteMask = r.color_mask;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = n_colors;
    cb.pAttachments = blends.data();
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT,          VK_DYNAMIC_STATE_SCISSOR,          VK_DYNAMIC_STATE_DEPTH_BIAS,
                                  VK_DYNAMIC_STATE_BLEND_CONSTANTS,   VK_DYNAMIC_STATE_CULL_MODE,        VK_DYNAMIC_STATE_FRONT_FACE,
                                  VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
                                  VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = static_cast<std::uint32_t>(std::size(dyn));
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = n_colors;
    ri.pColorAttachmentFormats = formats.data();
    ri.depthAttachmentFormat = target_.depth.tex ? target_.depth.tex->format : VK_FORMAT_UNDEFINED;
    if (target_.depth.tex && target_.depth.tex->format == VK_FORMAT_D24_UNORM_S8_UINT) ri.stencilAttachmentFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext = &ri;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dy;
    pci.layout = p->layout;
    VkPipeline out = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci, nullptr, &out));
    p->pipelines.emplace(key, out);
    return out;
}

// --- drawing --------------------------------------------------------------------

void VulkanDevice::end_rendering() {
    if (!rendering_) return;
    if (only_on_) vkCmdEndConditionalRenderingEXT(cmd()), only_on_ = false;
    // (A question cannot outlast the drawing it was asked in: what it found
    // so far is its answer.)
    if (asking_ >= 0) vkCmdEndQuery(cmd(), pools_[0], static_cast<std::uint32_t>(slots_[static_cast<std::size_t>(asking_)].place)), asking_ = -1;
    vkCmdEndRendering(cmd());
    rendering_ = false;
    written_ = true;
}

void VulkanDevice::target(const Target& t) {
    if (t == target_) return;
    end_rendering();
    target_ = t;
}

bool VulkanDevice::begin_rendering() {
    if (rendering_) return true;
    std::array<VkRenderingAttachmentInfo, Target::kColors> colors{};
    std::uint32_t n = 0;
    for (int i = 0; i < Target::kColors; ++i) {
        VkRenderingAttachmentInfo& a = colors[static_cast<std::size_t>(i)];
        a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (!target_.color[i].tex) continue;
        a.imageView = attachment_view(target_.color[i]);
        a.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        n = static_cast<std::uint32_t>(i + 1);
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (target_.depth.tex) {
        depth.imageView = attachment_view(target_.depth);
        depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    if (!n && !target_.depth.tex) return false;
    if (written_) barrier();
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {static_cast<std::uint32_t>(target_.w), static_cast<std::uint32_t>(target_.h)}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = n;
    ri.pColorAttachments = colors.data();
    ri.pDepthAttachment = target_.depth.tex ? &depth : nullptr;
    ri.pStencilAttachment = target_.depth.tex && target_.depth.tex->format == VK_FORMAT_D24_UNORM_S8_UINT ? &depth : nullptr;
    vkCmdBeginRendering(cmd(), &ri);
    rendering_ = true;
    if (only_buffer_) {
        VkConditionalRenderingBeginInfoEXT cb{VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT};
        cb.buffer = only_buffer_;
        cb.offset = only_at_;
        vkCmdBeginConditionalRenderingEXT(cmd(), &cb);
        only_on_ = true;
    }
    return true;
}

void VulkanDevice::clear(const int rect[4], std::uint8_t colors, const float color[4], bool depth, float depth_value) {
    if (!begin_rendering()) return;
    const int x0 = std::clamp(rect[0], 0, target_.w), y0 = std::clamp(rect[1], 0, target_.h);
    const int x1 = std::clamp(rect[0] + rect[2], 0, target_.w), y1 = std::clamp(rect[1] + rect[3], 0, target_.h);
    if (x1 <= x0 || y1 <= y0) return;
    std::vector<VkClearAttachment> what;
    for (std::uint32_t i = 0; i < Target::kColors; ++i)
        if ((colors >> i) & 1u) {
            VkClearAttachment c{VK_IMAGE_ASPECT_COLOR_BIT, i, {}};
            std::memcpy(c.clearValue.color.float32, color, 16);
            what.push_back(c);
        }
    if (depth && target_.depth.tex) {
        VkClearAttachment c{VK_IMAGE_ASPECT_DEPTH_BIT, 0, {}};
        c.clearValue.depthStencil = {depth_value, 0};
        what.push_back(c);
    }
    if (what.empty()) return;
    VkClearRect r{{{x0, y0}, {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)}}, 0, 1};
    vkCmdClearAttachments(cmd(), static_cast<std::uint32_t>(what.size()), what.data(), 1, &r);
}

void VulkanDevice::draw(const Draw& d) {
    Program* p = d.program;
    if (!p || d.viewport[2] <= 0 || d.viewport[3] <= 0) return;
    {
        // (Room for what it uploads, made before it is recorded.)
        Frame& f = frame();
        const bool room = f.chunk_now < f.chunks.size() && f.chunks[f.chunk_now].used + (std::size_t{96} << 10) <= f.chunks[f.chunk_now].buffer->size;
        // (Never while a question is open: it would end before what it asks of.)
        if (!room && f.chunks.size() >= kChunks && asking_ < 0) recycle();
    }
    struct Drawing {
        bool& on;
        explicit Drawing(bool& o) : on(o) { on = true; }
        ~Drawing() { on = false; }
    } drawing(drawing_);
    const int x0 = std::clamp(d.scissor[0], 0, target_.w), y0 = std::clamp(d.scissor[1], 0, target_.h);
    const int x1 = std::clamp(d.scissor[0] + d.scissor[2], 0, target_.w), y1 = std::clamp(d.scissor[1] + d.scissor[3], 0, target_.h);
    if (x1 <= x0 || y1 <= y0 || !begin_rendering()) return;
    VkCommandBuffer c = cmd();
    Bound_& b = bound_;
    if (VkPipeline pl = pipeline(p, d); pl != b.pipeline) vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pl), b.pipeline = pl;
    if (b.layout != p->layout) {
        // (Another program's set: everything pushed again.)
        b.layout = p->layout;
        for (Pushed& x : b.pushed) x = Pushed{};
    }

    // Its state, where it differs from what the commands have now.
    const VkViewport vp{static_cast<float>(d.viewport[0]), static_cast<float>(d.viewport[1]), static_cast<float>(d.viewport[2]),
                        static_cast<float>(d.viewport[3]), d.depth_range[0], d.depth_range[1]};
    if (!b.dynamic || std::memcmp(&vp, &b.viewport, sizeof vp) != 0) vkCmdSetViewport(c, 0, 1, &vp), b.viewport = vp;
    const VkRect2D sc{{x0, y0}, {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)}};
    if (!b.dynamic || std::memcmp(&sc, &b.scissor, sizeof sc) != 0) vkCmdSetScissor(c, 0, 1, &sc), b.scissor = sc;
    const Raster& r = d.raster;
    const VkCullModeFlags cull = r.cull ? (r.cull_front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) : VK_CULL_MODE_NONE;
    const bool test = r.depth_test && target_.depth.tex, write = test && r.depth_write;
    // (GL's rows and these lie alike, but the two measure a turn opposite ways.)
    const VkFrontFace front = r.front_cw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    if (!b.dynamic || front != b.front) vkCmdSetFrontFace(c, front), b.front = front;
    if (!b.dynamic || cull != b.cull) vkCmdSetCullMode(c, cull), b.cull = cull;
    if (!b.dynamic || test != b.test) vkCmdSetDepthTestEnable(c, test ? VK_TRUE : VK_FALSE), b.test = test;
    if (!b.dynamic || write != b.write) vkCmdSetDepthWriteEnable(c, write ? VK_TRUE : VK_FALSE), b.write = write;
    if (!b.dynamic || r.depth_op != b.op) vkCmdSetDepthCompareOp(c, vk_compare(r.depth_op)), b.op = r.depth_op;
    if (!b.dynamic || r.bias != b.bias) vkCmdSetDepthBiasEnable(c, r.bias ? VK_TRUE : VK_FALSE), b.bias = r.bias;
    if (r.bias && (!b.dynamic || r.bias_units != b.bias_units || r.bias_factor != b.bias_factor))
        vkCmdSetDepthBias(c, r.bias_units, 0.0f, r.bias_factor), b.bias_units = r.bias_units, b.bias_factor = r.bias_factor;
    if (!b.dynamic || std::memcmp(d.blend_constant, b.blend, sizeof b.blend) != 0)
        vkCmdSetBlendConstants(c, d.blend_constant), std::memcpy(b.blend, d.blend_constant, sizeof b.blend);
    b.dynamic = true;

    // What it reads: only what is not pushed already.
    std::array<VkWriteDescriptorSet, 2 + 48> writes;
    std::array<VkDescriptorBufferInfo, 2> buffers;
    std::array<VkDescriptorImageInfo, 48> images;
    std::array<VkBufferView, 48> texels;
    std::uint32_t n = 0;
    const auto add = [&](std::uint32_t binding, VkDescriptorType type) -> VkWriteDescriptorSet& {
        VkWriteDescriptorSet& w = writes[n++];
        w = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        return w;
    };
    const std::uint32_t block = std::max(p->t.block_size[0], p->t.block_size[1]);
    if (block && d.uniforms) {
        // (The same making of its uniforms, sent this frame already: that.)
        if (!d.uniforms_made || p->sent_made != d.uniforms_made || p->sent_epoch != epoch_) {
            const Ring u = ring(block, static_cast<std::size_t>(props_.limits.minUniformBufferOffsetAlignment));
            std::memcpy(u.host, d.uniforms, block);
            p->sent_buffer = u.buffer, p->sent_at = u.at, p->sent_made = d.uniforms_made, p->sent_epoch = epoch_;
        }
        for (int st = 0; st < 2; ++st) {
            if (!p->t.block_size[st]) continue;
            Pushed& was = b.pushed[static_cast<std::size_t>(kBlockBinding[st])];
            if (was.buffer == p->sent_buffer && was.at == p->sent_at) continue;
            was.buffer = p->sent_buffer, was.at = p->sent_at;
            buffers[static_cast<std::size_t>(st)] = {p->sent_buffer, p->sent_at, block};
            add(static_cast<std::uint32_t>(kBlockBinding[st]), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER).pBufferInfo = &buffers[static_cast<std::size_t>(st)];
        }
    }
    for (std::size_t i = 0; i < p->t.samplers.size() && i < 48; ++i) {
        const SamplerSlot& slot = p->t.samplers[i];
        const Bound& bd = d.bound[i];
        Pushed& was = b.pushed[static_cast<std::size_t>(slot.binding)];
        if (slot.kind == Sampled::buffer) {
            VkBufferView v;
            if (!bd.buffer || bd.buffer_bytes < 16) {
                v = empty_texels_;
            } else {
                Frame& f = frame();
                const std::uint64_t key = mix(mix(reinterpret_cast<std::uintptr_t>(bd.buffer->buffer), bd.buffer_at), bd.buffer_bytes);
                auto it = f.texel_views.find(key);
                if (it == f.texel_views.end()) {
                    VkBufferViewCreateInfo bvi{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
                    bvi.buffer = bd.buffer->buffer;
                    bvi.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    bvi.offset = bd.buffer_at;
                    bvi.range = bd.buffer_bytes / 16 * 16;
                    VkBufferView made;
                    VK_CHECK(vkCreateBufferView(device_, &bvi, nullptr, &made));
                    it = f.texel_views.emplace(key, made).first;
                }
                v = it->second;
            }
            if (was.texel == v) continue;
            was = Pushed{};
            was.texel = v;
            texels[i] = v;
            add(static_cast<std::uint32_t>(slot.binding), VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER).pTexelBufferView = &texels[i];
            continue;
        }
        const std::uint64_t sampler_key = bd.sampler.key();
        if (was.tex && was.tex == bd.tex && was.base == bd.base_level && was.top == bd.max_level && was.sampler_key == sampler_key) continue;
        Texture* t = bd.tex && ((bd.tex->info.samples > 1) == (slot.kind == Sampled::tex2d_ms)) ? bd.tex : empty(slot.kind);
        if (slot.kind == Sampled::tex3d && t->info.kind != TexKind::tex3d) t = empty(slot.kind);
        if (slot.kind == Sampled::cube && t->info.kind != TexKind::cube) t = empty(slot.kind);
        const int levels = t->info.levels;
        const int base = std::clamp(bd.base_level, 0, levels - 1);
        const int top = std::clamp(bd.max_level, base, levels - 1);
        VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
        int layers = 1;
        switch (slot.kind) {
            case Sampled::tex2d_array: type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, layers = t->info.kind == TexKind::tex3d ? 1 : t->info.depth; break;
            case Sampled::tex3d: type = VK_IMAGE_VIEW_TYPE_3D; break;
            case Sampled::cube: type = VK_IMAGE_VIEW_TYPE_CUBE, layers = 6; break;
            default: break;
        }
        was = Pushed{};
        was.tex = bd.tex, was.base = bd.base_level, was.top = bd.max_level, was.sampler_key = sampler_key;
        images[i] = {sampler(bd.sampler), view(t, type, base, top - base + 1, 0, layers), VK_IMAGE_LAYOUT_GENERAL};
        add(static_cast<std::uint32_t>(slot.binding), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).pImageInfo = &images[i];
    }
    if (n) vkCmdPushDescriptorSetKHR(c, VK_PIPELINE_BIND_POINT_GRAPHICS, p->layout, 0, n, writes.data());

    // Its inputs, bound in one call where any differs.
    std::array<VkBuffer, 16> vbs{};
    std::array<VkDeviceSize, 16> offsets{};
    std::uint32_t lo = 16, hi = 0;
    bool differ = false;
    for (std::uint32_t loc = 0; loc < 16; ++loc) {
        if (!(p->t.inputs & (1u << loc))) {
            vbs[loc] = constant_.buffer, offsets[loc] = constant_.at;  // (a gap: anything bound)
            continue;
        }
        lo = std::min(lo, loc), hi = loc + 1;
        const VertexInput& in = d.inputs[loc];
        if (in.buffer) {
            vbs[loc] = in.buffer->buffer, offsets[loc] = in.offset;
        } else {
            const Ring v = ring(16, 16);
            std::memcpy(v.host, in.value, 16);
            vbs[loc] = v.buffer, offsets[loc] = v.at;
        }
        differ = differ || vbs[loc] != b.vbs[loc] || offsets[loc] != b.offsets[loc];
    }
    if (differ && lo < hi) {
        vkCmdBindVertexBuffers(c, lo, hi - lo, vbs.data() + lo, offsets.data() + lo);
        for (std::uint32_t loc = lo; loc < hi; ++loc) b.vbs[loc] = vbs[loc], b.offsets[loc] = offsets[loc];
    }
    if (!d.indirect) {
        vkCmdDraw(c, static_cast<std::uint32_t>(d.count), static_cast<std::uint32_t>(d.instances), static_cast<std::uint32_t>(d.first), 0);
    } else if (multi_draw_) {
        vkCmdDrawIndirect(c, d.indirect->buffer, d.indirect_at, static_cast<std::uint32_t>(d.count), 16);
    } else {
        for (int i = 0; i < d.count; ++i) vkCmdDrawIndirect(c, d.indirect->buffer, d.indirect_at + static_cast<std::size_t>(i) * 16, 1, 16);
    }
}

void VulkanDevice::dispatch(const Dispatch& d) {
    Program* p = d.program;
    if (!p || !p->compute) return;
    end_rendering();
    {
        Frame& f = frame();
        const bool room = f.chunk_now < f.chunks.size() && f.chunks[f.chunk_now].used + (std::size_t{64} << 10) <= f.chunks[f.chunk_now].buffer->size;
        if (!room && f.chunks.size() >= kChunks && asking_ < 0) recycle();
    }
    // (What it reads written before it; what it writes read after it, as
    // the next pass or copy's barrier sees to.)
    if (written_) barrier();
    VkCommandBuffer c = cmd();
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_COMPUTE, p->compute);
    std::array<VkWriteDescriptorSet, 1 + 16 + 16> writes;
    std::array<VkDescriptorBufferInfo, 1 + 16 + 16> buffers;
    std::array<VkDescriptorImageInfo, 16> images;
    std::array<VkBufferView, 16> texels;
    std::uint32_t n = 0;
    const auto add = [&](std::uint32_t binding, VkDescriptorType type, VkDescriptorBufferInfo info) {
        buffers[n] = info;
        VkWriteDescriptorSet& w = writes[n];
        w = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        w.pBufferInfo = &buffers[n];
        ++n;
    };
    // What its samplers read, as a draw's do (no texel buffers in work on the card's own).
    for (std::size_t i = 0; i < p->t.samplers.size() && i < 16; ++i) {
        const SamplerSlot& slot = p->t.samplers[i];
        const Bound& bd = d.bound[i];
        VkWriteDescriptorSet& w = writes[n++];
        w = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = static_cast<std::uint32_t>(slot.binding);
        w.descriptorCount = 1;
        if (slot.kind == Sampled::buffer) {
            texels[i] = empty_texels_;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            w.pTexelBufferView = &texels[i];
            continue;
        }
        Texture* t = bd.tex && ((bd.tex->info.samples > 1) == (slot.kind == Sampled::tex2d_ms)) ? bd.tex : empty(slot.kind);
        if (slot.kind == Sampled::tex3d && t->info.kind != TexKind::tex3d) t = empty(slot.kind);
        if (slot.kind == Sampled::cube && t->info.kind != TexKind::cube) t = empty(slot.kind);
        const int levels = t->info.levels;
        const int base = std::clamp(bd.base_level, 0, levels - 1);
        const int top = std::clamp(bd.max_level, base, levels - 1);
        VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
        int layers = 1;
        switch (slot.kind) {
            case Sampled::tex2d_array: type = VK_IMAGE_VIEW_TYPE_2D_ARRAY, layers = t->info.kind == TexKind::tex3d ? 1 : t->info.depth; break;
            case Sampled::tex3d: type = VK_IMAGE_VIEW_TYPE_3D; break;
            case Sampled::cube: type = VK_IMAGE_VIEW_TYPE_CUBE, layers = 6; break;
            default: break;
        }
        images[i] = {sampler(bd.sampler), view(t, type, base, top - base + 1, 0, layers), VK_IMAGE_LAYOUT_GENERAL};
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &images[i];
    }
    if (const std::uint32_t block = p->t.block_size[0]; block && d.uniforms) {
        const Ring u = ring(block, static_cast<std::size_t>(props_.limits.minUniformBufferOffsetAlignment));
        std::memcpy(u.host, d.uniforms, block);
        add(static_cast<std::uint32_t>(kBlockBinding[0]), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {u.buffer, u.at, block});
    }
    for (const StorageSlot& s : p->t.storage) {
        const Storage& st = d.storage[s.binding];
        if (!st.buffer) return;
        add(static_cast<std::uint32_t>(kFirstStorage + s.binding), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {st.buffer->buffer, st.at, st.bytes});
    }
    if (n) vkCmdPushDescriptorSetKHR(c, VK_PIPELINE_BIND_POINT_COMPUTE, p->layout, 0, n, writes.data());
    vkCmdDispatch(c, static_cast<std::uint32_t>(d.groups[0]), static_cast<std::uint32_t>(d.groups[1]), static_cast<std::uint32_t>(d.groups[2]));
    written_ = true;
}

// --- questions ------------------------------------------------------------------

// A frame's part of the pools made ready to ask again, the card done with
// it: the answers found there kept first, for whoever reads them later.
void VulkanDevice::reset_places(std::size_t frame) {
    const std::int64_t from = static_cast<std::int64_t>(frame * kPerFrame), to = from + kPerFrame;
    for (Slot& q : slots_)
        if (q.place >= from && q.place < to && !q.recording) {
            std::uint64_t v = 0;
            if (vkGetQueryPoolResults(device_, pools_[q.kind], static_cast<std::uint32_t>(q.place), 1, sizeof v, &v, sizeof v, VK_QUERY_RESULT_64_BIT) !=
                VK_SUCCESS)
                v = 0;
            q.kept = q.kind ? static_cast<std::uint64_t>(static_cast<double>(v) * period_) : v;
            q.place = -2;
        }
    for (int k = 0; k < 2; ++k)
        if (taken_[frame][static_cast<std::size_t>(k)])
            vkResetQueryPool(device_, pools_[k], static_cast<std::uint32_t>(frame * kPerFrame), taken_[frame][static_cast<std::size_t>(k)]);
    taken_[frame] = {0, 0};
}

// A fresh place for a question asked: the next in this frame's part.
std::int64_t VulkanDevice::place(int kind, int slot) {
    const auto f = static_cast<std::size_t>(frame_index_ % kFrames);
    std::uint32_t& taken = taken_[f][static_cast<std::size_t>(kind)];
    if (taken == kPerFrame) {
        // (A frame long in coming has asked all it may: once the card has
        // answered, every part starts again.)
        submit(true);
        for (std::size_t i = 0; i < kFrames; ++i) reset_places(i);
    }
    const std::int64_t at = static_cast<std::int64_t>(f * kPerFrame + taken++);
    Slot& q = slots_[static_cast<std::size_t>(slot)];
    q.kind = kind, q.place = at;
    if (!q.recording) q.recording = true, recorded_.push_back(slot);
    return at;
}

void VulkanDevice::begin_query(int slot) {
    if (asking_ >= 0) end_query(asking_);
    const std::int64_t at = place(0, slot);
    // (Nothing to draw into: it is never begun, and finds nothing.)
    if (!begin_rendering()) return;
    vkCmdBeginQuery(cmd(), pools_[0], static_cast<std::uint32_t>(at), 0);
    asking_ = slot;
}

void VulkanDevice::end_query(int slot) {
    if (asking_ != slot || !rendering_) return;
    vkCmdEndQuery(cmd(), pools_[0], static_cast<std::uint32_t>(slots_[static_cast<std::size_t>(slot)].place));
    asking_ = -1;
}

void VulkanDevice::stamp(int slot) {
    const std::int64_t at = place(1, slot);
    vkCmdWriteTimestamp2(cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pools_[1], static_cast<std::uint32_t>(at));
}

bool VulkanDevice::answer(int slot, bool wait, std::uint64_t& out) {
    Slot& q = slots_[static_cast<std::size_t>(slot)];
    out = q.place == -2 ? q.kept : 0;
    if (q.place < 0) return true;
    if (q.recording) {
        if (!wait) return false;
        if (asking_ == slot) end_rendering();
        submit(true);
    }
    std::uint64_t now = 0;
    vkGetSemaphoreCounterValue(device_, timeline_, &now);
    if (q.at > now) {
        if (!wait) return false;
        wait_value(q.at);
    }
    if (q.place == -2) return out = q.kept, true;
    std::uint64_t v = 0;
    // (One begun and never ended, or never drawn into, has found nothing.)
    if (vkGetQueryPoolResults(device_, pools_[q.kind], static_cast<std::uint32_t>(q.place), 1, sizeof v, &v, sizeof v, VK_QUERY_RESULT_64_BIT) !=
        VK_SUCCESS)
        v = 0;
    out = q.kind ? static_cast<std::uint64_t>(static_cast<double>(v) * period_) : v;
    return true;
}

void VulkanDevice::only_if(int slot) {
    if (!conditional_) return;
    if (only_on_) vkCmdEndConditionalRenderingEXT(cmd()), only_on_ = false;
    only_buffer_ = VK_NULL_HANDLE;
    // (One never asked has no answer to wait for: what follows is drawn.)
    if (slot < 0) return;
    const Slot& q = slots_[static_cast<std::size_t>(slot)];
    // (Only what this frame asked: another frame's part may be reset while
    // the card still reads it. What follows anything else is drawn.)
    const std::int64_t mine = static_cast<std::int64_t>(frame_index_ % kFrames) * kPerFrame;
    if (q.kind != 0 || q.place < mine || q.place >= mine + kPerFrame) return;
    if (asking_ == slot) end_rendering();
    end_rendering();
    if (written_) barrier();
    const Ring r = ring(4, 4);
    vkCmdCopyQueryPoolResults(cmd(), pools_[0], static_cast<std::uint32_t>(q.place), 1, r.buffer, r.at, 4, VK_QUERY_RESULT_WAIT_BIT);
    written_ = true;
    only_buffer_ = r.buffer, only_at_ = r.at;
}

// --- the window -------------------------------------------------------------------

bool VulkanDevice::make_swapchain(bool vsync) {
    vkDeviceWaitIdle(device_);
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps));
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(window_, &fw, &fh);
    if (caps.currentExtent.width != UINT32_MAX) extent_ = caps.currentExtent;
    else extent_ = {static_cast<std::uint32_t>(std::max(fw, 1)), static_cast<std::uint32_t>(std::max(fh, 1))};
    if (extent_.width == 0 || extent_.height == 0) return false;
    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, fmts.data());
    VkSurfaceFormatKHR pick = fmts[0];
    for (auto& f : fmts)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pick = f;
            break;
        }
    swap_format_ = pick.format;
    std::uint32_t nm = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &nm, nullptr);
    std::vector<VkPresentModeKHR> modes(nm);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &nm, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync)
        for (VkPresentModeKHR m : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR})
            if (std::find(modes.begin(), modes.end(), m) != modes.end()) {
                mode = m;
                break;
            }
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface_;
    sci.minImageCount = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount) sci.minImageCount = std::min(sci.minImageCount, caps.maxImageCount);
    sci.imageFormat = pick.format;
    sci.imageColorSpace = pick.colorSpace;
    sci.imageExtent = extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = mode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = swapchain_;
    VkSwapchainKHR made;
    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &made) != VK_SUCCESS) return false;
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = made;
    vsync_ = vsync;
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swap_images_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, swap_images_.data());
    for (VkSemaphore s : swap_done_) vkDestroySemaphore(device_, s, nullptr);
    swap_done_.assign(n, VK_NULL_HANDLE);
    for (VkSemaphore& s : swap_done_) {
        VkSemaphoreCreateInfo bin{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device_, &bin, nullptr, &s));
    }
    return true;
}

void VulkanDevice::make_window_targets() {
    if (window_color_) destroy(window_color_);
    if (window_depth_) destroy(window_depth_);
    TextureInfo c;
    c.format = Format::rgba8;
    c.w = static_cast<int>(extent_.width), c.h = static_cast<int>(extent_.height);
    window_color_ = create_texture(c);
    TextureInfo d = c;
    d.format = Format::d24;
    window_depth_ = create_texture(d);
}

void VulkanDevice::present(bool vsync) {
    int fw = 0, fh = 0;
    glfwGetFramebufferSize(window_, &fw, &fh);
    const bool resized = fw > 0 && fh > 0 && (static_cast<std::uint32_t>(fw) != extent_.width || static_cast<std::uint32_t>(fh) != extent_.height);
    std::uint32_t index = 0;
    VkResult acquired = VK_ERROR_OUT_OF_DATE_KHR;
    if (!resized && vsync == vsync_) acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame().acquired, VK_NULL_HANDLE, &index);
    if (acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR) {
        end_rendering();
        if (written_) barrier();
        VkCommandBuffer c = cmd();
        VkImageMemoryBarrier2 ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        ib.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        ib.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ib.image = swap_images_[index];
        ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &ib;
        vkCmdPipelineBarrier2(c, &di);
        // GL's first row is its lowest: turned over as it is shown.
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[0] = {0, window_color_->info.h, 0};
        blit.srcOffsets[1] = {window_color_->info.w, 0, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[1] = {static_cast<std::int32_t>(extent_.width), static_cast<std::int32_t>(extent_.height), 1};
        vkCmdBlitImage(c, window_color_->image, VK_IMAGE_LAYOUT_GENERAL, swap_images_[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_NEAREST);
        ib.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        ib.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ib.dstAccessMask = 0;
        ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier2(c, &di);
        submit(false, frame().acquired, swap_done_[index]);
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &swap_done_[index];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain_;
        pi.pImageIndices = &index;
        const VkResult shown = vkQueuePresentKHR(queue_, &pi);
        if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR) acquired = VK_ERROR_OUT_OF_DATE_KHR;
    } else {
        submit(false);
    }
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR || resized || vsync != vsync_) {
        if (make_swapchain(vsync) && (window_color_->info.w != static_cast<int>(extent_.width) || window_color_->info.h != static_cast<int>(extent_.height)))
            make_window_targets();
    }
    ++frame_index_;
    retire(frame());
}

void VulkanDevice::finish() { submit(true); }

}  // namespace

std::unique_ptr<Device> make_vulkan(GLFWwindow* window, std::string& why) {
    auto d = std::make_unique<VulkanDevice>();
    if (!d->start(window, why)) return nullptr;
    return d;
}

}  // namespace sg::gl::native
