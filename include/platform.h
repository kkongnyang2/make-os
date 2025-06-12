#ifndef PLATFORM_H
#define PLATFORM_H

#if defined(PLATFORM_QEMU)
  #include "platform/qemu.h"
#elif defined(PLATFORM_RPI4)
  #include "platform/rpi4.h"
#else
  #error "PLATFORM_QEMU 또는 PLATFORM_RPI4를 지정하세요"
#endif

#endif // PLATFORM_H