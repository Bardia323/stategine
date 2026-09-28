// Stategine - the algebra's batches on a Vulkan device.
//
// One compute dispatch of algebra.comp (SPIR-V, built with the engine into
// sg_algebra_spv.h), a thread to each job, on the first device that has a
// compute queue and doubles (shaderFloat64). The device and its pipeline are
// made once and kept; each batch gets buffers of its own, host-visible, so
// nothing is staged.
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "sg/gpu/AlgebraBackend.hpp"
#include "sg_algebra_spv.h"  // const uint32_t sg_algebra_spv[]; const size_t sg_algebra_spv_size (bytes)

namespace sg::gpu::vulkan {

namespace {

constexpr uint32_t kBindings = 14;

struct Device {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    bool ok = false;

    Device() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "stategine";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) return;
        uint32_t n = 0;
        vkEnumeratePhysicalDevices(instance, &n, nullptr);
        std::vector<VkPhysicalDevice> all(n);
        vkEnumeratePhysicalDevices(instance, &n, all.data());
        for (VkPhysicalDevice p : all) {
            VkPhysicalDeviceFeatures f{};
            vkGetPhysicalDeviceFeatures(p, &f);
            if (!f.shaderFloat64) continue;  // doubles, or not this device
            uint32_t q = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(p, &q, nullptr);
            std::vector<VkQueueFamilyProperties> qs(q);
            vkGetPhysicalDeviceQueueFamilyProperties(p, &q, qs.data());
            for (uint32_t i = 0; i < q; ++i)
                if (qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    physical = p, family = i;
                    break;
                }
            if (physical) break;
        }
        if (!physical) return;
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures want{};
        want.shaderFloat64 = VK_TRUE;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.pEnabledFeatures = &want;
        if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS) return;
        vkGetDeviceQueue(device, family, 0, &queue);

        std::vector<VkDescriptorSetLayoutBinding> b(kBindings);
        for (uint32_t i = 0; i < kBindings; ++i)
            b[i] = VkDescriptorSetLayoutBinding{i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lci.bindingCount = kBindings;
        lci.pBindings = b.data();
        if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &set_layout) != VK_SUCCESS) return;
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)};
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &set_layout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &push;
        if (vkCreatePipelineLayout(device, &plci, nullptr, &pipeline_layout) != VK_SUCCESS) return;
        VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smci.codeSize = sg_algebra_spv_size;
        smci.pCode = sg_algebra_spv;
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &smci, nullptr, &module) != VK_SUCCESS) return;
        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpci.stage = VkPipelineShaderStageCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = module;
        cpci.stage.pName = "main";
        cpci.layout = pipeline_layout;
        const VkResult made = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline);
        vkDestroyShaderModule(device, module, nullptr);
        if (made != VK_SUCCESS) return;
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex = family;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device, &pci, nullptr, &pool) != VK_SUCCESS) return;
        ok = true;
    }
    ~Device() {
        if (device) {
            vkDeviceWaitIdle(device);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
            if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }

    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) const {
        VkPhysicalDeviceMemoryProperties m{};
        vkGetPhysicalDeviceMemoryProperties(physical, &m);
        for (uint32_t i = 0; i < m.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (m.memoryTypes[i].propertyFlags & want) == want) return i;
        return UINT32_MAX;
    }
};

Device& device() {
    static Device d;
    return d;
}

