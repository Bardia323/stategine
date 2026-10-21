// Stategine - NVIDIA's DLSS on the Vulkan device (Dlss.hpp). The SDK's
// headers say what NGX's names and structures are; its functions are the
// driver's, looked up by name in its NGX core, so nothing NVIDIA's is linked
// and a program built with any compiler (MinGW too) reaches it.
#include "Dlss.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#if defined(SG_DLSS)
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_defs_vk.h>
#include <nvsdk_ngx_params.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace sg::gl::native {

#if !defined(SG_DLSS)

// (Built without the SDK: there is no DLSS to ask.)
void Dlss::wanted(std::vector<std::string>&, std::vector<std::string>&) {}
Dlss::~Dlss() = default;
bool Dlss::start(VkInstance, VkPhysicalDevice, VkDevice, std::string& why) {
    why = "built without DLSS (SG_DLSS off, or its SDK not fetched)";
    return false;
}
bool Dlss::evaluate(VkCommandBuffer, const Ask&, std::string& why) {
    why = "built without DLSS";
    return false;
}

#else

namespace {

using Result = NVSDK_NGX_Result;
bool failed(Result r) { return (static_cast<unsigned>(r) & 0xFFF00000u) == static_cast<unsigned>(NVSDK_NGX_Result_Fail); }

// The core's own entry points (as it exports them: the SDK's static library
// is a thin layer over these, the version said before the feature's paths).
using PfnRequired = Result (*)(unsigned*, const char***, unsigned*, const char***);
using PfnInit = Result (*)(const char*, NVSDK_NGX_EngineType, const char*, const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice,
                           PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo*);
using PfnParams = Result (*)(NVSDK_NGX_Parameter**);
using PfnDestroyParams = Result (*)(NVSDK_NGX_Parameter*);
using PfnCreate = Result (*)(VkDevice, VkCommandBuffer, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using PfnEvaluate = Result (*)(VkCommandBuffer, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, void*);
using PfnRelease = Result (*)(NVSDK_NGX_Handle*);
// (Shut down as the core is: how many devices it still has said back.)
using PfnShutdown = Result (*)(VkDevice, unsigned*);

// A parameter block is a C++ object of the core's: its Set and Get called
// through its table of functions, each by its place there - the order they
// are declared in (nvsdk_ngx_params.h) where the core is built by GCC, and
// each overloaded name's in turn backwards where it is built by MSVC (as
// MSVC lays out overloads; what clang targeting MSVC lays out, checked).
enum Slot { set_ull, set_f, set_d, set_ui, set_i, set_d3d11, set_d3d12, set_ptr, get_ull, get_f, get_d, get_ui, get_i, get_d3d11, get_d3d12, get_ptr };
constexpr int place(Slot s) {
#if defined(_WIN32)
    return s < get_ull ? 7 - s : 23 - s;
#else
    return s;
#endif
}
struct Params {
    NVSDK_NGX_Parameter* p = nullptr;
    template <class Fn>
    Fn at(Slot s) const {
        return reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(p))[place(s)]);
    }
    void set(const char* n, int v) const { at<void (*)(NVSDK_NGX_Parameter*, const char*, int)>(set_i)(p, n, v); }
    void set(const char* n, unsigned v) const { at<void (*)(NVSDK_NGX_Parameter*, const char*, unsigned)>(set_ui)(p, n, v); }
    void set(const char* n, float v) const { at<void (*)(NVSDK_NGX_Parameter*, const char*, float)>(set_f)(p, n, v); }
    void set(const char* n, void* v) const { at<void (*)(NVSDK_NGX_Parameter*, const char*, void*)>(set_ptr)(p, n, v); }
    Result get(const char* n, int* v) const { return at<Result (*)(NVSDK_NGX_Parameter*, const char*, int*)>(get_i)(p, n, v); }
};

std::filesystem::path program_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? std::filesystem::path(buf).parent_path() : std::filesystem::path{};
#else
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

