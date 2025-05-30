# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/lo/Develop/ESP/v5.4/esp-idf/components/bootloader/subproject"
  "/home/lo/Develop/ESP/wifihostB/bootloader"
  "/home/lo/Develop/ESP/wifihostB/bootloader-prefix"
  "/home/lo/Develop/ESP/wifihostB/bootloader-prefix/tmp"
  "/home/lo/Develop/ESP/wifihostB/bootloader-prefix/src/bootloader-stamp"
  "/home/lo/Develop/ESP/wifihostB/bootloader-prefix/src"
  "/home/lo/Develop/ESP/wifihostB/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/lo/Develop/ESP/wifihostB/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/lo/Develop/ESP/wifihostB/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
