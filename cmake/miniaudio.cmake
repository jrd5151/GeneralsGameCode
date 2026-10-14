# Fetch miniaudio, which plays the game's sound in place of the Miles Sound System.

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        9634bedb5b5a2ca38c1ee7108a9358a4e233f14d # 0.11.25
    # Only fetch the sources: miniaudio's own CMake project builds tests and extras we don't need.
    SOURCE_SUBDIR  do-not-add
)

FetchContent_MakeAvailable(miniaudio)

add_library(deps_miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(deps_miniaudio PUBLIC ${miniaudio_SOURCE_DIR})
target_compile_definitions(deps_miniaudio PUBLIC
    MA_NO_RESOURCE_MANAGER
    MA_NO_GENERATION
    MA_NO_ENCODING
    MA_NO_FLAC
)
