# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-brookesia VERSION 0.1.65)

idf_build_get_property(build_components BUILD_COMPONENTS)
if("espressif__esp-dsp" IN_LIST build_components)
    idf_component_get_property(DSP_LIB espressif__esp-dsp COMPONENT_LIB)
    if(TARGET ${DSP_LIB})
        target_compile_options(
            ${DSP_LIB}
            PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/main/dsp_cxx_math_compat.hpp>"
        )
    endif()
endif()
