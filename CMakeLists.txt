# 
# dvblast++ cmake script
# Copyright (C) 2016, Kylone
# Authors: Gokhan Poyraz <gokhan@kylone.com>
#

cmake_minimum_required (VERSION 2.6)
project ("dvblastpp")

## configurable stuff
set (HAVE_CLDEBUG       true)
set (HAVE_CLLINUX       true)
set (HAVE_CLMACOS       false)
set (HAVE_CLFBSD        false)
set (HAVE_CLDVBHW       true)
set (HAVE_CLASIHW       true)
set (HAVE_CLICONV       true)

set (incdir_bitstream	"/usr/local/include")
set (incdir_ev		"/usr/local/include")
set (libdir_ev		"/usr/local/lib")
## end of configurable stuff

set (c__flags "-Wall -Wformat-security -Wno-strict-aliasing -O3 -fomit-frame-pointer -g -DVERSION=\\\"3.0\\\" -DVERSION_MAJOR=3 -DVERSION_MINOR=0 -DVERSION_EXTRA=\\\"release\\\"")
set (cxxflags "${c__flags}")

set (CMAKE_C_FLAGS           "${c__flags} ${CMAKE_C_FLAGS}")
set (CMAKE_CXX_FLAGS         "${cxxflags} ${CMAKE_CXX_FLAGS}")
set (CMAKE_C_FLAGS_DEBUG     "-g")
set (CMAKE_CXX_FLAGS_DEBUG   "-g")

if (HAVE_CLLINUX)
   if (NOT DEFINED HAVE_CLDVBHW)
      set (HAVE_CLDVBHW true)
   endif (NOT DEFINED HAVE_CLDVBHW)
   if (NOT DEFINED HAVE_CLASIHW)
      set (HAVE_CLASIHW true)
   endif (NOT DEFINED HAVE_CLASIHW)
else (HAVE_CLLINUX)
   set (HAVE_CLDVBHW false)
   set (HAVE_CLASIHW false)
endif (HAVE_CLLINUX)

set (_cLsrc
   cLcommon.cpp
   cLdvbev.c
   cLdvbcore.cpp
   cLdvbmrtgcnt.cpp
   cLdvboutput.cpp
   cLdvben50221.cpp
   cLdvbdemux.cpp
   cLdvbudp.cpp
)
if (HAVE_CLASIHW)
   set (_cLsrc
      ${_cLsrc}
      cLdvbasi.cpp
   )
endif (HAVE_CLASIHW)
if (HAVE_CLDVBHW)
   set (_cLsrc
      ${_cLsrc}
      cLdvbdevc.c
      cLdvbdev.cpp
   )
endif (HAVE_CLDVBHW)
set (_cLsrc
   ${_cLsrc}
#  cLdvbcomm.cpp
   cLdvbapp.cpp
   dvblastpp.cpp
)

if (HAVE_CLLINUX)
   add_definitions (-DHAVE_CLLINUX)
endif (HAVE_CLLINUX)
if (HAVE_CLMACOS)
   add_definitions (-DHAVE_CLMACOS)
endif (HAVE_CLMACOS)
if (HAVE_CLFBSD)
   add_definitions (-DHAVE_CLFBSD)
endif (HAVE_CLFBSD)
if (HAVE_CLDEBUG)
   add_definitions (-DHAVE_CLDEBUG)
endif (HAVE_CLDEBUG)
if (HAVE_CLDVBHW)
   add_definitions (-DHAVE_CLDVBHW)
endif (HAVE_CLDVBHW)
if (HAVE_CLASIHW)
   add_definitions (-DHAVE_CLASIHW)
endif (HAVE_CLASIHW)
if (HAVE_CLICONV)
  add_definitions (-DHAVE_CLICONV)
endif(HAVE_CLICONV)

include_directories (${PROJECT_SOURCE_DIR})
include_directories (${incdir_bitstream})
include_directories (${incdir_ev})
link_directories (${libdir_ev})
if (HAVE_CLLINUX)
  link_libraries (pthread ev rt) 
else (HAVE_CLLINUX)
  link_libraries (iconv pthread ev) 
endif (HAVE_CLLINUX)

add_executable (
   dvblastpp
   ${_cLsrc}
)

