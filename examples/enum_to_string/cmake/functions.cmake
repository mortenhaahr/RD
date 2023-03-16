function(force_env_or_flag var_name)
# Checks if var_name exists as a CMake variable of system env variable. Sets the variable.
    if (NOT ${var_name}) # Defined as CMake variable = do nothing
        if (NOT "$ENV{${var_name}}" STREQUAL "") # Defined as env variable = set as CMake flag (if $ENV{${var_name}} != "")
            set(${var_name} "$ENV{${var_name}}" PARENT_SCOPE)
        else()
            message(FATAL_ERROR "${var_name} is required to build project. Please configure as environment variable or specify -D${var_name}")
        endif()
    endif()
endfunction()

function(set_clang_lib llvm_build var_name)
# Throws error if the folder ${llvm_build}/lib/clang doesn't exist
# Sets var_name to point to ${llvm_build}/lib/clang
if(IS_DIRECTORY "${llvm_build}/lib/clang")
    set(${var_name} "${llvm_build}/lib/clang" CACHE INTERNAL "Configured lib/clang folder")
else()
    message(FATAL_ERROR "Unable to find ${llvm_build}/lib/clang. Aborting.")
endif()
endfunction()

function(copy_lib_clang lib_clang)
    message(STATUS "Copying ${lib_clang}/lib/clang to ${CMAKE_BINARY_DIR}/lib")
    make_directory(${CMAKE_BINARY_DIR}/lib)
    file(COPY ${lib_clang} DESTINATION "${CMAKE_BINARY_DIR}/lib")
endfunction()

function(configure_clang_lib)
    force_env_or_flag("LLVM_BUILD")
    message(STATUS "Using LLVM_BUILD = ${LLVM_BUILD}")
    set_clang_lib("${LLVM_BUILD}" "LIB_CLANG")
    message(STATUS "Using LIB_CLANG = ${LIB_CLANG}")
    copy_lib_clang("${LIB_CLANG}")
endfunction()