// The driver's NGX core, by name - looked for again until found (on
// Windows, beside the driver's Vulkan once the device has loaded it).
void* core = nullptr;
void* core_lib() {
    if (core) return core;
    if (const char* off = std::getenv("SG_NO_DLSS"); off && *off == '1') return nullptr;
#if defined(_WIN32)
    // Where the driver says it put it (the legacy key every NVIDIA driver
    // keeps, and the service's own), and beside the driver's Vulkan, which
    // lives in the same folder of its store.
    std::vector<std::filesystem::path> dirs;
    const auto key = [&](const wchar_t* hive, const wchar_t* name) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, hive, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
        wchar_t buf[MAX_PATH] = {};
        DWORD size = sizeof buf;
        if (RegQueryValueExW(k, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) dirs.emplace_back(buf);
        RegCloseKey(k);
    };
    key(L"System\\CurrentControlSet\\Services\\nvlddmkm\\Parameters\\NGXCore", L"NGXPath");
    key(L"System\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore", L"NGXPath");
    key(L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", L"FullPath");
    for (const wchar_t* icd : {L"nvoglv64.dll", L"nvoglv32.dll"})
        if (HMODULE m = GetModuleHandleW(icd)) {
            wchar_t buf[MAX_PATH] = {};
            if (GetModuleFileNameW(m, buf, MAX_PATH)) dirs.push_back(std::filesystem::path(buf).parent_path());
        }
    for (const auto& d : dirs)
        for (const wchar_t* name : {L"_nvngx.dll", L"nvngx.dll"})
            if (HMODULE m = LoadLibraryW((d / name).c_str())) return core = reinterpret_cast<void*>(m);
    return nullptr;
#else
    return core = dlopen("libnvidia-ngx.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
}
template <class Fn>
Fn fn(const char* name) {
    void* lib = core_lib();
    if (!lib) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name)));
#else
    return reinterpret_cast<Fn>(dlsym(lib, name));
#endif
}

std::string said(Result r) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "NGX 0x%08x", static_cast<unsigned>(r));
    return buf;
}

void NVSDK_CONV log_line(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) { std::fprintf(stderr, "ngx: %s", message); }

}  // namespace

void Dlss::wanted(std::vector<std::string>& instance, std::vector<std::string>& device) {
    if (auto required = fn<PfnRequired>("NVSDK_NGX_VULKAN_RequiredExtensions")) {
        unsigned ni = 0, nd = 0;
        const char** is = nullptr;
        const char** ds = nullptr;
        if (!failed(required(&ni, &is, &nd, &ds))) {
            for (unsigned i = 0; i < ni; ++i) instance.emplace_back(is[i]);
            for (unsigned i = 0; i < nd; ++i) device.emplace_back(ds[i]);
            return;
        }
    }
    // (Where the core is not found until the device is - beside the
    // driver's Vulkan, on Windows - what DLSS has always asked.)
    device.emplace_back("VK_NVX_binary_import");
    device.emplace_back("VK_NVX_image_view_handle");
}

struct Dlss::Impl {
    VkDevice device = VK_NULL_HANDLE;
    PfnParams capability = nullptr, allocate = nullptr;
    PfnDestroyParams destroy = nullptr;
    PfnCreate create = nullptr;
    PfnEvaluate evaluate = nullptr;
    PfnRelease release = nullptr;
    PfnShutdown shutdown = nullptr;
    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    int in_w = 0, in_h = 0, out_w = 0, out_h = 0, quality = -1;
    bool reversed = false, auto_exposure = false;
};

Dlss::~Dlss() {
    if (!impl_) return;
    if (impl_->feature && impl_->release) impl_->release(impl_->feature);
    if (impl_->params && impl_->destroy) impl_->destroy(impl_->params);
    unsigned left = 0;
    if (impl_->shutdown) impl_->shutdown(impl_->device, &left);
    delete impl_;
}

