// MotionBricks' model, run by ONNX Runtime on the processor (sg/domains/Bricks.hpp).
//
// The library is opened at run time (cmake/onnxruntime.cmake) and only its
// entry looked up; its C API does the rest. Where it is: SG_ONNXRUNTIME_LIB,
// beside the program, the build's own copy, then the system's. A build or a
// machine without it opens no model, and says why.
#include "sg/domains/Bricks.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>

#if defined(SG_ONNXRUNTIME)
#include <onnxruntime_c_api.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace sg::bricks {

namespace {
std::string model_path(const std::string& asked) {
    if (!asked.empty()) return asked;
    if (const char* e = std::getenv("SG_MOTIONBRICKS"); e && *e) return e;
#if defined(SG_MOTIONBRICKS_MODEL)
    return SG_MOTIONBRICKS_MODEL;
#else
    return {};
#endif
}

#if defined(SG_ONNXRUNTIME)
std::filesystem::path program_dir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? std::filesystem::path(std::wstring(buf, n)).parent_path() : std::filesystem::path{};
#else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : p.parent_path();
#endif
}

// The library's API, opened once for the program.
const OrtApi* ort_api(std::string* why) {
    static std::mutex m;
    static const OrtApi* api = nullptr;
    static std::string failed;
    std::lock_guard<std::mutex> lock(m);
    if (api || !failed.empty()) {
        if (!api && why) *why = failed;
        return api;
    }
    const char* file = SG_ORT_FILE;
    std::vector<std::string> where;
    if (const char* e = std::getenv("SG_ONNXRUNTIME_LIB"); e && *e) where.emplace_back(e);
    if (const auto d = program_dir(); !d.empty()) where.push_back((d / file).string());
    where.emplace_back(SG_ORT_LIBRARY);
    where.emplace_back(file);
    using GetBase = const OrtApiBase* (*)();
    GetBase get = nullptr;
    for (const std::string& w : where) {
#if defined(_WIN32)
        if (HMODULE h = LoadLibraryA(w.c_str())) get = reinterpret_cast<GetBase>(reinterpret_cast<void*>(GetProcAddress(h, "OrtGetApiBase")));
#else
        if (void* h = dlopen(w.c_str(), RTLD_NOW | RTLD_LOCAL)) get = reinterpret_cast<GetBase>(dlsym(h, "OrtGetApiBase"));
#endif
        if (get) break;
    }
    if (!get) {
        failed = std::string("ONNX Runtime's library (") + file + ") was not found beside the program, in SG_ONNXRUNTIME_LIB or the build";
    } else if (const OrtApiBase* base = get(); !base || !(api = base->GetApi(ORT_API_VERSION))) {
        failed = "ONNX Runtime's library is older than this build asks (API " + std::to_string(ORT_API_VERSION) + ")";
    }
    if (!api && why) *why = failed;
    return api;
}

// What a call said went wrong, taken (and freed).
bool failed(const OrtApi* api, OrtStatus* s, std::string* why) {
    if (!s) return false;
    if (why) *why = std::string("ONNX Runtime: ") + api->GetErrorMessage(s);
    api->ReleaseStatus(s);
    return true;
}
#endif
}  // namespace

struct Model::Impl {
#if defined(SG_ONNXRUNTIME)
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    ~Impl() {
        if (!api) return;
        if (session) api->ReleaseSession(session);
        if (options) api->ReleaseSessionOptions(options);
        if (memory) api->ReleaseMemoryInfo(memory);
        if (env) api->ReleaseEnv(env);
    }
#endif
};

Model::Model() : impl_(std::make_unique<Impl>()) {}
Model::~Model() = default;

bool Model::can() {
#if defined(SG_ONNXRUNTIME)
    return ort_api(nullptr) != nullptr;
#else
    return false;
#endif
}

std::unique_ptr<Model> Model::open(const std::string& asked, std::string* why) {
    const std::string path = model_path(asked);
#if !defined(SG_ONNXRUNTIME)
    (void)path;
    if (why) *why = "this build has no ONNX Runtime (SG_ONNXRUNTIME): no learned model can run";
    return nullptr;
#else
    if (path.empty()) {
        if (why) *why = "no MotionBricks model given: say where its .onnx is (SG_MOTIONBRICKS, or cmake's SG_MOTIONBRICKS_MODEL)";
        return nullptr;
    }
    if (!std::filesystem::exists(path)) {
        if (why) *why = "no MotionBricks model at " + path;
        return nullptr;
    }
    const OrtApi* api = ort_api(why);
    if (!api) return nullptr;
    std::unique_ptr<Model> m(new Model());
    Impl& i = *m->impl_;
    i.api = api;
    if (failed(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "stategine", &i.env), why)) return nullptr;
    if (failed(api, api->CreateSessionOptions(&i.options), why)) return nullptr;
    // (Its own few threads, as the library keeps them: SG_MOTIONBRICKS_THREADS, else four.)
    const char* t = std::getenv("SG_MOTIONBRICKS_THREADS");
    if (failed(api, api->SetIntraOpNumThreads(i.options, t ? std::max(1, std::atoi(t)) : 4), why)) return nullptr;
    if (failed(api, api->SetSessionGraphOptimizationLevel(i.options, ORT_ENABLE_ALL), why)) return nullptr;
