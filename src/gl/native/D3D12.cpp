// Stategine - the native device on D3D12: committed resources whose states
// are kept and changed as they are used, a root signature a program, and its
// HLSL compiled by DXC (shader model 6) - or, where DXC is not to be had, by
// the system's own compiler (shader model 5) - and kept on disk once built.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>  // (ID3DBlob, for root signatures)
#include <dxcapi.h>
#include <dxgi1_6.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>
#include <vector>

#include "Device.hpp"

namespace sg::gl::native {

struct Texture {
    TextureInfo info;
    ID3D12Resource* res = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // the resource's (typeless where its views differ)
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    std::unordered_map<std::uint64_t, D3D12_CPU_DESCRIPTOR_HANDLE> srvs, rtvs, dsvs;
};

struct Buffer {
    ID3D12Resource* res = nullptr;
    std::size_t size = 0;
    void* host = nullptr;
    // A buffer on the card has its state kept; a host one is promoted as it
    // is used and falls back to common after each submission.
    bool tracked = false;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct Program {
    Translated t;
    ID3D12RootSignature* root = nullptr;
    // Its uniforms as last sent: which making, where, and in which epoch of
    // the frames' uploads (sent again once those are reused).
    D3D12_GPU_VIRTUAL_ADDRESS sent_va = 0;
    std::uint64_t sent_made = 0, sent_epoch = ~std::uint64_t{0};
    std::vector<char> code[2];
    std::unordered_map<std::uint64_t, ID3D12PipelineState*> psos;
    int tables = 0;  // the sampler slots its tables hold
    ID3D12PipelineState* compute = nullptr;  // a compute program's one
};

namespace {

// The debug layer's messages (SG_D3D12_DEBUG=1), said when something fails.
ID3D12InfoQueue* info_queue = nullptr;

void say_messages() {
    if (!info_queue) return;
    for (UINT64 i = 0, n = info_queue->GetNumStoredMessages(); i < n; ++i) {
        SIZE_T size = 0;
        info_queue->GetMessage(i, nullptr, &size);
        std::vector<char> m(size);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(m.data());
        if (SUCCEEDED(info_queue->GetMessage(i, msg, &size))) std::fprintf(stderr, "d3d12: %s\n", msg->pDescription);
    }
    info_queue->ClearStoredMessages();
}

void hr_check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        say_messages();
        std::fprintf(stderr, "stategine d3d12: %s failed (0x%08lx)\n", what, static_cast<unsigned long>(hr));
        std::abort();
    }
}
#define HR(x) hr_check((x), #x)

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v) { return (h ^ v) * 0x100000001B3ull + (h >> 29); }

