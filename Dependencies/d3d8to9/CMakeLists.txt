# d3d8to9 by Patrick Mours (BSD-2-Clause, see LICENSE.md), from https://github.com/crosire/d3d8to9 at commit
# 255338f698c8270b537f0a91a13f795f4f988250. It implements Direct3D 8 on top of Direct3D 9.
#
# Windows has no 64-bit d3d8.dll, so the 64-bit game links this in and uses it when D3D8.DLL does not load.
# Changes from upstream are marked with "TheSuperHackers": shader and state block handles are table keys
# instead of pointers, the entry point is renamed to D3D8to9_Direct3DCreate8, and a missing d3dx9_43.dll
# no longer shows a message box.

add_library(deps_d3d8to9 STATIC
    source/d3d8.hpp
    source/d3d8to9.cpp
    source/d3d8to9.hpp
    source/d3d8to9_base.cpp
    source/d3d8to9_device.cpp
    source/d3d8to9_index_buffer.cpp
    source/d3d8to9_surface.cpp
    source/d3d8to9_swap_chain.cpp
    source/d3d8to9_texture.cpp
    source/d3d8to9_vertex_buffer.cpp
    source/d3d8to9_volume.cpp
    source/d3d8types.cpp
    source/d3d8types.hpp
    source/d3dx9.hpp
    source/interface_query.cpp
    source/interface_query.hpp
)

set_target_properties(deps_d3d8to9 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(deps_d3d8to9 PRIVATE D3D8TO9NOLOG)

if(MSVC)
    target_compile_options(deps_d3d8to9 PRIVATE /W3)
else()
    target_compile_options(deps_d3d8to9 PRIVATE -Wno-delete-non-virtual-dtor -Wno-unknown-pragmas)
endif()

target_link_libraries(deps_d3d8to9 PUBLIC d3d9)
