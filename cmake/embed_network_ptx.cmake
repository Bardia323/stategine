# Device code is built into the library; no runtime source file is a dependency.
file(READ "${IN}" ptx)
file(WRITE "${OUT}" "// Generated from src/gpu/network_cuda.cu.\n#pragma once\nstatic const char sg_network_ptx[] = R\"SG_NET_PTX(${ptx})SG_NET_PTX\";\n")