std::uint64_t hash_bytes(const void* data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

std::size_t round4(int v) { return static_cast<std::size_t>((v + 3) & ~3); }

DXGI_FORMAT resource_format(Format f) {
    switch (f) {
        case Format::rgba8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::srgba8: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case Format::bgra8: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::rgba16f: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case Format::rgba32f: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::rg16f: return DXGI_FORMAT_R16G16_FLOAT;
        case Format::rg32f: return DXGI_FORMAT_R32G32_FLOAT;
        case Format::r32f: return DXGI_FORMAT_R32_FLOAT;
        case Format::r32ui: return DXGI_FORMAT_R32_UINT;
        case Format::rg32ui: return DXGI_FORMAT_R32G32_UINT;
        case Format::rgba8ui: return DXGI_FORMAT_R8G8B8A8_UINT;
        case Format::bc7: return DXGI_FORMAT_BC7_UNORM;
        case Format::bc7_srgb: return DXGI_FORMAT_BC7_UNORM_SRGB;
        case Format::d24: return DXGI_FORMAT_R24G8_TYPELESS;
        case Format::d32f: return DXGI_FORMAT_R32_TYPELESS;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT read_format(Format f) {
    switch (f) {
        case Format::srgba8: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case Format::d24: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case Format::d32f: return DXGI_FORMAT_R32_FLOAT;
        default: return resource_format(f);
    }
}

DXGI_FORMAT draw_format(const Attachment& a) {
    switch (a.tex->info.format) {
        case Format::srgba8: return a.encode ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::d24: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case Format::d32f: return DXGI_FORMAT_D32_FLOAT;
        default: return resource_format(a.tex->info.format);
    }
}

// How a texture's texels lie when copied to or from a buffer.
DXGI_FORMAT copy_format(Format f) {
    switch (f) {
        case Format::srgba8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::d24: return DXGI_FORMAT_R24G8_TYPELESS;
        case Format::d32f: return DXGI_FORMAT_R32_FLOAT;
        default: return resource_format(f);
    }
}

D3D12_BLEND d3d_factor(Factor f) {
    switch (f) {
        case Factor::zero: return D3D12_BLEND_ZERO;
        case Factor::src_alpha: return D3D12_BLEND_SRC_ALPHA;
        case Factor::one_minus_src_alpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case Factor::constant_alpha: return D3D12_BLEND_BLEND_FACTOR;
        case Factor::one_minus_constant_alpha: return D3D12_BLEND_INV_BLEND_FACTOR;
        default: return D3D12_BLEND_ONE;
    }
}

D3D12_TEXTURE_ADDRESS_MODE d3d_wrap(SamplerInfo::Wrap w) {
    return w == SamplerInfo::repeat ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
           : w == SamplerInfo::clamp_border ? D3D12_TEXTURE_ADDRESS_MODE_BORDER
                                            : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
}

// A heap of descriptors the processor writes, handed out one by one.
struct CpuHeap {
    ID3D12DescriptorHeap* heap = nullptr;
    std::size_t step = 0, start = 0;
    std::vector<std::uint32_t> spare;
    std::uint32_t next = 0, size = 0;
    D3D12_DESCRIPTOR_HEAP_TYPE type{};
    void make(ID3D12Device* d, D3D12_DESCRIPTOR_HEAP_TYPE t, std::uint32_t n) {
        type = t;
        D3D12_DESCRIPTOR_HEAP_DESC hd{t, n, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        HR(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
        step = d->GetDescriptorHandleIncrementSize(t);
        start = heap->GetCPUDescriptorHandleForHeapStart().ptr;
        size = n;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE take() {
        std::uint32_t i;
        if (!spare.empty()) {
            i = spare.back();
            spare.pop_back();
        } else if (next < size) {
            i = next++;
        } else {
            std::fprintf(stderr, "stategine d3d12: out of descriptors (%u of a heap of type %d)\n", size, static_cast<int>(type));
            std::abort();
        }
        return {start + i * step};
    }
    void give(D3D12_CPU_DESCRIPTOR_HANDLE h) { spare.push_back(static_cast<std::uint32_t>((h.ptr - start) / step)); }
};

// --- compiling HLSL ---------------------------------------------------------------

struct ShaderCompiler {
    IDxcCompiler* dxc = nullptr;
    IDxcLibrary* library = nullptr;
    std::string cache;  // where built shaders are kept

    ~ShaderCompiler() { release(dxc), release(library); }

    // DXC: beside the program, where SG_DXC says, or the Windows SDK's.
    // (Not the system's own compiler: shader model 5 cannot take some of the
    // renderer's programs - a gradient in a loop it cannot unroll.)
    bool start() {
        std::vector<std::filesystem::path> dirs;
        wchar_t exe[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) dirs.push_back(std::filesystem::path(exe).parent_path());
        if (const char* e = std::getenv("SG_DXC")) dirs.emplace_back(e);
        const std::filesystem::path kits = L"C:/Program Files (x86)/Windows Kits/10";
        dirs.push_back(kits / L"Redist/D3D/x64");
        std::error_code ec;
        std::vector<std::filesystem::path> bins;
        for (const auto& v : std::filesystem::directory_iterator(kits / L"bin", ec))
            if (std::filesystem::exists(v.path() / L"x64/dxcompiler.dll", ec)) bins.push_back(v.path() / L"x64");
        std::sort(bins.rbegin(), bins.rend());
        dirs.insert(dirs.end(), bins.begin(), bins.end());
        for (const std::filesystem::path& d : dirs) {
            if (!std::filesystem::exists(d / L"dxcompiler.dll", ec)) continue;
            // (Its validator first, from the same place: it is asked for by name.)
            LoadLibraryW((d / L"dxil.dll").c_str());
            HMODULE m = LoadLibraryW((d / L"dxcompiler.dll").c_str());
            if (!m) continue;
            auto create = reinterpret_cast<DxcCreateInstanceProc>(reinterpret_cast<void*>(GetProcAddress(m, "DxcCreateInstance")));
            if (create && SUCCEEDED(create(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(&dxc))) &&
                SUCCEEDED(create(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(&library))))
                break;
            release(dxc), release(library);
        }
        if (const char* local = std::getenv("LOCALAPPDATA")) {
            cache = std::string(local) + "/stategine/shaders";
            std::filesystem::create_directories(cache, ec);
        }
        return dxc != nullptr;
    }

    // HLSL built for `stage` ("vs", "ps", "cs") as DXIL, shader model 6;
    // kept on disk by what it was built from.
    bool compile(const std::string& src, const char* stage, std::vector<char>& out, std::string& log) {
        const std::string target = std::string(stage) + "_6_0";
        const std::uint64_t key = hash_bytes(target.data(), target.size(), hash_bytes(src.data(), src.size()));
        char name[32];
        std::snprintf(name, sizeof name, "%016llx.bin", static_cast<unsigned long long>(key));
        const std::string path = cache.empty() ? std::string() : cache + "/" + name;
        if (!path.empty()) {
            std::ifstream in(path, std::ios::binary);
            if (in) {
                out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                if (!out.empty()) return true;
            }
        }
        out.clear();
        IDxcBlobEncoding* blob = nullptr;
        library->CreateBlobWithEncodingFromPinned(src.data(), static_cast<UINT32>(src.size()), CP_UTF8, &blob);
        const std::wstring wtarget(target.begin(), target.end());
        const wchar_t* args[] = {L"-O3"};
        IDxcOperationResult* r = nullptr;
        HRESULT status = E_FAIL;
        if (blob && SUCCEEDED(dxc->Compile(blob, L"stategine.hlsl", L"main", wtarget.c_str(), args, 1, nullptr, 0, nullptr, &r)) && r)
            r->GetStatus(&status);
        if (SUCCEEDED(status)) {
            IDxcBlob* code = nullptr;
            r->GetResult(&code);
            const auto* p = static_cast<const char*>(code->GetBufferPointer());
            out.assign(p, p + code->GetBufferSize());
            release(code);
        } else if (r) {
            IDxcBlobEncoding* e = nullptr;
            r->GetErrorBuffer(&e);
            if (e) log += std::string(static_cast<const char*>(e->GetBufferPointer()), e->GetBufferSize());
            release(e);
        }
        release(r), release(blob);
        if (out.empty()) return false;
        if (!path.empty()) std::ofstream(path, std::ios::binary).write(out.data(), static_cast<std::streamsize>(out.size()));
        return true;
    }

    bool build(const Translated& t, int stage, std::vector<char>& out, std::string& log) {
        return !t.hlsl[stage].empty() && compile(t.hlsl[stage], t.compute ? "cs" : stage ? "ps" : "vs", out, log);
    }
};

const char* kShowVs = R"(
float4 main(uint id : SV_VertexID) : SV_Position {
    return float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0, 0.0, 1.0);
}
)";
// GL's first row is its lowest: the picture turned over as it is shown.
const char* kShowPs = R"(
Texture2D<float4> picture : register(t0);
float4 main(float4 at : SV_Position) : SV_Target {
    uint w, h;
    picture.GetDimensions(w, h);
    int2 p = int2(at.xy);
    return picture.Load(int3(min(p.x, int(w) - 1), max(int(h) - 1 - p.y, 0), 0));
}
)";

class D3D12Device final : public Device {
public:
    bool start(GLFWwindow* window, std::string& why);
    ~D3D12Device() override;

    const char* name() const override { return name_.c_str(); }
    float y_flip() const override { return -1.0f; }
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
    void finish() override { submit(true); }

private:
    static constexpr int kFrames = 2;
    static constexpr std::uint32_t kVisible = 1u << 19;  // shader-visible descriptors, both frames
    static constexpr std::uint32_t kSamplerTables = 2048;
    static constexpr std::size_t kChunks = 32;  // a frame's uploads before it waits to reuse them

    struct Chunk {
        Buffer* buffer = nullptr;
        std::size_t used = 0;
    };
    // A picture read into a buffer, rows apart as the card lays them: put
    // tight where it was asked once it is read.
    struct Repack {
        Buffer* staging;
        std::size_t pitch, row, rows;
        Buffer* into;
        std::size_t at;
    };
    struct Frame {
        ID3D12CommandAllocator* allocator = nullptr;
        std::uint64_t done_at = 0;
        std::vector<Chunk> chunks;
        std::size_t chunk_now = 0;
        std::vector<Buffer*> dead_buffers;
        std::vector<Texture*> dead_textures;
        std::vector<ID3D12PipelineState*> dead_psos;
        std::uint32_t visible_used = 0;
    };
    struct Ring {
        Buffer* buffer;
        std::size_t at;
        std::uint8_t* host;
        D3D12_GPU_VIRTUAL_ADDRESS va;
    };

    Frame& frame() { return frames_[frame_index_ % kFrames]; }
    ID3D12GraphicsCommandList* list();
    Ring ring(std::size_t bytes, std::size_t align);
    void recycle();
    std::uint32_t visible(std::uint32_t n);
    void submit(bool wait);
    void wait_value(std::uint64_t v);
    void retire(Frame&);
    void free_texture(Texture*);
    void free_buffer(Buffer*);
    void to_state(Texture*, D3D12_RESOURCE_STATES);
    void to_state(Buffer*, D3D12_RESOURCE_STATES);
    void flush_barriers();
    D3D12_CPU_DESCRIPTOR_HANDLE srv(Texture*, Sampled kind, int base, int levels);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(const Attachment&);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv(const Attachment&);
    D3D12_GPU_DESCRIPTOR_HANDLE sampler_table(const std::uint64_t* keys, std::size_t count);
    Texture* empty(Sampled kind);
    ID3D12PipelineState* pso(Program*, const Draw&);
    void bind_target();
    bool make_swapchain();
    void make_window_targets();
    void apply_repacks(Buffer*);

    GLFWwindow* window_ = nullptr;
    HWND hwnd_ = nullptr;
    std::string name_;
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serialize_ = nullptr;
    IDXGIFactory4* factory_ = nullptr;
    ID3D12Device* device_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12GraphicsCommandList* list_ = nullptr;
    bool open_ = false;
    ID3D12Fence* fence_ = nullptr;
    HANDLE event_ = nullptr;
    std::uint64_t fence_value_ = 0;
    std::array<Frame, kFrames> frames_{};
    std::uint64_t frame_index_ = 0;
    std::vector<D3D12_RESOURCE_BARRIER> barriers_;
    std::vector<Repack> repacks_;

    CpuHeap rtv_heap_, dsv_heap_, srv_heap_;
    ID3D12DescriptorHeap* visible_ = nullptr;
    ID3D12DescriptorHeap* samplers_visible_ = nullptr;
    std::size_t visible_step_ = 0, sampler_step_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE visible_cpu_{}, sampler_cpu_{};
    D3D12_GPU_DESCRIPTOR_HANDLE visible_gpu_{}, sampler_gpu_{};
    std::map<std::vector<std::uint64_t>, std::uint32_t> sampler_tables_;
    std::uint32_t sampler_next_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE null_texel_{}, null_rtv_{};

    ShaderCompiler compiler_;
    IDXGISwapChain3* swapchain_ = nullptr;
    std::vector<ID3D12Resource*> backbuffers_;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> back_rtvs_;
    int width_ = 0, height_ = 0;
    bool tearing_ = false;
    ID3D12RootSignature* show_root_ = nullptr;
    ID3D12PipelineState* show_pso_ = nullptr;
    ID3D12CommandSignature* draws_ = nullptr;  // GL's draw commands, read from a buffer
    Texture* window_color_ = nullptr;
    Texture* window_depth_ = nullptr;

    Target target_;
    bool target_bound_ = false;
    bool drawing_ = false;  // a draw being recorded: nothing submitted under it
    bool splitting_ = false;  // a draw being made slice by slice (draw)
    std::uint64_t epoch_ = 0;  // each time the uploads and descriptors a draw may point at are reused, another
    // What the list being recorded has set, so a draw sets only what differs.
    // Forgotten as the list is opened again.
    struct Bound_ {
        D3D12_GPU_VIRTUAL_ADDRESS cbv = 0;
        std::uint64_t table = ~std::uint64_t{0}, table_set = 0, table_key = 0, table_epoch = 0;
        std::uint64_t samplers = ~std::uint64_t{0}, samplers_key = 0;
        std::array<D3D12_VERTEX_BUFFER_VIEW, 16> vbs{};
        bool ia = false, rs = false;
        D3D12_VIEWPORT viewport{};
        D3D12_RECT scissor{};
        float blend[4] = {};
    } bound_;
    ID3D12RootSignature* root_now_ = nullptr;
    ID3D12PipelineState* pso_now_ = nullptr;
    std::array<Texture*, 8> empties_{};

    // Questions: a heap of each kind (0 occlusion, 1 time), their answers
    // resolved into a buffer the processor reads, and for each slot the
    // submission it was in.
    struct Slot {
        bool used = false, recording = false;
        std::uint64_t at = 0;
    };
    ID3D12QueryHeap* query_heaps_[2] = {};
    Buffer* answers_ = nullptr;
    Buffer* predicates_ = nullptr;
    std::vector<Slot> slots_;
    std::vector<std::uint8_t> kind_of_;
    std::vector<int> recorded_;
    int asking_ = -1;
    int only_ = -1;  // the question what is drawn waits on, if any
    double tick_ns_ = 1.0;
};

D3D12Device::~D3D12Device() {
    if (!device_) {
        release(factory_);
        return;
    }
    submit(true);
    for (Frame& f : frames_) {
        retire(f);
        for (Chunk& c : f.chunks) free_buffer(c.buffer);
        release(f.allocator);
    }
    for (Repack& r : repacks_) free_buffer(r.staging);
    for (Texture* t : empties_)
        if (t) free_texture(t);
    if (window_color_) free_texture(window_color_);
    if (window_depth_) free_texture(window_depth_);
    if (answers_) free_buffer(answers_);
    if (predicates_) free_buffer(predicates_);
    for (ID3D12QueryHeap*& q : query_heaps_) release(q);
    for (ID3D12Resource*& b : backbuffers_) release(b);
    release(show_pso_), release(show_root_), release(draws_);
    release(swapchain_);
    release(visible_), release(samplers_visible_);
    release(rtv_heap_.heap), release(dsv_heap_.heap), release(srv_heap_.heap);
    release(list_), release(fence_), release(queue_);
    if (event_) CloseHandle(event_);
    release(device_), release(factory_);
}

bool D3D12Device::start(GLFWwindow* window, std::string& why) {
    window_ = window;
    hwnd_ = glfwGetWin32Window(window);
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll"), dxgi = LoadLibraryW(L"dxgi.dll");
    if (!d3d12 || !dxgi) {
        why = "no d3d12.dll or dxgi.dll";
        return false;
    }
    auto create_device = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice")));
    auto debug_interface = reinterpret_cast<PFN_D3D12_GET_DEBUG_INTERFACE>(reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12GetDebugInterface")));
    serialize_ = reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")));
    using CreateFactory = HRESULT(WINAPI*)(UINT, REFIID, void**);
    auto create_factory = reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")));
    if (!create_device || !create_factory || !serialize_) {
        why = "D3D12 entry points missing";
        return false;
    }
    if (const char* v = std::getenv("SG_D3D12_DEBUG"); v && *v == '1' && debug_interface) {
        ID3D12Debug* dbg = nullptr;
        if (SUCCEEDED(debug_interface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer(), dbg->Release();
    }
    if (FAILED(create_factory(0, IID_PPV_ARGS(&factory_)))) {
        why = "CreateDXGIFactory2 failed";
        return false;
    }
    IDXGIAdapter1* adapter = nullptr;
    IDXGIFactory6* f6 = nullptr;
    if (SUCCEEDED(factory_->QueryInterface(IID_PPV_ARGS(&f6)))) {
        f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
        BOOL allow = FALSE;
        if (SUCCEEDED(f6->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow))) tearing_ = allow == TRUE;
        f6->Release();
    }
    if (FAILED(create_device(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)))) {
        release(adapter);
        why = "no card with D3D12 at feature level 12_0";
        return false;
    }
    if (std::getenv("SG_D3D12_DEBUG")) device_->QueryInterface(IID_PPV_ARGS(&info_queue));
    if (adapter) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        char n[128] = {};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, n, sizeof n - 1, nullptr, nullptr);
        name_ = n;
        adapter->Release();
    }
    if (!compiler_.start()) {
        why = "no DXC (dxcompiler.dll and dxil.dll) beside the program, where SG_DXC says, or in the Windows SDK";
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)));
    std::uint64_t freq = 1;
    if (SUCCEEDED(queue_->GetTimestampFrequency(&freq)) && freq) tick_ns_ = 1e9 / static_cast<double>(freq);
    HR(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    for (Frame& f : frames_) HR(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator)));
    HR(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator, nullptr, IID_PPV_ARGS(&list_)));
    HR(list_->Close());

    rtv_heap_.make(device_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1u << 16);
    dsv_heap_.make(device_, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1u << 14);
    srv_heap_.make(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1u << 18);
    D3D12_DESCRIPTOR_HEAP_DESC vd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kVisible, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    HR(device_->CreateDescriptorHeap(&vd, IID_PPV_ARGS(&visible_)));
    D3D12_DESCRIPTOR_HEAP_DESC sd{D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerTables, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    HR(device_->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&samplers_visible_)));
    visible_step_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    sampler_step_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    visible_cpu_ = visible_->GetCPUDescriptorHandleForHeapStart();
    visible_gpu_ = visible_->GetGPUDescriptorHandleForHeapStart();
    sampler_cpu_ = samplers_visible_->GetCPUDescriptorHandleForHeapStart();
    sampler_gpu_ = samplers_visible_->GetGPUDescriptorHandleForHeapStart();
    {
        // (What an unbound samplerBuffer reads, and a gap among what is drawn into.)
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        null_texel_ = srv_heap_.take();
        device_->CreateShaderResourceView(nullptr, &d, null_texel_);
        D3D12_RENDER_TARGET_VIEW_DESC r{};
        r.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        null_rtv_ = rtv_heap_.take();
        device_->CreateRenderTargetView(nullptr, &r, null_rtv_);
    }

    // The window's picture shown: its own root signature and pipeline.
    {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable = {1, &range};
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rd{1, &param, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob *blob = nullptr, *err = nullptr;
        HR(serialize_(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err));
        HR(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&show_root_)));
        release(blob), release(err);
        std::vector<char> vs, ps;
        std::string log;
        if (!compiler_.compile(kShowVs, "vs", vs, log) || !compiler_.compile(kShowPs, "ps", ps, log)) {
            why = "the window's own shaders do not build: " + log;
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = show_root_;
        pd.VS = {vs.data(), vs.size()};
        pd.PS = {ps.data(), ps.size()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pd.SampleDesc.Count = 1;
        HR(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&show_pso_)));
    }

    {
        D3D12_INDIRECT_ARGUMENT_DESC arg{};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC cs{16, 1, &arg, 0};
        HR(device_->CreateCommandSignature(&cs, nullptr, IID_PPV_ARGS(&draws_)));
    }

    for (int k = 0; k < 2; ++k) {
        D3D12_QUERY_HEAP_DESC qh{k ? D3D12_QUERY_HEAP_TYPE_TIMESTAMP : D3D12_QUERY_HEAP_TYPE_OCCLUSION, kQueries, 0};
        HR(device_->CreateQueryHeap(&qh, IID_PPV_ARGS(&query_heaps_[k])));
    }
    answers_ = create_buffer(std::size_t{2} * kQueries * 8, Memory::readback);
    predicates_ = create_buffer(std::size_t{kQueries} * 8, Memory::card);
    slots_.assign(std::size_t{2} * kQueries, Slot{});
    kind_of_.assign(kQueries, 0);

    if (!make_swapchain()) {
        why = "no swapchain for the window";
        return false;
    }
    make_window_targets();
    for (Sampled k : {Sampled::tex2d, Sampled::tex2d_array, Sampled::tex3d, Sampled::cube, Sampled::tex2d_ms, Sampled::utex2d}) empty(k);
    return true;
}

