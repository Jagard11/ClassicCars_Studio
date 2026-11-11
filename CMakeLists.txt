cmake_minimum_required(VERSION 3.10)

# A Conda shell exports CC, CFLAGS, and LDFLAGS for an older sysroot and can
# bake -rpath into that prefix. Configure with the system compiler on Linux.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  foreach(_var CC CXX CFLAGS CXXFLAGS CPPFLAGS LDFLAGS AR LD RANLIB NM STRIP CONDA_BUILD_SYSROOT HOST build_alias host_alias)
    unset(ENV{${_var}})
  endforeach()
  set(_studio_cc "")
  if(EXISTS "/usr/bin/cc")
    set(_studio_cc "/usr/bin/cc")
  elseif(EXISTS "/usr/bin/gcc")
    set(_studio_cc "/usr/bin/gcc")
  endif()
  if(_studio_cc)
    set(ENV{CC} "${_studio_cc}")
    if(NOT CMAKE_C_COMPILER OR CMAKE_C_COMPILER MATCHES "miniconda3|anaconda3")
      set(CMAKE_C_COMPILER "${_studio_cc}" CACHE FILEPATH "System C compiler" FORCE)
    endif()
  endif()
  foreach(_flag CMAKE_C_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
    if("${${_flag}}" MATCHES "miniconda3|anaconda3")
      unset(${_flag})
      unset(${_flag} CACHE)
    endif()
  endforeach()
  unset(_var)
  unset(_flag)
  unset(_studio_cc)
endif()

project(classiccars-studio C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(classiccars-studio
  src/main.c
  src/http.c
  src/router.c
  src/ssi.c
  src/proxy.c
  src/settings.c
  src/json.c
  src/gallery.c
  src/preview_draft.c
  src/api.c
  src/window.c
)

target_include_directories(classiccars-studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(classiccars-studio PRIVATE /W4)
else()
  target_compile_options(classiccars-studio PRIVATE -Wall -Wextra)
endif()

if(WIN32)
  target_link_libraries(classiccars-studio PRIVATE ws2_32 gdi32 user32 ole32 oleaut32 uuid shlwapi advapi32)
  if(MSVC)
    target_link_options(classiccars-studio PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
  else()
    target_link_options(classiccars-studio PRIVATE "-Wl,--subsystem,windows" "-Wl,-e,mainCRTStartup" "-mwindows")
    if(MINGW)
      target_link_options(classiccars-studio PRIVATE "-static")
    endif()
  endif()
else()
  target_compile_definitions(classiccars-studio PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
  find_package(Threads REQUIRED)
  target_link_libraries(classiccars-studio PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

enable_testing()
add_test(NAME studio-check COMMAND classiccars-studio --check)
