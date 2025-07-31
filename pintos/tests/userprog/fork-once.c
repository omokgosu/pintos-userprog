/* Forks and waits for a single child process. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
  int pid;

  /* 부모 프로세스에는 pid 에 자식의 pid가 반환됨 */
  /* 자식 프로세스는 pid에 0이 반환됨 */
  if ((pid = fork("child"))){
    
    int status = wait (pid);
    msg ("Parent: child exit status is %d", status);
  
  } else {
    
    msg ("child run");
    exit(81);
  
  }
}