// --- commands -------------------------------------------------------------------

ID3D12GraphicsCommandList* D3D12Device::list() {
    if (open_) return list_;
    HR(list_->Reset(frame().allocator, nullptr));
    ID3D12DescriptorHeap* heaps[2] = {visible_, samplers_visible_};
    list_->SetDescriptorHeaps(2, heaps);
    open_ = true;
    target_bound_ = false;
    root_now_ = nullptr, pso_now_ = nullptr;
    bound_ = Bound_{};
    // (A list keeps no predicate of the last one's: what is skipped still is.)
    if (only_ >= 0) list_->SetPredication(predicates_->res, static_cast<std::uint64_t>(only_) * 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
    return list_;
}

void D3D12Device::flush_barriers() {
    if (barriers_.empty()) return;
    list()->ResourceBarrier(static_cast<UINT>(barriers_.size()), barriers_.data());
    barriers_.clear();
}

void D3D12Device::to_state(Texture* t, D3D12_RESOURCE_STATES s) {
    if (t->state == s) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {t->res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, t->state, s};
    barriers_.push_back(b);
    t->state = s;
}

void D3D12Device::to_state(Buffer* buf, D3D12_RESOURCE_STATES s) {
    if (!buf->tracked || buf->state == s) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {buf->res, 0, buf->state, s};
    barriers_.push_back(b);
    buf->state = s;
}

void D3D12Device::submit(bool wait) {
    ID3D12GraphicsCommandList* c = list();
    // The questions asked in it answered into the buffer they are read from.
    for (int slot : recorded_) {
        const int kind = kind_of_[static_cast<std::size_t>(slot)];
        c->ResolveQueryData(query_heaps_[kind], kind ? D3D12_QUERY_TYPE_TIMESTAMP : D3D12_QUERY_TYPE_BINARY_OCCLUSION, static_cast<UINT>(slot), 1,
                            answers_->res, (static_cast<std::uint64_t>(kind) * kQueries + static_cast<std::uint64_t>(slot)) * 8);
    }
    flush_barriers();
    HR(c->Close());
    open_ = false;
    // (What the list had set is gone with it.)
    target_bound_ = false;
    root_now_ = nullptr, pso_now_ = nullptr;
    bound_ = Bound_{};
    ID3D12CommandList* lists[] = {c};
    queue_->ExecuteCommandLists(1, lists);
    const std::uint64_t value = ++fence_value_;
    HR(queue_->Signal(fence_, value));
    frame().done_at = value;
    for (int slot : recorded_) {
        Slot& q = slots_[static_cast<std::size_t>(kind_of_[static_cast<std::size_t>(slot)]) * kQueries + static_cast<std::size_t>(slot)];
        q.recording = false, q.at = value;
    }
    recorded_.clear();
    if (wait) {
        wait_value(value);
        // (The card idle: what every frame let go of goes now.)
        for (Frame& f : frames_) {
            for (Buffer* b : f.dead_buffers) free_buffer(b);
            for (Texture* t : f.dead_textures) free_texture(t);
            for (ID3D12PipelineState* p : f.dead_psos) p->Release();
            f.dead_buffers.clear(), f.dead_textures.clear(), f.dead_psos.clear();
        }
    }
}

void D3D12Device::wait_value(std::uint64_t v) {
    if (fence_->GetCompletedValue() >= v) return;
    HR(fence_->SetEventOnCompletion(v, event_));
    WaitForSingleObject(event_, INFINITE);
}

D3D12Device::Ring D3D12Device::ring(std::size_t bytes, std::size_t align) {
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
    Ring r{c.buffer, c.used, static_cast<std::uint8_t*>(c.buffer->host) + c.used, c.buffer->res->GetGPUVirtualAddress() + c.used};
    c.used += bytes;
    return r;
}

std::uint32_t D3D12Device::visible(std::uint32_t n) {
    Frame& f = frame();
    constexpr std::uint32_t half = kVisible / kFrames;
    // (A frame long in coming - many drawn and none shown - starts its
    // descriptors over once the card has used them.)
    if (f.visible_used + n > half) {
        if (drawing_) {
            std::fprintf(stderr, "stategine d3d12: out of visible descriptors\n");
            std::abort();
        }
        submit(true), f.visible_used = 0;
    }
    const std::uint32_t at = static_cast<std::uint32_t>(frame_index_ % kFrames) * half + f.visible_used;
    f.visible_used += n;
    return at;
}

// A frame long in coming - many drawn and none shown - starts its uploads
// over once the card has used them.
void D3D12Device::recycle() {
    Frame& f = frame();
    submit(true);
    ++epoch_;
    for (Chunk& c : f.chunks) c.used = 0;
    f.chunk_now = 0;
}

void D3D12Device::retire(Frame& f) {
    wait_value(f.done_at);
    ++epoch_;
    HR(f.allocator->Reset());
    for (Chunk& c : f.chunks) c.used = 0;
    f.chunk_now = 0;
    f.visible_used = 0;
    for (Buffer* b : f.dead_buffers) free_buffer(b);
    for (Texture* t : f.dead_textures) free_texture(t);
    for (ID3D12PipelineState* p : f.dead_psos) p->Release();
    f.dead_buffers.clear(), f.dead_textures.clear(), f.dead_psos.clear();
}

// --- buffers --------------------------------------------------------------------

Buffer* D3D12Device::create_buffer(std::size_t bytes, Memory memory) {
    auto* b = new Buffer;
    b->size = bytes;
    const bool host = memory != Memory::card;
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    if (memory == Memory::upload) {
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        state = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (memory == Memory::readback) {
        // (System memory, cached, in any state: the card writes it - copies,
        // answers to questions - and the processor reads it.)
        hp.Type = D3D12_HEAP_TYPE_CUSTOM;
        hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
        hp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    } else {
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    }
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = std::max<std::size_t>(bytes, 256);
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // (One on the card may be written by a compute program.)
    if (!host) rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HR(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&b->res)));
    b->tracked = !host;
    b->state = state;
    if (host) {
        D3D12_RANGE none{0, 0};
        HR(b->res->Map(0, &none, &b->host));
    }
    return b;
}

