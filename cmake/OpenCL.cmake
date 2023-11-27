set (OPENCL_AMDSDK /opt/AMDAPPSDK-3.0 CACHE PATH "Installation path for the installed AMD OpenCL SDK, if used")

# Options used when building the project
find_package (OpenCL)
if (NOT OpenCL_FOUND)
    # Attempt with AMD's OpenCL SDK
    find_library (LIB_OPENCL OpenCL PATHS ${OPENCL_AMDSDK}/lib/x86_64/)
    if (NOT LIB_OPENCL)
        set (ENABLE_OPENCL OFF)
    else ()
        # Found, set the required include path.
        set (OpenCL_INCLUDE_DIRS ${OPENCL_AMDSDK}/include CACHE PATH "AMD OpenCL SDK Header include path")
        set (OpenCL_COMPILATION_OPTIONS
            -Wno-ignored-attributes
            -Wno-deprecated-declarations
            CACHE STRING "AMD OpenCL SDK Compilation Option Requirements")
        message ("OpenCL support found in the AMD SDK")
    endif()
endif ()

if (PACK_DEBIAN)
    if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "^ppc")
        set(ENABLE_OPENCL OFF)
    endif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "^ppc")
endif (PACK_DEBIAN)

message ("OpenCL Support is: ${ENABLE_OPENCL}")

if (ENABLE_OPENCL)
    foreach (i IN ITEMS ${OpenCL_INCLUDE_DIRS})
        if (EXISTS ${i}/CL/opencl.hpp)
            set (OPENCL_V3 ON)
        endif ()
    endforeach ()
    if (OPENCL_V3)
        target_compile_definitions (qrack PUBLIC CL_HPP_TARGET_OPENCL_VERSION=300)
        target_compile_definitions (qrack PUBLIC CL_HPP_MINIMUM_OPENCL_VERSION=110)
    else (OPENCL_V3)
        target_compile_definitions (qrack PUBLIC CL_HPP_TARGET_OPENCL_VERSION=200)
        target_compile_definitions (qrack PUBLIC CL_HPP_MINIMUM_OPENCL_VERSION=110)
    endif (OPENCL_V3)

    if (ENABLE_SNUCL)
        find_package(MPI REQUIRED)
        set(QRACK_OpenCL_LIBRARIES snucl_cluster)
        set(QRACK_OpenCL_INCLUDE_DIRS ${MPI_CXX_INCLUDE_PATH} $ENV{SNUCLROOT}/inc)
        set(QRACK_OpenCL_LINK_DIRS $ENV{SNUCLROOT}/lib)
        set(QRACK_OpenCL_COMPILATION_OPTIONS ${MPI_CXX_COMPILE_FLAGS} ${OpenCL_COMPILATION_OPTIONS} -Wno-deprecated-declarations -Wno-ignored-attributes)
    else (ENABLE_SNUCL)
        set(QRACK_OpenCL_LIBRARIES ${OpenCL_LIBRARIES})
        set(QRACK_OpenCL_INCLUDE_DIRS ${OpenCL_INCLUDE_DIRS})
        set(QRACK_OpenCL_LINK_DIRS ${OpenCL_LINK_DIRS})
        set(QRACK_OpenCL_COMPILATION_OPTIONS ${OpenCL_COMPILATION_OPTIONS})
    endif (ENABLE_SNUCL)

    message ("SnuCL Support is: ${ENABLE_SNUCL}")
    message ("    libOpenCL: ${QRACK_OpenCL_LIBRARIES}")
    message ("    Includes:  ${QRACK_OpenCL_INCLUDE_DIRS}")
    message ("    Options:   ${QRACK_OpenCL_COMPILATION_OPTIONS}")

    link_directories (${QRACK_OpenCL_LINK_DIRS})
    target_include_directories (qrack PUBLIC ${PROJECT_BINARY_DIR} ${QRACK_OpenCL_INCLUDE_DIRS})
    target_compile_options (qrack PUBLIC ${QRACK_OpenCL_COMPILATION_OPTIONS})
    target_link_libraries (qrack ${QRACK_OpenCL_LIBRARIES})

    if (NOT ENABLE_EMIT_LLVM)
        # Declare the OCL precompilation executable
        add_executable (qrack_cl_precompile
            src/qrack_cl_precompile.cpp
            )
        target_link_libraries (qrack_cl_precompile ${QRACK_LIBS})
        target_compile_options (qrack_cl_precompile PUBLIC ${TEST_COMPILE_OPTS})
        install(TARGETS qrack_cl_precompile RUNTIME DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif (NOT ENABLE_EMIT_LLVM)

    # Build the OpenCL command files
    find_program (XXD_BIN xxd)
    file (GLOB_RECURSE COMPILABLE_RESOURCES "src/common/*.cl")
    foreach (INPUT_FILE ${COMPILABLE_RESOURCES})
        get_filename_component (INPUT_NAME ${INPUT_FILE} NAME)
        get_filename_component (INPUT_BASENAME ${INPUT_FILE} NAME_WE)
        get_filename_component (INPUT_DIR ${INPUT_FILE} DIRECTORY)

        set (OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/include/common/${INPUT_BASENAME}cl.hpp)

        message (" Creating XXD Rule for ${INPUT_FILE} -> ${OUTPUT_FILE}")
        add_custom_command (
            WORKING_DIRECTORY ${INPUT_DIR}
            OUTPUT ${OUTPUT_FILE}
            COMMAND ${XXD_BIN} -i ${INPUT_NAME} > ${OUTPUT_FILE}
            COMMENT "Building OpenCL Commands in ${INPUT_FILE}"
            )
        list (APPEND COMPILED_RESOURCES ${OUTPUT_FILE})
    endforeach ()

    # Add the OpenCL objects to the library
    target_sources (qrack PRIVATE
        ${COMPILED_RESOURCES}
        ${CMAKE_CURRENT_BINARY_DIR}/src/common/oclengine.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/src/qengine/opencl.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/src/qhybrid.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/src/qunitmulti.cpp
        )

endif (ENABLE_OPENCL)
