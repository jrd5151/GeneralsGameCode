FetchContent_Declare(
    dx8
    GIT_REPOSITORY https://github.com/TheSuperHackers/min-dx8-sdk.git
    GIT_TAG        7bddff8c01f5fb931c3cb73d4aa8e66d303d97bc
)

FetchContent_MakeAvailable(dx8)

# TheSuperHackers @build The game has its own copies of the D3DX8 helpers it uses (d3dxmath.h, d3dxtex.h),
# because d3dx8 only exists for 32-bit x86. Only the tools that still call D3DX8 link it themselves.
get_target_property(D3D8LIB_LINKS d3d8lib INTERFACE_LINK_LIBRARIES)
list(REMOVE_ITEM D3D8LIB_LINKS d3dx8 d3dx8d)
set_target_properties(d3d8lib PROPERTIES INTERFACE_LINK_LIBRARIES "${D3D8LIB_LINKS}")

# TheSuperHackers @build The bundled libraries are 32-bit only. A 64-bit build keeps the SDK headers, takes
# dinput8 and dxguid from the Windows SDK, and needs no d3d8 import library, because the game loads d3d8.dll
# with LoadLibrary.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(d3d8lib PROPERTIES
        INTERFACE_LINK_LIBRARIES "dinput8;dxguid"
        INTERFACE_LINK_DIRECTORIES ""
        INTERFACE_LINK_OPTIONS ""
    )
endif()
