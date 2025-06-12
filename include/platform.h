#ifndef PLATFORM_H
#define PLATFORM_H
#if defined(PLATFORM_QEMU)
  #include "platform/qemu.h"
#elif defined(PLATFORM_RPI4)
  #include "platform/rpi4.h"
#else
  #error "PLATFORM not set"
#endif
#endif