void D3D12Device::free_buffer(Buffer* b) {
    release(b->res);
    delete b;
}

void D3D12Device::destroy(Buffer* b) {
    if (b) frame().dead_buffers.push_back(b);
}

void D3D12Device::upload(Buffer* b, std::size_t at, const void* data, std::size_t bytes) {
    if (!bytes) return;
    const Ring r = ring(bytes, 16);
    std::memcpy(r.host, data, bytes);
    to_state(b, D3D12_RESOURCE_STATE_COPY_DEST);
    flush_barriers();
    list()->CopyBufferRegion(b->res, at, r.buffer->res, r.at, bytes);
}

void D3D12Device::apply_repacks(Buffer* b) {
    for (auto it = repacks_.begin(); it != repacks_.end();) {
        if (it->into != b) {
            ++it;
            continue;
        }
        const auto* from = static_cast<const std::uint8_t*>(it->staging->host);
        auto* to = static_cast<std::uint8_t*>(b->host) + it->at;
        for (std::size_t y = 0; y < it->rows; ++y) std::memcpy(to + y * it->row, from + y * it->pitch, it->row);
        free_buffer(it->staging);
        it = repacks_.erase(it);
    }
}

void D3D12Device::download(Buffer* b, std::size_t at, void* out, std::size_t bytes) {
    if (b->host) {
        submit(true);
        apply_repacks(b);
        std::memcpy(out, static_cast<std::uint8_t*>(b->host) + at, bytes);
        return;
    }
    Buffer* staging = create_buffer(bytes, Memory::readback);
    to_state(b, D3D12_RESOURCE_STATE_COPY_SOURCE);
    flush_barriers();
    list()->CopyBufferRegion(staging->res, 0, b->res, at, bytes);
    submit(true);
    std::memcpy(out, staging->host, bytes);
    free_buffer(staging);
}

// --- textures -------------------------------------------------------------------

Texture* D3D12Device::create_texture(const TextureInfo& info) {
    auto* t = new Texture;
    t->info = info;
    t->format = resource_format(info.format);
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = info.kind == TexKind::tex3d ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = static_cast<UINT64>(info.w);
    rd.Height = static_cast<UINT>(info.h);
    rd.DepthOrArraySize = static_cast<UINT16>(info.depth);
    rd.MipLevels = static_cast<UINT16>(info.levels);
    rd.Format = t->format;
    rd.SampleDesc.Count = static_cast<UINT>(info.samples);
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (is_depth(info.format)) rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    else if (!is_packed(info.format)) rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE cv{};
    const D3D12_CLEAR_VALUE* clear = nullptr;
    if (is_depth(info.format)) {
        cv.Format = info.format == Format::d24 ? DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 1.0f;
        clear = &cv;
    }
    HR(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, clear, IID_PPV_ARGS(&t->res)));
    return t;
}

void D3D12Device::free_texture(Texture* t) {
    for (auto& [k, h] : t->srvs) srv_heap_.give(h);
    for (auto& [k, h] : t->rtvs) rtv_heap_.give(h);
    for (auto& [k, h] : t->dsvs) dsv_heap_.give(h);
    release(t->res);
    delete t;
}

void D3D12Device::destroy(Texture* t) {
    if (!t) return;
    for (int i = 0; i < Target::kColors; ++i)
        if (target_.color[i].tex == t) target_.color[i] = {}, target_bound_ = false;
    if (target_.depth.tex == t) target_.depth = {}, target_bound_ = false;
    // (Its descriptors were read as they were recorded: theirs to give back
    // now. The texture itself waits for the card.)
    for (auto& [k, h] : t->srvs) srv_heap_.give(h);
    for (auto& [k, h] : t->rtvs) rtv_heap_.give(h);
    for (auto& [k, h] : t->dsvs) dsv_heap_.give(h);
    t->srvs.clear(), t->rtvs.clear(), t->dsvs.clear();
    frame().dead_textures.push_back(t);
}

UINT subresource(const Texture* t, int level, int layer) {
    return static_cast<UINT>(level) + static_cast<UINT>(t->info.kind == TexKind::tex3d ? 0 : layer) * static_cast<UINT>(t->info.levels);
}