// A batch's buffers, host-visible, freed with it.
struct Buffers {
    Device& d;
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memory;
    std::vector<void*> mapped;
    std::vector<VkDeviceSize> sizes;
    explicit Buffers(Device& dev) : d(dev) {}
    ~Buffers() {
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            if (mapped[i]) vkUnmapMemory(d.device, memory[i]);
            vkDestroyBuffer(d.device, buffers[i], nullptr);
            vkFreeMemory(d.device, memory[i], nullptr);
        }
    }
    bool add(const void* data, VkDeviceSize bytes) {
        bytes = std::max<VkDeviceSize>(bytes, 16);
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = bytes;
        bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VkBuffer b = VK_NULL_HANDLE;
        if (vkCreateBuffer(d.device, &bci, nullptr, &b) != VK_SUCCESS) return false;
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(d.device, b, &req);
        const uint32_t type = d.memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = type;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        if (type == UINT32_MAX || vkAllocateMemory(d.device, &mai, nullptr, &mem) != VK_SUCCESS) {
            vkDestroyBuffer(d.device, b, nullptr);
            return false;
        }
        vkBindBufferMemory(d.device, b, mem, 0);
        void* p = nullptr;
        vkMapMemory(d.device, mem, 0, bytes, 0, &p);
        std::memset(p, 0, static_cast<std::size_t>(bytes));
        if (data) std::memcpy(p, data, static_cast<std::size_t>(bytes));
        buffers.push_back(b), memory.push_back(mem), mapped.push_back(p), sizes.push_back(bytes);
        return true;
    }
    template <typename T>
    bool add(const std::vector<T>& v) {
        return v.empty() ? add(nullptr, 16) : add(v.data(), v.size() * sizeof(T));
    }
};

}  // namespace

bool available() { return device().ok; }

bool run(const algebra::Batch& b, std::vector<algebra::Verdict>& out) {
    Device& d = device();
    if (!d.ok) return false;
    const uint32_t n = static_cast<uint32_t>(b.size());
    std::vector<uint32_t> jobs, scratch_first, how(b.cmp_how.begin(), b.cmp_how.end());
    uint64_t scratch = 0;
    for (const algebra::Batch::Job& j : b.jobs) {
        const uint32_t f[8] = {j.lhs_first, j.lhs_count, j.rhs_first, j.rhs_count, j.x_first, j.slots, j.cmp_first, j.cmp_count};
        jobs.insert(jobs.end(), f, f + 8);
        scratch_first.push_back(static_cast<uint32_t>(scratch));
        scratch += 2ull * j.slots;
    }
    if (scratch > 0xffffffffull) return false;
    Buffers buf(d);
    if (!buf.add(jobs) || !buf.add(b.row_slot) || !buf.add(b.row_first) || !buf.add(b.row_count) || !buf.add(b.row_bias) ||
        !buf.add(b.term_col) || !buf.add(b.term_k) || !buf.add(b.start) || !buf.add(b.cmp_slot) || !buf.add(how) ||
        !buf.add(scratch_first) || !buf.add(nullptr, scratch * sizeof(double)) || !buf.add(nullptr, n * sizeof(uint32_t)) ||
        !buf.add(nullptr, n * sizeof(uint32_t)))
        return false;

    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kBindings};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &size;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(d.device, &dpci, nullptr, &dpool) != VK_SUCCESS) return false;
    std::unique_ptr<void, std::function<void(void*)>> free_pool(reinterpret_cast<void*>(1), [&](void*) { vkDestroyDescriptorPool(d.device, dpool, nullptr); });
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &d.set_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(d.device, &dsai, &set) != VK_SUCCESS) return false;
    std::vector<VkDescriptorBufferInfo> infos(kBindings);
    std::vector<VkWriteDescriptorSet> writes(kBindings);
    for (uint32_t i = 0; i < kBindings; ++i) {
        infos[i] = VkDescriptorBufferInfo{buf.buffers[i], 0, VK_WHOLE_SIZE};
        writes[i] = VkWriteDescriptorSet{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(d.device, kBindings, writes.data(), 0, nullptr);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = d.pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(d.device, &cbai, &cmd) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, d.pipeline_layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, d.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &n);
    vkCmdDispatch(cmd, (n + 63) / 64, 1, 1);
    vkEndCommandBuffer(cmd);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(d.device, &fci, nullptr, &fence);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const bool done = vkQueueSubmit(d.queue, 1, &submit, fence) == VK_SUCCESS &&
                      vkWaitForFences(d.device, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    vkDestroyFence(d.device, fence, nullptr);
    vkFreeCommandBuffers(d.device, d.pool, 1, &cmd);
    if (!done) return false;
    const uint32_t* agree = static_cast<const uint32_t*>(buf.mapped[12]);
    const uint32_t* where = static_cast<const uint32_t*>(buf.mapped[13]);
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) out[i] = algebra::Verdict{static_cast<uint8_t>(agree[i]), where[i]};
    return true;
}

}  // namespace sg::gpu::vulkan
