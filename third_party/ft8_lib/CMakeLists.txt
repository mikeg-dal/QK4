add_library(qk4_ft8_codec STATIC
    ft8/constants.c ft8/crc.c ft8/decode.c ft8/encode.c ft8/ldpc.c
    ft8/message.c ft8/text.c common/monitor.c fft/kiss_fft.c fft/kiss_fftr.c)
target_include_directories(qk4_ft8_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(qk4_ft8_codec PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
if(NOT MSVC)
    target_link_libraries(qk4_ft8_codec PUBLIC m)
endif()

