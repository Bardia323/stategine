# Drawn by Vulkan or D3D12

The renderer speaks OpenGL 3.3: `sg/gl` and whatever draws beside it (a
game's own screens, the lab's) call GL through the table `sg/gl/GL.hpp`
loads, and write their shaders in GLSL. Built with `SG_NATIVE_GPU`, that same
table can be answered by Vulkan 1.3 or D3D12 instead. Nothing that draws
changes, and nothing in the graph knows: no state, functor, look or law
says which card API drew it. GL stays the default.

```sh
cmake -S . -B build -DSG_NATIVE_GPU=ON   # downloads glslang, SPIRV-Cross, Vulkan headers, volk, VMA
SG_GPU=vulkan ./build/sg_room3d          # or d3d12; gl, or unset, is OpenGL
```

`sg/gl/Native.hpp` says the same in code (`use_api`, before the window is
made); `glGetString(GL_RENDERER)` names the API the window got. Where the
API asked for cannot start - no driver for it, no card that has what it
needs - the window says why on stderr and is drawn by OpenGL.

D3D12 builds its shaders with DXC (shader model 6): a program that ships
with it calls `stategine_native_runtime(target)`, which copies
`dxcompiler.dll` and `dxil.dll` from the Windows SDK beside the executable.
Without them beside it (or where `SG_DXC` says) the backend uses the SDK's
where one is installed, and otherwise does not start - the system's own
compiler (shader model 5) cannot take some of the renderer's programs.

## How it is built

`src/gl/native/`, a library of its own (`stategine_native`):

| File | What it is |
| --- | --- |
| `Front.cpp` | the GL front: every entry point in `GL.hpp`, keeping what GL keeps - names, bindings, the state a draw is made with - and saying each draw whole to a device. What GL does that a card does not, it does with draws of its own: blits, MSAA resolves, mipmaps, a packed picture read back unpacked |
| `Device.hpp` | what a device is asked: buffers, textures, programs; copy, clear, draw, ask (queries, conditional drawing), show |
| `Glsl.cpp` | GLSL as GL takes it to SPIR-V (glslang, Vulkan's rules relaxed so loose uniforms and samplers stand), its uniforms, samplers and inputs read back by name, and HLSL from that (SPIRV-Cross) |
| `Vulkan.cpp` | Vulkan 1.3: dynamic rendering, pushed descriptors, timeline semaphores, VMA |
| `D3D12.cpp` | D3D12: committed resources with their states kept, a root signature a program, DXC, built shaders kept under `%LOCALAPPDATA%/stategine/shaders` |

GL's own ways are kept where the card's differ:

- **Rows.** GL's first row is its lowest. Pictures are kept in GL's order
  on both cards - Vulkan's clip space already lays them so, D3D12's is
  turned in the vertex stage - and turned over only as the window shows
  them. Faces are wound as GL winds them.
- **Depth.** GL's clip space runs -1..1 unless `glClipControl` says 0..1;
  the vertex stage takes whichever is set to the card's 0..1.
- **Clip distances** a shader writes cut only while `GL_CLIP_DISTANCEi` is on.
- **sRGB.** The renderer never turns `GL_FRAMEBUFFER_SRGB` on, so what is
  drawn into an sRGB picture is kept as it is drawn; it is still read as sRGB.
- **Uniforms** are set by name, location and type as GL sets them (a value
  of the wrong type is refused, as GL refuses it), and sent to the card
  only when they have changed since it last read them.
- **Readback.** Rows 4-byte aligned, values converted as GL converts them;
  into a pixel-pack buffer, read later without a wait.
- **Questions.** Occlusion and time queries, and drawing on an occlusion
  query's answer, are the card's own.
- **Work on the card's own.** The front says GL 3.3 with
  `GL_ARB_compute_shader`, `GL_ARB_shader_storage_buffer_object`,
  `GL_ARB_draw_indirect` and `GL_ARB_multi_draw_indirect`, as a 3.3 context
  a driver gives them to does, so the renderer culls merged cells on the card
  (`gpu_cull`). A compute program runs between draws, in their order; a
  buffer bound for storage or draw commands lives on the card from then on;
  what it writes is seen by whatever is asked after it, so `glMemoryBarrier`
  has nothing to do. A compute program reads pictures through samplers as
  a draw does (no texel buffers).
- **Buffers GL changes.** What GL rewrites (`GL_DYNAMIC_DRAW`, a buffer
  written through a map) is laid in the frame's uploads as it changes, read
  from there. A big one (a megabyte and more) that is not rewritten three
  frames running - a land's mesh, laid again as the eye moves on - is
  copied onto the card when it changes and read from that copy: read from
  the uploads, every draw of it went over the bus (a sun's map of the land,
  forty times slower). Each frame's uploads are a ring of chunks; a draw that
  finds its chunk full goes on to the next with room, and only a frame that
  has filled them all waits for the card.

## Timing

A window shown on Vulkan or D3D12 waits for the screen's refresh as asked
(`SG_NO_VSYNC` says not to); a GL driver may not, for a window no one sees.
A card held to the refresh idles, slows its clocks, and what it does takes
longer: checks that compare the card's time (`sg_lamps_gl`, `sg_upscale_gl`)
ask for no refresh, as a benchmark should.

## Checks

The renderer's GL tests run on any API: `SG_GPU=vulkan ctest -R _gl` (or
`d3d12`). `SG_VK_VALIDATE=1` turns on Vulkan's validation layer where it is
installed, `SG_D3D12_DEBUG=1` D3D12's debug layer (its messages are said
when a call fails).