bool Dlss::start(VkInstance instance, VkPhysicalDevice physical, VkDevice device, std::string& why) {
    if (impl_) return true;
    auto init = fn<PfnInit>("NVSDK_NGX_VULKAN_Init_ProjectID_Ext");
    if (!core_lib()) {
        why = "no NGX core (it comes with NVIDIA's driver)";
        return false;
    }
    Impl im;
    im.device = device;
    im.capability = fn<PfnParams>("NVSDK_NGX_VULKAN_GetCapabilityParameters");
    im.allocate = fn<PfnParams>("NVSDK_NGX_VULKAN_AllocateParameters");
    im.destroy = fn<PfnDestroyParams>("NVSDK_NGX_VULKAN_DestroyParameters");
    im.create = fn<PfnCreate>("NVSDK_NGX_VULKAN_CreateFeature1");
    im.evaluate = fn<PfnEvaluate>("NVSDK_NGX_VULKAN_EvaluateFeature");
    im.release = fn<PfnRelease>("NVSDK_NGX_VULKAN_ReleaseFeature");
    im.shutdown = fn<PfnShutdown>("NVSDK_NGX_VULKAN_Shutdown1");
    if (!init || !im.capability || !im.destroy || !im.create || !im.evaluate || !im.release || !im.shutdown) {
        why = "the driver's NGX core is too old for DLSS on Vulkan";
        return false;
    }
    // Where DLSS's own file is looked for besides the program's folder: the
    // SDK's, where this was built (SG_DLSS_DIR), and where SG_DLSS_PATH says.
    std::vector<std::wstring> paths;
    if (const char* p = std::getenv("SG_DLSS_PATH"); p && *p) paths.push_back(std::filesystem::path(p).wstring());
    paths.push_back(program_dir().wstring());
#if defined(SG_DLSS_DIR)
    paths.push_back(std::filesystem::path(SG_DLSS_DIR).wstring());
#endif
    std::vector<const wchar_t*> list;
    for (const auto& p : paths) list.push_back(p.c_str());
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = list.data();
    info.PathListInfo.Length = static_cast<unsigned>(list.size());
    if (const char* l = std::getenv("SG_DLSS_LOG"); l && *l == '1') {
        info.LoggingInfo.LoggingCallback = &log_line;
        info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
        info.LoggingInfo.DisableOtherLoggingSinks = true;
    }
    // Its logs and what it keeps: in the system's place for passing files.
    std::error_code ec;
    const std::filesystem::path data = std::filesystem::temp_directory_path(ec) / "stategine-ngx";
    std::filesystem::create_directories(data, ec);
    // (A project's own id, as NGX asks of an engine of no name it knows.)
    const Result r = init("5f0e3c9a-7b1d-4c2e-9a6f-3d8b2e1c4a70", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", data.wstring().c_str(), instance, physical, device,
                          vkGetInstanceProcAddr, vkGetDeviceProcAddr, NVSDK_NGX_Version_API, &info);
    if (failed(r)) {
        why = "NGX did not start on this card (" + said(r) + ")";
        return false;
    }
    unsigned left = 0;
    if (failed(im.capability(&im.params)) || !im.params) {
        im.shutdown(device, &left);
        why = "NGX gave no parameters";
        return false;
    }
    const Params p{im.params};
    int available = 0, needs_driver = 0;
    p.get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    p.get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
    if (!available) {
        im.destroy(im.params);
        im.shutdown(device, &left);
        why = needs_driver ? "DLSS needs a newer driver" : "this card has no DLSS (or its file, nvngx_dlss, was not found)";
        return false;
    }
    impl_ = new Impl(im);
    return true;
}

