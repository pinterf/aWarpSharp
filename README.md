aWarpSharp package 2016.06.24 - a WarpSharpening filter for Avisynth+ and AviSynth 2.6

  Based on awarpsharp2-2015.12.30 build (cretindesalpes mod)
    http://ldesoras.free.fr/src/avs/awarpsharp2-2015.12.30.zip

  with the help of 2012.03.28 x64 build
    http://www.dropbox.com/s/5s6xht0xu80otbz/aWarpSharp_20120328_x64.zip?dl=1

  Usage:
    http://avisynth.nl/index.php/AWarpSharp2

  Addition by pinterf on 20160624: 
  - AviSynth 2.6 interface, Avisynth+ header
  - working x64 version
  - minor cleanup

awarpsharp2-2015.12.30 build:

  Modified from the Firesledge's 2015.01.29 modification of 2012.03.28 version
  and modified again.

  This filter implements the same WarpSharpening algorithm as aWarpSharp by MarcFD,
  but with several bugfixes and optimizations. In addition to complete algorithm
  filter aWarpSharp2 parts of algorithm are also available as aSobel, aBlur, aWarp.

  Requires planar YUV and at least MMXExt capable CPU, optimized for Nehalem, blur
  will be more precise around frame borders if SSSE3 is available.

