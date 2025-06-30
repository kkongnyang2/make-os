#include "kernel/types.h"
#include "user/user.h"   // write, exit 프로토타입

int
main(int argc, char *argv[])
{
  static const char msg[] = "hi,kkongnyang2\n";

  /* fd 1 = 표준출력 */
  write(1, msg, sizeof(msg) - 1);   // '\0' 제외 길이

  exit(0);
  return 0;                         // 도달하지 않지만 ANSI 경고 방지
}