bool Dlss::evaluate(VkCommandBuffer cmd, const Ask& a, std::string& why) {
    if (!impl_) {
        why = "DLSS not started";
        return false;
    }
    Impl& im = *impl_;
    const Params p{im.params};
    // Made again when what it is made for changes: the sizes, the quality,
    // which way depth runs.
    if (!im.feature || im.in_w != a.color.w || im.in_h != a.color.h || im.out_w != a.output.w || im.out_h != a.output.h || im.quality != a.quality ||
        im.reversed != a.reversed || im.auto_exposure != (a.exposure <= 0.0f)) {
        if (im.feature) im.release(im.feature), im.feature = nullptr;
        static const NVSDK_NGX_PerfQuality_Value kQuality[] = {NVSDK_NGX_PerfQuality_Value_MaxQuality, NVSDK_NGX_PerfQuality_Value_Balanced,
                                                               NVSDK_NGX_PerfQuality_Value_MaxPerf, NVSDK_NGX_PerfQuality_Value_UltraPerformance,
                                                               NVSDK_NGX_PerfQuality_Value_DLAA};
        // (Its own exposure only where the eye's is not said: DLSS's, found
        // from the picture, holds back the brightest light - a window's sky
        // came out greyer than the TAA's.)
        int flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        if (a.exposure <= 0.0f) flags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        if (a.reversed) flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        p.set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
        p.set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
        p.set(NVSDK_NGX_Parameter_Width, static_cast<unsigned>(a.color.w));
        p.set(NVSDK_NGX_Parameter_Height, static_cast<unsigned>(a.color.h));
        p.set(NVSDK_NGX_Parameter_OutWidth, static_cast<unsigned>(a.output.w));
        p.set(NVSDK_NGX_Parameter_OutHeight, static_cast<unsigned>(a.output.h));
        p.set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(kQuality[a.quality < 0 || a.quality > 4 ? 0 : a.quality]));
        p.set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
        p.set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);
        const Result r = im.create(im.device, cmd, NVSDK_NGX_Feature_SuperSampling, im.params, &im.feature);
        if (failed(r) || !im.feature) {
            im.feature = nullptr;
            why = "DLSS could not be made for " + std::to_string(a.color.w) + "x" + std::to_string(a.color.h) + " to " + std::to_string(a.output.w) + "x" +
                  std::to_string(a.output.h) + " (" + said(r) + ")";
            return false;
        }
        im.in_w = a.color.w, im.in_h = a.color.h, im.out_w = a.output.w, im.out_h = a.output.h, im.quality = a.quality, im.reversed = a.reversed;
        im.auto_exposure = a.exposure <= 0.0f;
    }
    const auto resource = [](const Image& i, bool write) {
        NVSDK_NGX_Resource_VK r{};
        r.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
        r.Resource.ImageViewInfo.ImageView = i.view;
        r.Resource.ImageViewInfo.Image = i.image;
        r.Resource.ImageViewInfo.SubresourceRange = {static_cast<VkImageAspectFlags>(i.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
        r.Resource.ImageViewInfo.Format = i.format;
        r.Resource.ImageViewInfo.Width = static_cast<unsigned>(i.w);
        r.Resource.ImageViewInfo.Height = static_cast<unsigned>(i.h);
        r.ReadWrite = write;
        return r;
    };
    NVSDK_NGX_Resource_VK color = resource(a.color, false), depth = resource(a.depth, false), motion = resource(a.motion, false),
                          output = resource(a.output, true);
    p.set(NVSDK_NGX_Parameter_Color, static_cast<void*>(&color));
    p.set(NVSDK_NGX_Parameter_Output, static_cast<void*>(&output));
    p.set(NVSDK_NGX_Parameter_Depth, static_cast<void*>(&depth));
    p.set(NVSDK_NGX_Parameter_MotionVectors, static_cast<void*>(&motion));
    p.set(NVSDK_NGX_Parameter_TransparencyMask, static_cast<void*>(nullptr));
    p.set(NVSDK_NGX_Parameter_ExposureTexture, static_cast<void*>(nullptr));
    p.set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, static_cast<void*>(nullptr));
    p.set(NVSDK_NGX_Parameter_Jitter_Offset_X, a.jitter_x);
    p.set(NVSDK_NGX_Parameter_Jitter_Offset_Y, a.jitter_y);
    p.set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    p.set(NVSDK_NGX_Parameter_Reset, a.reset ? 1 : 0);
    p.set(NVSDK_NGX_Parameter_MV_Scale_X, a.motion_x);
    p.set(NVSDK_NGX_Parameter_MV_Scale_Y, a.motion_y);
    p.set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, static_cast<unsigned>(a.color.w));
    p.set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, static_cast<unsigned>(a.color.h));
    // (The picture as the eye will see it: the light, times its exposure.)
    p.set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, a.exposure > 0.0f ? 1.0f / a.exposure : 1.0f);
    p.set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
    p.set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, a.frame_ms);
    for (const char* zero : {NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,
                             NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,
                             NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y,
                             NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y})
        p.set(zero, 0u);
    const Result r = im.evaluate(cmd, im.feature, im.params, nullptr);
    if (failed(r)) {
        why = "DLSS did not run (" + said(r) + ")";
        return false;
    }
    return true;
}

#endif

}  // namespace sg::gl::native