void D3D12Device::write(Texture* t, int level, int x, int y, int z, int w, int h, int d, const void* data) {
    const bool packed = is_packed(t->info.format);
    const std::size_t row = packed ? round4(w) / 4 * 16 : static_cast<std::size_t>(w) * static_cast<std::size_t>(texel_bytes(t->info.format));
    const std::size_t rows = packed ? round4(h) / 4 : static_cast<std::size_t>(h);
    const std::size_t pitch = (row + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~std::size_t{D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1};
    const bool volume = t->info.kind == TexKind::tex3d;
    const Ring r = ring(pitch * rows * static_cast<std::size_t>(d), D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    for (int s = 0; s < d; ++s)
        for (std::size_t j = 0; j < rows; ++j)
            std::memcpy(r.host + (static_cast<std::size_t>(s) * rows + j) * pitch,
                        static_cast<const std::uint8_t*>(data) + (static_cast<std::size_t>(s) * rows + j) * row, row);
    to_state(t, D3D12_RESOURCE_STATE_COPY_DEST);
    flush_barriers();
    const UINT fw = static_cast<UINT>(packed ? round4(w) : static_cast<std::size_t>(w)), fh = static_cast<UINT>(packed ? round4(h) : static_cast<std::size_t>(h));
    for (int s = 0; s < (volume ? 1 : d); ++s) {
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = r.buffer->res;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = r.at + static_cast<std::size_t>(s) * rows * pitch;
        src.PlacedFootprint.Footprint = {copy_format(t->info.format), fw, fh, static_cast<UINT>(volume ? d : 1), static_cast<UINT>(pitch)};
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = t->res;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource(t, level, volume ? 0 : z + s);
        const D3D12_BOX box{0, 0, 0, fw, fh, static_cast<UINT>(volume ? d : 1)};
        list()->CopyTextureRegion(&dst, static_cast<UINT>(x), static_cast<UINT>(y), static_cast<UINT>(volume ? z : 0), &src, &box);
    }
}

void D3D12Device::read_into(Texture* t, int level, int layer, int x, int y, int w, int h, Buffer* b, std::size_t at) {
    const std::size_t row = static_cast<std::size_t>(w) * static_cast<std::size_t>(texel_bytes(t->info.format));
    const std::size_t pitch = (row + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~std::size_t{D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1};
    Buffer* staging = create_buffer(pitch * static_cast<std::size_t>(h), Memory::readback);
    to_state(t, D3D12_RESOURCE_STATE_COPY_SOURCE);
    flush_barriers();
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = staging->res;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint = {copy_format(t->info.format), static_cast<UINT>(w), static_cast<UINT>(h), 1, static_cast<UINT>(pitch)};
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = t->res;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const bool volume = t->info.kind == TexKind::tex3d;
    src.SubresourceIndex = subresource(t, level, layer);
    const D3D12_BOX box{static_cast<UINT>(x), static_cast<UINT>(y), static_cast<UINT>(volume ? layer : 0), static_cast<UINT>(x + w),
                        static_cast<UINT>(y + h), static_cast<UINT>(volume ? layer + 1 : 1)};
    list()->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    repacks_.push_back({staging, pitch, row, static_cast<std::size_t>(h), b, at});
}

void D3D12Device::read(Texture* t, int level, int layer, int x, int y, int w, int h, void* out) {
    const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(texel_bytes(t->info.format));
    Buffer* into = create_buffer(bytes, Memory::readback);
    read_into(t, level, layer, x, y, w, h, into, 0);
    download(into, 0, out, bytes);
    free_buffer(into);
}

void D3D12Device::copy(Texture* from, int from_level, int from_layer, int fx, int fy, Texture* to, int to_level, int to_layer, int tx, int ty,
                       int w, int h) {
    to_state(from, D3D12_RESOURCE_STATE_COPY_SOURCE);
    to_state(to, D3D12_RESOURCE_STATE_COPY_DEST);
    flush_barriers();
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = from->res;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = subresource(from, from_level, from_layer);
    dst.pResource = to->res;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = subresource(to, to_level, to_layer);
    const bool fv = from->info.kind == TexKind::tex3d, tv = to->info.kind == TexKind::tex3d;
    const bool packed = is_packed(from->info.format);
    const UINT bw = static_cast<UINT>(packed ? round4(w) : static_cast<std::size_t>(w)), bh = static_cast<UINT>(packed ? round4(h) : static_cast<std::size_t>(h));
    const D3D12_BOX box{static_cast<UINT>(fx), static_cast<UINT>(fy), static_cast<UINT>(fv ? from_layer : 0), static_cast<UINT>(fx) + bw,
                        static_cast<UINT>(fy) + bh, static_cast<UINT>(fv ? from_layer + 1 : 1)};
    list()->CopyTextureRegion(&dst, static_cast<UINT>(tx), static_cast<UINT>(ty), static_cast<UINT>(tv ? to_layer : 0), &src, &box);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::srv(Texture* t, Sampled kind, int base, int levels) {
    const std::uint64_t key = std::uint64_t(kind) | std::uint64_t(base) << 8 | std::uint64_t(levels) << 16;
    auto it = t->srvs.find(key);
    if (it != t->srvs.end()) return it->second;
    // (Views are copied where the card reads them as they are drawn with: a
    // texture that has had many - its levels streamed in - forgets them.)
    if (t->srvs.size() >= 16) {
        for (auto& [k, h] : t->srvs) srv_heap_.give(h);
        t->srvs.clear();
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = read_format(t->info.format);
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    switch (kind) {
        case Sampled::tex2d_array:
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            d.Texture2DArray = {static_cast<UINT>(base), static_cast<UINT>(levels), 0, static_cast<UINT>(t->info.depth), 0, 0.0f};
            break;
        case Sampled::tex3d:
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            d.Texture3D = {static_cast<UINT>(base), static_cast<UINT>(levels), 0.0f};
            break;
        case Sampled::cube:
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            d.TextureCube = {static_cast<UINT>(base), static_cast<UINT>(levels), 0.0f};
            break;
        case Sampled::tex2d_ms: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS; break;
        default:
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Texture2D = {static_cast<UINT>(base), static_cast<UINT>(levels), 0, 0.0f};
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE h = srv_heap_.take();
    device_->CreateShaderResourceView(t->res, &d, h);
    t->srvs.emplace(key, h);
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::rtv(const Attachment& a) {
    const std::uint64_t key = std::uint64_t(a.level) | std::uint64_t(a.layer) << 16 | std::uint64_t(a.encode) << 40;
    auto it = a.tex->rtvs.find(key);
    if (it != a.tex->rtvs.end()) return it->second;
    D3D12_RENDER_TARGET_VIEW_DESC d{};
    d.Format = draw_format(a);
    const TextureInfo& i = a.tex->info;
    if (i.samples > 1) {
        d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    } else if (i.kind == TexKind::tex3d) {
        d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        d.Texture3D = {static_cast<UINT>(a.level), static_cast<UINT>(a.layer), 1};
    } else if (i.kind != TexKind::tex2d) {
        d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        d.Texture2DArray = {static_cast<UINT>(a.level), static_cast<UINT>(a.layer), 1, 0};
    } else {
        d.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        d.Texture2D = {static_cast<UINT>(a.level), 0};
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_.take();
    device_->CreateRenderTargetView(a.tex->res, &d, h);
    a.tex->rtvs.emplace(key, h);
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::dsv(const Attachment& a) {
    const std::uint64_t key = std::uint64_t(a.level) | std::uint64_t(a.layer) << 16;
    auto it = a.tex->dsvs.find(key);
    if (it != a.tex->dsvs.end()) return it->second;
    D3D12_DEPTH_STENCIL_VIEW_DESC d{};
    d.Format = draw_format(a);
    const TextureInfo& i = a.tex->info;
    if (i.samples > 1) {
        d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    } else if (i.kind != TexKind::tex2d) {
        d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        d.Texture2DArray = {static_cast<UINT>(a.level), static_cast<UINT>(a.layer), 1};
    } else {
        d.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        d.Texture2D = {static_cast<UINT>(a.level)};
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE h = dsv_heap_.take();
    device_->CreateDepthStencilView(a.tex->res, &d, h);
    a.tex->dsvs.emplace(key, h);
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Device::sampler_table(const std::uint64_t* keys, std::size_t count) {
    std::vector<std::uint64_t> key(keys, keys + count);
    auto it = sampler_tables_.find(key);
    if (it == sampler_tables_.end()) {
        if (sampler_next_ + count > kSamplerTables) {
            // (Full: every table forgotten, once the card is done with them.)
            submit(true);
            for (Frame& f : frames_) wait_value(f.done_at);
            sampler_tables_.clear();
            sampler_next_ = 0;
        }
        const std::uint32_t at = sampler_next_;
        sampler_next_ += static_cast<std::uint32_t>(count);
        for (std::size_t i = 0; i < count; ++i) {
            const SamplerInfo s = SamplerInfo::from_key(keys[i]);
            D3D12_SAMPLER_DESC d{};
            const bool aniso = s.anisotropy > 1 && s.mip != SamplerInfo::none;
            int f = (s.min == SamplerInfo::linear ? 0x10 : 0) | (s.mag == SamplerInfo::linear ? 0x4 : 0) | (s.mip == SamplerInfo::mip_linear ? 0x1 : 0);
            if (aniso) f = D3D12_FILTER_ANISOTROPIC;
            if (s.compare) f |= 0x80;
            d.Filter = static_cast<D3D12_FILTER>(f);
            d.AddressU = d3d_wrap(s.wrap[0]);
            d.AddressV = d3d_wrap(s.wrap[1]);
            d.AddressW = d3d_wrap(s.wrap[2]);
            d.MaxAnisotropy = aniso ? s.anisotropy : 1;
            d.ComparisonFunc = static_cast<D3D12_COMPARISON_FUNC>(s.compare_op + 1);
            const float border = s.border_white ? 1.0f : 0.0f;
            d.BorderColor[0] = d.BorderColor[1] = d.BorderColor[2] = d.BorderColor[3] = border;
            d.MinLOD = 0;
            d.MaxLOD = s.mip == SamplerInfo::none ? 0.0f : D3D12_FLOAT32_MAX;
            device_->CreateSampler(&d, {sampler_cpu_.ptr + (at + i) * sampler_step_});
        }
        it = sampler_tables_.emplace(std::move(key), at).first;
    }
    return {sampler_gpu_.ptr + static_cast<std::uint64_t>(it->second) * sampler_step_};
}

Texture* D3D12Device::empty(Sampled kind) {
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

Program* D3D12Device::create_program(const Translated& t, std::string& log) {
    auto* p = new Program;
    p->t = t;
    p->tables = static_cast<int>(t.samplers.size());
    for (int s = 0; s < (t.compute ? 1 : 2); ++s)
        if (!compiler_.build(t, s, p->code[s], log)) {
            delete p;
            if (log.empty()) log = "its HLSL does not build";
            return nullptr;
        }
    std::vector<D3D12_ROOT_PARAMETER> params;
    if (t.compute) {
        // Its uniforms, then each storage buffer a UAV of its own, by where
        // it declares it.
        D3D12_ROOT_PARAMETER cb{};
        cb.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        cb.Descriptor = {static_cast<UINT>(kBlockBinding[0]), 0};
        cb.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(cb);
        for (const StorageSlot& s : t.storage) {
            D3D12_ROOT_PARAMETER uav{};
            uav.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            uav.Descriptor = {static_cast<UINT>(kFirstStorage + s.binding), 0};
            uav.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params.push_back(uav);
        }
        // (And what its samplers read, a table of views and one of samplers, as a draw's.)
        D3D12_DESCRIPTOR_RANGE ranges[2] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, static_cast<UINT>(p->tables), static_cast<UINT>(kFirstSampler), 0, 0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, static_cast<UINT>(p->tables), static_cast<UINT>(kFirstSampler), 0, 0},
        };
        if (p->tables)
            for (D3D12_DESCRIPTOR_RANGE& r : ranges) {
                D3D12_ROOT_PARAMETER tp{};
                tp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                tp.DescriptorTable = {1, &r};
                tp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                params.push_back(tp);
            }
        D3D12_ROOT_SIGNATURE_DESC rd{static_cast<UINT>(params.size()), params.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob *blob = nullptr, *err = nullptr;
        if (FAILED(serialize_(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
            log = err ? static_cast<const char*>(err->GetBufferPointer()) : "root signature";
            release(err);
            delete p;
            return nullptr;
        }
        HR(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&p->root)));
        release(blob), release(err);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = p->root;
        pd.CS = {p->code[0].data(), p->code[0].size()};
        if (FAILED(device_->CreateComputePipelineState(&pd, IID_PPV_ARGS(&p->compute)))) {
            say_messages();
            log = "compute pipeline";
            release(p->root);
            delete p;
            return nullptr;
        }
        return p;
    }
    for (int s = 0; s < 2; ++s) {
        D3D12_ROOT_PARAMETER cb{};
        cb.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        cb.Descriptor = {static_cast<UINT>(kBlockBinding[s]), 0};
        cb.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params.push_back(cb);
    }
    D3D12_DESCRIPTOR_RANGE ranges[2] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, static_cast<UINT>(p->tables), static_cast<UINT>(kFirstSampler), 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, static_cast<UINT>(p->tables), static_cast<UINT>(kFirstSampler), 0, 0},
    };
    if (p->tables)
        for (D3D12_DESCRIPTOR_RANGE& r : ranges) {
            D3D12_ROOT_PARAMETER tp{};
            tp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            tp.DescriptorTable = {1, &r};
            tp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params.push_back(tp);
        }
    D3D12_ROOT_SIGNATURE_DESC rd{static_cast<UINT>(params.size()), params.data(), 0, nullptr,
                                 D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ID3DBlob *blob = nullptr, *err = nullptr;
    if (FAILED(serialize_(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
        log = err ? static_cast<const char*>(err->GetBufferPointer()) : "root signature";
        release(err);
        delete p;
        return nullptr;
    }
    HR(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&p->root)));
    release(blob), release(err);
    return p;
}

void D3D12Device::destroy(Program* p) {
    if (!p) return;
    for (auto& [k, pso] : p->psos) frame().dead_psos.push_back(pso);
    if (p->compute) frame().dead_psos.push_back(p->compute);
    // (Its root signature outlives whatever the card still draws with it.)
    submit(true);
    if (root_now_ == p->root) root_now_ = nullptr;
    pso_now_ = nullptr;
    release(p->root);
    delete p;
}

ID3D12PipelineState* D3D12Device::pso(Program* p, const Draw& d) {
    const Raster& r = d.raster;
    std::uint64_t key = 1469598103934665603ull;
    key = mix(key, std::uint64_t(r.blend) | std::uint64_t(r.src_rgb) << 1 | std::uint64_t(r.dst_rgb) << 4 | std::uint64_t(r.src_a) << 7 |
                       std::uint64_t(r.dst_a) << 10 | std::uint64_t(r.color_mask) << 13 | std::uint64_t(r.depth_clamp) << 17 |
                       std::uint64_t(r.depth_test) << 18 | std::uint64_t(r.depth_write) << 19 | std::uint64_t(r.depth_op) << 20 |
                       std::uint64_t(r.cull) << 24 | std::uint64_t(r.cull_front) << 25 | std::uint64_t(r.bias) << 26 |
                       std::uint64_t(r.front_cw) << 27);
    if (r.bias) {
        std::uint32_t a, b;
        std::memcpy(&a, &r.bias_factor, 4), std::memcpy(&b, &r.bias_units, 4);
        key = mix(mix(key, a), b);
    }
    for (int i = 0; i < Target::kColors; ++i) key = mix(key, target_.color[i].tex ? std::uint64_t(draw_format(target_.color[i])) + 1 : 0);
    key = mix(key, target_.depth.tex ? std::uint64_t(draw_format(target_.depth)) + 1 : 0);
    const Texture* any = target_.depth.tex;
    for (int i = 0; i < Target::kColors && !any; ++i) any = target_.color[i].tex;
    const int samples = any ? any->info.samples : 1;
    key = mix(key, std::uint64_t(samples));
    for (int loc = 0; loc < 16; ++loc) {
        if (!(p->t.inputs & (1u << loc))) continue;
        const VertexInput& in = d.inputs[loc];
        key = mix(key, in.buffer ? (std::uint64_t(in.components) | std::uint64_t(in.per_instance) << 8) : 0xFFFF);
    }
    auto it = p->psos.find(key);
    if (it != p->psos.end()) return it->second;

    std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
    for (UINT loc = 0; loc < 16; ++loc) {
        if (!(p->t.inputs & (1u << loc))) continue;
        const VertexInput& in = d.inputs[loc];
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT};
        const bool instance = in.buffer && in.per_instance;
        layout.push_back({"TEXCOORD", loc, in.buffer ? formats[std::clamp<int>(in.components, 1, 4) - 1] : DXGI_FORMAT_R32G32B32A32_FLOAT, loc, 0,
                          instance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, instance ? 1u : 0u});
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = p->root;
    pd.VS = {p->code[0].data(), p->code[0].size()};
    pd.PS = {p->code[1].data(), p->code[1].size()};
    for (int i = 0; i < Target::kColors; ++i) {
        D3D12_RENDER_TARGET_BLEND_DESC& b = pd.BlendState.RenderTarget[i];
        const bool whole = target_.color[i].tex && (target_.color[i].tex->info.format == Format::r32ui || target_.color[i].tex->info.format == Format::rg32ui || target_.color[i].tex->info.format == Format::rgba8ui);
        b.BlendEnable = r.blend && !whole;
        b.SrcBlend = d3d_factor(r.src_rgb);
        b.DestBlend = d3d_factor(r.dst_rgb);
        b.BlendOp = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha = d3d_factor(r.src_a);
        b.DestBlendAlpha = d3d_factor(r.dst_a);
        b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        b.LogicOp = D3D12_LOGIC_OP_NOOP;
        b.RenderTargetWriteMask = r.color_mask;
    }
    pd.BlendState.IndependentBlendEnable = TRUE;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = r.cull ? (r.cull_front ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK) : D3D12_CULL_MODE_NONE;
    // (GL's rows and these lie alike; the two measure a turn opposite ways.)
    pd.RasterizerState.FrontCounterClockwise = r.front_cw ? TRUE : FALSE;
    pd.RasterizerState.DepthBias = r.bias ? static_cast<INT>(std::lround(r.bias_units)) : 0;
    pd.RasterizerState.SlopeScaledDepthBias = r.bias ? r.bias_factor : 0.0f;
    pd.RasterizerState.DepthClipEnable = r.depth_clamp ? FALSE : TRUE;
    pd.RasterizerState.MultisampleEnable = samples > 1;
    pd.DepthStencilState.DepthEnable = r.depth_test && target_.depth.tex;
    pd.DepthStencilState.DepthWriteMask = r.depth_test && r.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    pd.DepthStencilState.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(static_cast<int>(r.depth_op) + 1);
    pd.InputLayout = {layout.data(), static_cast<UINT>(layout.size())};
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    UINT n = 0;
    for (int i = 0; i < Target::kColors; ++i)
        if (target_.color[i].tex) n = static_cast<UINT>(i + 1);
    // (A gap among what is drawn into is the null view's: its format, nothing written.)
    for (UINT i = 0; i < n; ++i) {
        pd.RTVFormats[i] = target_.color[i].tex ? draw_format(target_.color[i]) : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (!target_.color[i].tex) pd.BlendState.RenderTarget[i].RenderTargetWriteMask = 0;
    }
    pd.NumRenderTargets = n;
    pd.DSVFormat = target_.depth.tex ? draw_format(target_.depth) : DXGI_FORMAT_UNKNOWN;
    pd.SampleDesc.Count = static_cast<UINT>(samples);
    ID3D12PipelineState* out = nullptr;
    HR(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&out)));
    p->psos.emplace(key, out);
    return out;
}

// --- drawing --------------------------------------------------------------------

void D3D12Device::target(const Target& t) {
    if (t == target_) return;
    target_ = t;
    target_bound_ = false;
}

void D3D12Device::bind_target() {
    for (int i = 0; i < Target::kColors; ++i)
        if (target_.color[i].tex) to_state(target_.color[i].tex, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (target_.depth.tex) to_state(target_.depth.tex, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    if (target_bound_) return;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, Target::kColors> rtvs{};
    UINT n = 0;
    for (int i = 0; i < Target::kColors; ++i)
        if (target_.color[i].tex) n = static_cast<UINT>(i + 1);
    for (UINT i = 0; i < n; ++i) rtvs[i] = target_.color[i].tex ? rtv(target_.color[i]) : null_rtv_;
    D3D12_CPU_DESCRIPTOR_HANDLE ds{};
    if (target_.depth.tex) ds = dsv(target_.depth);
    list()->OMSetRenderTargets(n, rtvs.data(), FALSE, target_.depth.tex ? &ds : nullptr);
    target_bound_ = true;
}

void D3D12Device::clear(const int rect[4], std::uint8_t colors, const float color[4], bool depth, float depth_value) {
    const D3D12_RECT r{std::clamp(rect[0], 0, target_.w), std::clamp(rect[1], 0, target_.h), std::clamp(rect[0] + rect[2], 0, target_.w),
                       std::clamp(rect[1] + rect[3], 0, target_.h)};
    if (r.right <= r.left || r.bottom <= r.top) return;
    bind_target();
    flush_barriers();
    for (int i = 0; i < Target::kColors; ++i)
        if (((colors >> i) & 1u) && target_.color[i].tex) list()->ClearRenderTargetView(rtv(target_.color[i]), color, 1, &r);
    if (depth && target_.depth.tex) list()->ClearDepthStencilView(dsv(target_.depth), D3D12_CLEAR_FLAG_DEPTH, depth_value, 0, 1, &r);
}

void D3D12Device::draw(const Draw& d) {
    Program* p = d.program;
    if (!p) return;
    // A volume's slices drawn into at once are one subresource here, which
    // no two views of may be written through together: drawn slice by
    // slice, each with what goes to it alone.
    if (!splitting_) {
        bool shared = false;
        for (int i = 0; i < Target::kColors && !shared; ++i)
            for (int j = i + 1; j < Target::kColors && !shared; ++j)
                shared = target_.color[i].tex && target_.color[i].tex == target_.color[j].tex && target_.color[i].tex->info.kind == TexKind::tex3d;
        if (shared) {
            const Target whole = target_;
            splitting_ = true;
            for (int i = 0; i < Target::kColors; ++i) {
                if (!whole.color[i].tex) continue;
                Target one = whole;
                for (int j = 0; j < Target::kColors; ++j)
                    if (j != i) one.color[j] = {};
                target(one);
                draw(d);
            }
            target(whole);
            splitting_ = false;
            return;
        }
    }
    bool any = target_.depth.tex != nullptr;
    for (int i = 0; i < Target::kColors; ++i) any = any || target_.color[i].tex;
    if (!any || d.viewport[2] <= 0 || d.viewport[3] <= 0) return;
    const D3D12_RECT sc{std::clamp(d.scissor[0], 0, target_.w), std::clamp(d.scissor[1], 0, target_.h),
                        std::clamp(d.scissor[0] + d.scissor[2], 0, target_.w), std::clamp(d.scissor[1] + d.scissor[3], 0, target_.h)};
    if (sc.right <= sc.left || sc.bottom <= sc.top) return;
    {
        // (Room for what it uploads and the descriptors it takes, made
        // before it is recorded.)
        Frame& f = frame();
        // (Never while a question is open: it would end before what it asks of.)
        if (f.visible_used + static_cast<std::uint32_t>(p->tables) > kVisible / kFrames) submit(true), f.visible_used = 0, ++epoch_;
        // (Room in this frame's uploads: the first chunk on from the one in use
        // with its 96 KB to spare - not only the one in use, or a frame with a
        // near-full chunk and a dozen empty ones after it would wait on the card.)
        while (f.chunk_now < f.chunks.size() && f.chunks[f.chunk_now].used + (std::size_t{96} << 10) > f.chunks[f.chunk_now].buffer->size) ++f.chunk_now;
        const bool room = f.chunk_now < f.chunks.size();
        if (!room && f.chunks.size() >= kChunks && asking_ < 0) recycle();
    }
    struct Drawing {
        bool& on;
        explicit Drawing(bool& o) : on(o) { on = true; }
        ~Drawing() { on = false; }
    } drawing(drawing_);

    // What it reads, in the state it is read in; what it draws into, in theirs.
    const std::size_t n = std::min<std::size_t>(p->t.samplers.size(), 48);
    std::array<std::uint64_t, 48> keys{};       // each slot's sampler
    std::array<std::uint64_t, 48> views{};      // each slot's view (or texels)
    std::array<std::uint64_t, 48> seen{};       // what each slot reads: its texture and levels, never a descriptor's place
    for (std::size_t i = 0; i < n; ++i) {
        const SamplerSlot& slot = p->t.samplers[i];
        const Bound& b = d.bound[i];
        keys[i] = b.sampler.key();
        if (slot.kind == Sampled::buffer) {
            views[i] = b.buffer && b.buffer_bytes >= 16 ? mix(mix(reinterpret_cast<std::uintptr_t>(b.buffer), b.buffer_at), b.buffer_bytes) : 0;
            seen[i] = views[i];
            continue;
        }
        Texture* t = b.tex && ((b.tex->info.samples > 1) == (slot.kind == Sampled::tex2d_ms)) ? b.tex : empty(slot.kind);
        if (slot.kind == Sampled::tex3d && t->info.kind != TexKind::tex3d) t = empty(slot.kind);
        if (slot.kind == Sampled::cube && t->info.kind != TexKind::cube) t = empty(slot.kind);
        if (slot.kind == Sampled::tex2d_array && t->info.kind == TexKind::tex3d) t = empty(slot.kind);
        const int levels = t->info.levels;
        const int base = std::clamp(b.base_level, 0, levels - 1);
        const int top = std::clamp(b.max_level, base, levels - 1);
        to_state(t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        views[i] = srv(t, slot.kind, base, top - base + 1).ptr;
        seen[i] = mix(mix(reinterpret_cast<std::uintptr_t>(t), std::uint64_t(base) << 8 | std::uint64_t(top) << 16), std::uint64_t(slot.kind));
    }
    for (int loc = 0; loc < 16; ++loc)
        if ((p->t.inputs & (1u << loc)) && d.inputs[loc].buffer) to_state(d.inputs[loc].buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    if (d.indirect) to_state(d.indirect, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    bind_target();
    flush_barriers();
    ID3D12GraphicsCommandList* c = list();
    Bound_& was = bound_;
    if (root_now_ != p->root) {
        c->SetGraphicsRootSignature(p->root), root_now_ = p->root;
        // (Another root signature: its parameters set again.)
        was.cbv = 0, was.table = was.samplers = ~std::uint64_t{0};
    }
    ID3D12PipelineState* state = pso(p, d);
    if (pso_now_ != state) c->SetPipelineState(state), pso_now_ = state;

    const std::uint32_t block = std::max(p->t.block_size[0], p->t.block_size[1]);
    // (The same making of its uniforms, sent this frame already: that.)
    if (!d.uniforms_made || p->sent_made != d.uniforms_made || p->sent_epoch != epoch_ || !p->sent_va) {
        const Ring u = ring(std::max<std::size_t>((block + 255) & ~255u, 256), 256);
        if (block && d.uniforms) std::memcpy(u.host, d.uniforms, block);
        p->sent_va = u.va, p->sent_made = d.uniforms_made, p->sent_epoch = epoch_;
    }
    if (was.cbv != p->sent_va) {
        c->SetGraphicsRootConstantBufferView(0, p->sent_va);
        c->SetGraphicsRootConstantBufferView(1, p->sent_va);
        was.cbv = p->sent_va;
    }
    if (p->tables) {
        // The same pictures as the last draw read, through the same table: that.
        const std::uint64_t table_key = hash_bytes(seen.data(), n * sizeof(std::uint64_t), reinterpret_cast<std::uintptr_t>(p->root));
        if (table_key != was.table_key || was.table == ~std::uint64_t{0} || was.table_epoch != epoch_) {
            const std::uint32_t at = visible(static_cast<std::uint32_t>(p->tables));
            const std::size_t cpu = visible_cpu_.ptr + static_cast<std::size_t>(at) * visible_step_;
            for (std::size_t i = 0; i < n; ++i) {
                const D3D12_CPU_DESCRIPTOR_HANDLE dst{cpu + i * visible_step_};
                const Bound& b = d.bound[i];
                if (p->t.samplers[i].kind != Sampled::buffer) {
                    device_->CopyDescriptorsSimple(1, dst, {static_cast<std::size_t>(views[i])}, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                } else if (views[i]) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
                    sd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                    sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    sd.Buffer.FirstElement = b.buffer_at / 16;
                    sd.Buffer.NumElements = static_cast<UINT>(b.buffer_bytes / 16);
                    device_->CreateShaderResourceView(b.buffer->res, &sd, dst);
                } else {
                    device_->CopyDescriptorsSimple(1, dst, null_texel_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
            }
            was.table_key = table_key, was.table_epoch = epoch_;
            was.table = visible_gpu_.ptr + static_cast<std::uint64_t>(at) * visible_step_;
            c->SetGraphicsRootDescriptorTable(2, {was.table});
        } else if (was.table_set != was.table) {
            c->SetGraphicsRootDescriptorTable(2, {was.table});
        }
        was.table_set = was.table;
        const std::uint64_t sampler_key = hash_bytes(keys.data(), n * sizeof(std::uint64_t), n);
        if (sampler_key != was.samplers_key || was.samplers == ~std::uint64_t{0}) {
            was.samplers = sampler_table(keys.data(), n).ptr;
            was.samplers_key = sampler_key;
            c->SetGraphicsRootDescriptorTable(3, {was.samplers});
        }
    }

    std::array<D3D12_VERTEX_BUFFER_VIEW, 16> vbs{};
    for (int loc = 0; loc < 16; ++loc) {
        if (!(p->t.inputs & (1u << loc))) continue;
        const VertexInput& in = d.inputs[loc];
        if (in.buffer) {
            const std::size_t left = in.buffer->size > in.offset ? in.buffer->size - in.offset : 0;
            vbs[static_cast<std::size_t>(loc)] = {in.buffer->res->GetGPUVirtualAddress() + in.offset, static_cast<UINT>(left), in.stride};
        } else {
            const Ring v = ring(16, 16);
            std::memcpy(v.host, in.value, 16);
            vbs[static_cast<std::size_t>(loc)] = {v.va, 16, 0};
        }
    }
    if (std::memcmp(vbs.data(), was.vbs.data(), sizeof vbs) != 0 || !was.ia) {
        c->IASetVertexBuffers(0, 16, vbs.data());
        was.vbs = vbs;
    }
    if (!was.ia) c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST), was.ia = true;
    const D3D12_VIEWPORT vp{static_cast<float>(d.viewport[0]), static_cast<float>(d.viewport[1]), static_cast<float>(d.viewport[2]),
                            static_cast<float>(d.viewport[3]), d.depth_range[0], d.depth_range[1]};
    if (!was.rs || std::memcmp(&vp, &was.viewport, sizeof vp) != 0) c->RSSetViewports(1, &vp), was.viewport = vp;
    if (!was.rs || std::memcmp(&sc, &was.scissor, sizeof sc) != 0) c->RSSetScissorRects(1, &sc), was.scissor = sc;
    // (D3D12 blends by the constant's own channels, never by its alpha alone:
    // GL's constant-alpha factors have it in all four.)
    float constant[4] = {d.blend_constant[0], d.blend_constant[1], d.blend_constant[2], d.blend_constant[3]};
    const auto by_alpha = [](Factor f) { return f == Factor::constant_alpha || f == Factor::one_minus_constant_alpha; };
    const Raster& rr = d.raster;
    if (rr.blend && (by_alpha(rr.src_rgb) || by_alpha(rr.dst_rgb) || by_alpha(rr.src_a) || by_alpha(rr.dst_a)))
        constant[0] = constant[1] = constant[2] = constant[3];
    if (!was.rs || std::memcmp(constant, was.blend, sizeof was.blend) != 0)
        c->OMSetBlendFactor(constant), std::memcpy(was.blend, constant, sizeof was.blend);
    was.rs = true;
    if (d.indirect) c->ExecuteIndirect(draws_, static_cast<UINT>(d.count), d.indirect->res, d.indirect_at, nullptr, 0);
    else c->DrawInstanced(static_cast<UINT>(d.count), static_cast<UINT>(d.instances), static_cast<UINT>(d.first), 0);
}

void D3D12Device::dispatch(const Dispatch& d) {
    Program* p = d.program;
    if (!p || !p->compute) return;
    {
        Frame& f = frame();
        // (Room in this frame's uploads: the first chunk on from the one in use
        // with its 64 KB to spare - not only the one in use, or a frame with a
        // near-full chunk and a dozen empty ones after it would wait on the card.)
        while (f.chunk_now < f.chunks.size() && f.chunks[f.chunk_now].used + (std::size_t{64} << 10) > f.chunks[f.chunk_now].buffer->size) ++f.chunk_now;
        const bool room = f.chunk_now < f.chunks.size();
        if (!room && f.chunks.size() >= kChunks && asking_ < 0) recycle();
    }
    // (Its table's place, taken before anything is recorded: taking it may
    // hand the list in.)
    const std::uint32_t at = p->tables ? visible(static_cast<std::uint32_t>(p->tables)) : 0;
    // What its samplers read, in the state it is read in.
    const std::size_t n = std::min<std::size_t>(p->t.samplers.size(), 16);
    std::array<std::uint64_t, 16> keys{}, views{};
    for (std::size_t i = 0; i < n; ++i) {
        const SamplerSlot& slot = p->t.samplers[i];
        const Bound& b = d.bound[i];
        keys[i] = b.sampler.key();
        if (slot.kind == Sampled::buffer) continue;  // (no texel buffers in work on the card's own: none read)
        Texture* t = b.tex && ((b.tex->info.samples > 1) == (slot.kind == Sampled::tex2d_ms)) ? b.tex : empty(slot.kind);
        if (slot.kind == Sampled::tex3d && t->info.kind != TexKind::tex3d) t = empty(slot.kind);
        if (slot.kind == Sampled::cube && t->info.kind != TexKind::cube) t = empty(slot.kind);
        if (slot.kind == Sampled::tex2d_array && t->info.kind == TexKind::tex3d) t = empty(slot.kind);
        const int levels = t->info.levels;
        const int base = std::clamp(b.base_level, 0, levels - 1);
        const int top = std::clamp(b.max_level, base, levels - 1);
        to_state(t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        views[i] = srv(t, slot.kind, base, top - base + 1).ptr;
    }
    // What it reads and writes as UAVs: what a dispatch before wrote there
    // done first.
    for (const StorageSlot& s : p->t.storage) {
        Buffer* b = d.storage[s.binding].buffer;
        if (!b) return;
        if (b->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER u{};
            u.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            u.UAV.pResource = b->res;
            barriers_.push_back(u);
        }
        to_state(b, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    flush_barriers();
    ID3D12GraphicsCommandList* c = list();
    c->SetComputeRootSignature(p->root);
    // (The pipeline is the list's one, drawn with or not: the next draw sets its own.)
    c->SetPipelineState(p->compute), pso_now_ = nullptr;
    const std::uint32_t block = p->t.block_size[0];
    const Ring u = ring(std::max<std::size_t>((block + 255) & ~255u, 256), 256);
    if (block && d.uniforms) std::memcpy(u.host, d.uniforms, block);
    c->SetComputeRootConstantBufferView(0, u.va);
    UINT param = 1;
    for (const StorageSlot& s : p->t.storage) {
        const Storage& st = d.storage[s.binding];
        c->SetComputeRootUnorderedAccessView(param++, st.buffer->res->GetGPUVirtualAddress() + st.at);
    }
    if (p->tables) {
        const std::size_t cpu = visible_cpu_.ptr + static_cast<std::size_t>(at) * visible_step_;
        for (std::size_t i = 0; i < static_cast<std::size_t>(p->tables); ++i) {
            const D3D12_CPU_DESCRIPTOR_HANDLE dst{cpu + i * visible_step_};
            device_->CopyDescriptorsSimple(1, dst, i < n && views[i] ? D3D12_CPU_DESCRIPTOR_HANDLE{static_cast<std::size_t>(views[i])} : null_texel_,
                                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        c->SetComputeRootDescriptorTable(param++, {visible_gpu_.ptr + static_cast<std::uint64_t>(at) * visible_step_});
        c->SetComputeRootDescriptorTable(param++, sampler_table(keys.data(), static_cast<std::size_t>(p->tables)));
    }
    c->Dispatch(static_cast<UINT>(d.groups[0]), static_cast<UINT>(d.groups[1]), static_cast<UINT>(d.groups[2]));
}

// --- questions ------------------------------------------------------------------

void D3D12Device::begin_query(int slot) {
    if (asking_ >= 0) end_query(asking_);
    Slot& q = slots_[static_cast<std::size_t>(slot)];
    q.used = q.recording = true;
    kind_of_[static_cast<std::size_t>(slot)] = 0;
    list()->BeginQuery(query_heaps_[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, static_cast<UINT>(slot));
    asking_ = slot;
}

void D3D12Device::end_query(int slot) {
    if (asking_ != slot) return;
    list()->EndQuery(query_heaps_[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, static_cast<UINT>(slot));
    recorded_.push_back(slot);
    asking_ = -1;
}

void D3D12Device::stamp(int slot) {
    Slot& q = slots_[std::size_t{kQueries} + static_cast<std::size_t>(slot)];
    q.used = q.recording = true;
    kind_of_[static_cast<std::size_t>(slot)] = 1;
    list()->EndQuery(query_heaps_[1], D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>(slot));
    recorded_.push_back(slot);
}

bool D3D12Device::answer(int slot, bool wait, std::uint64_t& out) {
    const int kind = kind_of_[static_cast<std::size_t>(slot)];
    Slot& q = slots_[static_cast<std::size_t>(kind) * kQueries + static_cast<std::size_t>(slot)];
    out = 0;
    if (!q.used) return true;
    if (q.recording) {
        if (!wait) return false;
        if (asking_ == slot) end_query(slot);
        submit(true);
    }
    if (fence_->GetCompletedValue() < q.at) {
        if (!wait) return false;
        wait_value(q.at);
    }
    std::uint64_t v = 0;
    std::memcpy(&v, static_cast<std::uint8_t*>(answers_->host) + (static_cast<std::size_t>(kind) * kQueries + static_cast<std::size_t>(slot)) * 8, 8);
    out = kind ? static_cast<std::uint64_t>(static_cast<double>(v) * tick_ns_) : v;
    return true;
}

void D3D12Device::only_if(int slot) {
    ID3D12GraphicsCommandList* c = list();
    only_ = -1;
    if (slot < 0 || kind_of_[static_cast<std::size_t>(slot)] != 0 || !slots_[static_cast<std::size_t>(slot)].used) {
        c->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        return;
    }
    if (asking_ == slot) end_query(slot);
    to_state(predicates_, D3D12_RESOURCE_STATE_COPY_DEST);
    flush_barriers();
    c->ResolveQueryData(query_heaps_[0], D3D12_QUERY_TYPE_BINARY_OCCLUSION, static_cast<UINT>(slot), 1, predicates_->res,
                        static_cast<std::uint64_t>(slot) * 8);
    to_state(predicates_, D3D12_RESOURCE_STATE_PREDICATION);
    flush_barriers();
    c->SetPredication(predicates_->res, static_cast<std::uint64_t>(slot) * 8, D3D12_PREDICATION_OP_EQUAL_ZERO);
    only_ = slot;
}

// --- the window -------------------------------------------------------------------

bool D3D12Device::make_swapchain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return false;
    for (ID3D12Resource*& b : backbuffers_) release(b);
    for (D3D12_CPU_DESCRIPTOR_HANDLE r : back_rtvs_) rtv_heap_.give(r);
    back_rtvs_.clear();
    const UINT flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    if (!swapchain_) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = static_cast<UINT>(w);
        sd.Height = static_cast<UINT>(h);
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 3;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = flags;
        IDXGISwapChain1* sc1 = nullptr;
        if (FAILED(factory_->CreateSwapChainForHwnd(queue_, hwnd_, &sd, nullptr, nullptr, &sc1))) return false;
        factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        HR(sc1->QueryInterface(IID_PPV_ARGS(&swapchain_)));
        sc1->Release();
    } else {
        HR(swapchain_->ResizeBuffers(3, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_R8G8B8A8_UNORM, flags));
    }
    width_ = w, height_ = h;
    backbuffers_.assign(3, nullptr);
    for (UINT i = 0; i < 3; ++i) {
        HR(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])));
        back_rtvs_.push_back(rtv_heap_.take());
        device_->CreateRenderTargetView(backbuffers_[i], nullptr, back_rtvs_.back());
    }
    return true;
}

void D3D12Device::make_window_targets() {
    if (window_color_) destroy(window_color_);
    if (window_depth_) destroy(window_depth_);
    TextureInfo c;
    c.format = Format::rgba8;
    c.w = width_, c.h = height_;
    window_color_ = create_texture(c);
    TextureInfo d = c;
    d.format = Format::d24;
    window_depth_ = create_texture(d);
}

void D3D12Device::present(bool vsync) {
    ID3D12GraphicsCommandList* c = list();
    if (asking_ >= 0) end_query(asking_);
    c->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
    only_ = -1;
    const UINT index = swapchain_->GetCurrentBackBufferIndex();
    to_state(window_color_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {backbuffers_[index], 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET};
    barriers_.push_back(b);
    flush_barriers();
    const std::uint32_t at = visible(1);
    device_->CopyDescriptorsSimple(1, {visible_cpu_.ptr + static_cast<std::size_t>(at) * visible_step_}, srv(window_color_, Sampled::tex2d, 0, 1),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    c->OMSetRenderTargets(1, &back_rtvs_[index], FALSE, nullptr);
    c->SetGraphicsRootSignature(show_root_);
    c->SetPipelineState(show_pso_);
    c->SetGraphicsRootDescriptorTable(0, {visible_gpu_.ptr + static_cast<std::uint64_t>(at) * visible_step_});
    const D3D12_VIEWPORT vp{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    const D3D12_RECT sc{0, 0, width_, height_};
    c->RSSetViewports(1, &vp);
    c->RSSetScissorRects(1, &sc);
    c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->DrawInstanced(3, 1, 0, 0);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barriers_.push_back(b);
    target_bound_ = false;
    root_now_ = nullptr, pso_now_ = nullptr;
    bound_ = Bound_{};
    submit(false);
    swapchain_->Present(vsync ? 1 : 0, !vsync && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    if (w > 0 && h > 0 && (w != width_ || h != height_)) {
        for (Frame& fr : frames_) wait_value(fr.done_at);
        if (make_swapchain()) make_window_targets();
    }
    ++frame_index_;
    retire(frame());
}

}  // namespace

std::unique_ptr<Device> make_d3d12(GLFWwindow* window, std::string& why) {
    auto d = std::make_unique<D3D12Device>();
    if (!d->start(window, why)) return nullptr;
    return d;
}

}  // namespace sg::gl::native