#if defined(_WIN32)
    const std::wstring wide = std::filesystem::path(path).wstring();
    if (failed(api, api->CreateSession(i.env, wide.c_str(), i.options, &i.session), why)) return nullptr;
#else
    if (failed(api, api->CreateSession(i.env, path.c_str(), i.options, &i.session), why)) return nullptr;
#endif
    if (failed(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &i.memory), why)) return nullptr;
    return m;
#endif
}

bool Model::generate(const Skeleton& sk, const std::array<Pose, 4>& context, const std::vector<Pose>& targets, const Settings& s, Motion& out,
                     std::string* why) {
#if !defined(SG_ONNXRUNTIME)
    (void)sk, (void)context, (void)targets, (void)s, (void)out;
    if (why) *why = "this build has no ONNX Runtime";
    return false;
#else
    const auto began = std::chrono::steady_clock::now();
    if (targets.empty() || targets.size() > static_cast<std::size_t>(kPerToken)) {
        if (why) *why = "a motion is made toward one to four target poses";
        return false;
    }
    const std::vector<std::string>& names = model_joints();
    for (const std::string& n : names)
        if (sk.index(n) < 0) {
            if (why) *why = "the skeleton has no joint " + n + ", which the model moves";
            return false;
        }
    Impl& i = *impl_;
    const OrtApi* api = i.api;
    Inputs in{};
    const Xf alignment = encode(sk, context, targets, s, in);
    struct Tensor {
        const char* name;
        void* data;
        std::size_t bytes;
        std::vector<int64_t> shape;
        ONNXTensorElementDataType type;
    };
    const auto F = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, I = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32;
    Tensor ts[] = {{"global_root_values", in.global_root, sizeof in.global_root, {1, 8, 5}, F},
                   {"local_root_values", in.local_root, sizeof in.local_root, {1, 8, 4}, F},
                   {"local_poses", in.local_poses, sizeof in.local_poses, {1, 8, 267}, F},
                   {"has_global_root_values", in.has_global_root, sizeof in.has_global_root, {1, 8}, I},
                   {"has_local_root_values", in.has_local_root, sizeof in.has_local_root, {1, 8}, I},
                   {"has_local_poses", in.has_poses, sizeof in.has_poses, {1, 8}, I},
                   {"allowed_pred_num_tokens", in.allowed, sizeof in.allowed, {1, 11}, I}};
    constexpr std::size_t kIn = sizeof ts / sizeof ts[0];
    OrtValue* inputs[kIn] = {};
    const char* in_names[kIn];
    bool ok = true;
    for (std::size_t k = 0; k < kIn && ok; ++k) {
        in_names[k] = ts[k].name;
        ok = !failed(api, api->CreateTensorWithDataAsOrtValue(i.memory, ts[k].data, ts[k].bytes, ts[k].shape.data(), ts[k].shape.size(), ts[k].type, &inputs[k]), why);
    }
    const char* out_names[] = {"pred_global_motions", "pred_num_tokens"};
    OrtValue* outputs[2] = {nullptr, nullptr};
    if (ok) ok = !failed(api, api->Run(i.session, nullptr, in_names, inputs, kIn, out_names, 2, outputs), why);
    if (ok) {
        float* pred = nullptr;
        int32_t* tokens = nullptr;
        ok = !failed(api, api->GetTensorMutableData(outputs[0], reinterpret_cast<void**>(&pred)), why) &&
             !failed(api, api->GetTensorMutableData(outputs[1], reinterpret_cast<void**>(&tokens)), why);
        if (ok && (tokens[0] < kMinTokens || tokens[0] > kMaxTokens || std::find(s.tokens.begin(), s.tokens.end(), tokens[0]) == s.tokens.end())) {
            if (why) *why = "the model chose " + std::to_string(tokens[0]) + " tokens, which were not allowed";
            ok = false;
        }
        if (ok) {
            decode(sk, pred, tokens[0], alignment, out);
            if (s.correct) correct(context, targets.front(), out);
        }
    }
    for (OrtValue* v : inputs)
        if (v) api->ReleaseValue(v);
    for (OrtValue* v : outputs)
        if (v) api->ReleaseValue(v);
    last_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    return ok;
#endif
}

}  // namespace sg::bricks
