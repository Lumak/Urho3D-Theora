CMAKE_MINIMUM_REQUIRED( VERSION 2.8.12 )
set (TARGET_NAME libtheora)

#if( CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX )
#    find_package(Threads)
#endif()

set(Encoder_SRCS
  lib/apiwrapper.c   
  lib/fragment.c     
  lib/idct.c         
  lib/internal.c     
  lib/state.c        
  lib/quant.c        
  lib/analyze.c      
  lib/encfrag.c      
  lib/encapiwrapper.c
  lib/encinfo.c      
  lib/encode.c       
  lib/enquant.c      
  lib/fdct.c         
  lib/huffenc.c      
  lib/mathops.c      
  lib/mcenc.c        
  lib/rate.c         
  lib/tokenize.c
)

if( MSVC )
  set(EncoderPlatform_SRCS
    lib/x86_vc/mmxencfrag.c
    lib/x86_vc/mmxfdct.c
    lib/x86_vc/x86enc.c
    lib/x86_vc/mmxfrag.c
    lib/x86_vc/mmxidct.c
    lib/x86_vc/mmxstate.c
    lib/x86_vc/x86state.c
    )
else()
  set(EncoderPlatform_SRCS
    lib/x86/mmxencfrag.c
    lib/x86/mmxfdct.c
    lib/x86/x86enc.c
    lib/x86/mmxfrag.c
    lib/x86/mmxidct.c
    lib/x86/mmxstate.c
    lib/x86/x86state.c
    )
endif()

set(Decoder_SRCS
  lib/apiwrapper.c    
  lib/bitpack.c       
  lib/decapiwrapper.c 
  lib/decinfo.c       
  lib/decode.c        
  lib/dequant.c       
  lib/fragment.c      
  lib/huffdec.c       
  lib/idct.c          
  lib/info.c          
  lib/internal.c      
  lib/quant.c         
  lib/state.c         
)

if( MSVC )
  set(DecoderPlatform_SRCS
    lib/x86_vc/mmxidct.c
    lib/x86_vc/mmxfrag.c
    lib/x86_vc/mmxstate.c
    lib/x86_vc/x86state.c
    )
else()
  set(DecoderPlatform_SRCS
    lib/x86/mmxidct.c
    lib/x86/mmxfrag.c
    lib/x86/mmxstate.c
    lib/x86/x86state.c
    )
endif()

add_definitions( -D_CRT_SECURE_NO_WARNINGS -DOC_X86_ASM )
if (URHO3D_64BIT)
add_definitions( -DOC_X86_64_ASM )
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -Wall -Wno-parentheses")

file (GLOB CPP_FILES ${Encoder_SRCS} ${EncoderPlatform_SRCS} ${Decoder_SRCS} ${DecoderPlatform_SRCS})
file (GLOB H_FILES include/theora/*.h)

set (SOURCE_FILES ${CPP_FILES} ${H_FILES})
set (INCLUDE_DIRS include ${CMAKE_BINARY_DIR}/${DEST_INCLUDE_DIR}/ThirdParty/libogg)

# These are used to create visual studio folders.
source_group(Encoder FILES ${Encoder_SRCS} ${EncoderPlatform_SRCS} )
source_group(Decoder FILES ${Decoder_SRCS} ${DecoderPlatform_SRCS} )

# Setup target
setup_library ()

# Install headers for building and using the Urho3D library
install_header_files (DIRECTORY include/theora DESTINATION ${DEST_INCLUDE_DIR}/ThirdParty FILES_MATCHING PATTERN *.h BUILD_TREE_ONLY)

