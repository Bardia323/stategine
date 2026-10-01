# Pinned upstream cryptography, including its standard Ed25519 module.
enable_language(C)
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(monocypher
  URL https://monocypher.org/download/monocypher-4.0.3.tar.gz
  URL_HASH SHA512=40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965)
FetchContent_MakeAvailable(monocypher)
add_library(sg_net_crypto STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c
  ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(sg_net_crypto PUBLIC ${monocypher_SOURCE_DIR}/src ${monocypher_SOURCE_DIR}/src/optional)
target_link_libraries(stategine_net PRIVATE sg_net_crypto)
if(WIN32)
  target_link_libraries(stategine_net PRIVATE bcrypt)
endif()
