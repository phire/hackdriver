#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mailbox.h"

// I/O access
volatile unsigned *v3d;

int main(int argc, char **argv) {
  int mbox = mbox_open();

  // The blob now has this nice handy call which powers up the v3d pipeline.
  qpu_enable(mbox, 1);

  // map v3d's registers into our address space.
  v3d = (unsigned *) mapmem(0x20c00000, 0x1000);

  if(v3d[0] != 0x02443356) {
    printf("Error: V3D pipeline isn't powered up and accessable.\n");
    exit(-1);
  }

  // We now have access to the v3d registers, we should do something.

  return 0;
}





