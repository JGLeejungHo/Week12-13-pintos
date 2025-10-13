/* After fork, the child process will read and close the opened file
   and the parent will access the closed file. */

#include <string.h>
#include <syscall.h>
#include "tests/userprog/boundary.h"
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  pid_t pid;
  int handle;
  int byte_cnt;
  char *buffer;

  CHECK ((handle = open ("sample.txt")) > 1, "open \"sample.txt\"");
  buffer = get_boundary_area () - sizeof sample / 2;
  byte_cnt = read (handle, buffer, 20);
  
  // DEBUG: fork 직전 부모의 파일 오프셋 확인
  // msg ("DEBUG: Parent before fork, file offset is %d", tell (handle));

  if ((pid = fork("child"))){
    wait (pid);

    // DEBUG: 자식 프로세스가 종료된 후, 부모의 파일 오프셋 확인 (가장 중요!)
    // msg ("DEBUG: Parent after wait, file offset is %d", tell (handle));

    byte_cnt = read (handle, buffer + 20, sizeof sample - 21);

    // DEBUG: 부모가 read를 시도한 후, 실제로 읽은 바이트 수 확인
    // msg ("DEBUG: Parent after read, byte_cnt is %d", byte_cnt);

    if (byte_cnt != sizeof sample - 21)
      fail ("read() returned %d instead of %zu", byte_cnt, sizeof sample - 21);
    else if (strcmp (sample, buffer)) {
        msg ("expected text:\n%s", sample);
        msg ("text actually read:\n%s", buffer);
        fail ("expected text differs from actual");
    } else {
      msg ("Parent success");
    }

    close(handle);
  } else {
    msg ("child run");

    // DEBUG: 자식 프로세스 시작 시점의 파일 오프셋 확인
    // msg ("DEBUG: Child at start, file offset is %d", tell (handle));

    byte_cnt = read (handle, buffer + 20, sizeof sample - 21);

    // DEBUG: 자식이 read를 실행한 후, 실제로 읽은 바이트 수와 오프셋 확인
    // msg ("DEBUG: Child after read, byte_cnt is %d", byte_cnt);
    // msg ("DEBUG: Child after read, file offset is %d", tell (handle));


    if (byte_cnt != sizeof sample - 21)
      fail ("read() returned %d instead of %zu", byte_cnt, sizeof sample - 21);
    else if (strcmp (sample, buffer))
      {
        msg ("expected text:\n%s", sample);
        msg ("text actually read:\n%s", buffer);
        fail ("expected text differs from actual");
      }

    char magic_sentence[17] = "pintos is funny!";
    memcpy(buffer, magic_sentence, 17);

    msg ("Child: %s", buffer);
    close(handle);
  }
}