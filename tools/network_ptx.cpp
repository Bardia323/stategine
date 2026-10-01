// Build-time device-only compiler. No game, state or running backend lives here.
#include <nvrtc.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    nvrtcProgram program = nullptr;
    try {
        const auto checked = [](nvrtcResult r) {
            if (r != NVRTC_SUCCESS) throw std::runtime_error(nvrtcGetErrorString(r));
        };
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("cannot read network kernel source");
        const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        checked(nvrtcCreateProgram(&program, source.c_str(), "network_cuda.cu", 0, nullptr, nullptr));
        const char* options[] = {"--std=c++17", "--gpu-architecture=compute_60", "--fmad=false"};
        const auto result = nvrtcCompileProgram(program, 3, options);
        std::size_t log_size = 0;
        checked(nvrtcGetProgramLogSize(program, &log_size));
        std::vector<char> log(log_size);
        checked(nvrtcGetProgramLog(program, log.data()));
        if (log_size > 1) std::cerr << log.data();
        checked(result);
        std::size_t size = 0;
        checked(nvrtcGetPTXSize(program, &size));
        std::vector<char> ptx(size);
        checked(nvrtcGetPTX(program, ptx.data()));
        std::ofstream output(argv[2], std::ios::binary);
        output.write(ptx.data(), static_cast<std::streamsize>(size - 1));
        if (!output) throw std::runtime_error("cannot write network PTX");
        checked(nvrtcDestroyProgram(&program));
        return 0;
    } catch (const std::exception& e) {
        if (program) nvrtcDestroyProgram(&program);
        std::cerr << "network PTX: " << e.what() << '\n';
        return 1;
    }
}
