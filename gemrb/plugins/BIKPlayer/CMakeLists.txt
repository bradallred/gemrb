if(HAVE_LDEXPF EQUAL 1)
ADD_GEMRB_PLUGIN ( BIKPlayer BIKPlayer.cpp dct.cpp fft.cpp GetBitContext.cpp mem.cpp rational.cpp rdft.cpp )
endif()
