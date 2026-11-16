# libFuzzer harnesses for the three parsers that consume untrusted bytes.
#
# Build them in a dedicated tree (tools/fuzz.sh does): the library itself has to
# carry sanitizer coverage, otherwise the fuzzer gets no feedback from the code
# under test and the sanitizer never sees its memory errors.
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "IOLINK_BUILD_FUZZERS needs clang: the harnesses use libFuzzer")
endif()
if(CMAKE_VERSION VERSION_LESS 3.13)
    message(FATAL_ERROR "IOLINK_BUILD_FUZZERS needs CMake 3.13+ (target_link_options)")
endif()

target_compile_options(iolinki PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-sanitize-recover=all
    -O1
    -g)

set(IOLINK_FUZZ_TARGETS fuzz_frame fuzz_isdu fuzz_od)
foreach(_fuzz_target IN LISTS IOLINK_FUZZ_TARGETS)
    add_executable(${_fuzz_target} ${_fuzz_target}.c)
    target_link_libraries(${_fuzz_target} PRIVATE iolinki)
    target_compile_options(${_fuzz_target} PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-sanitize-recover=all
        -O1
        -g)
    target_link_options(${_fuzz_target} PRIVATE -fsanitize=fuzzer,address,undefined)
endforeach()
