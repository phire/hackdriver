#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

#include "mailbox.h"
#include "v3d.h"

// I/O access
volatile unsigned *v3d;
int mbox;

// Execute a nop control list to prove that we have contol.
void testControlLists() {
// First we allocate and map some videocore memory to build our control list in.

  // ask the blob to allocate us 256 bytes with 4k alignment, zero it.
  unsigned int handle = mem_alloc(mbox, 0x100, 0x1000, MEM_FLAG_COHERENT | MEM_FLAG_ZERO);
  if (!handle) {
    printf("Error: Unable to allocate memory");
    return;
  }
  // ask the blob to lock our memory at a single location at give is that address.
  uint32_t bus_addr = mem_lock(mbox, handle); 
  // map that address into our local address space.
  uint8_t *list = (uint8_t*) mapmem(bus_addr, 0x100);

// Now we construct our control list.
  // 255 nops, with a halt somewhere in the middle
  for(int i = 0; i < 0xff; i++) {
    list[i] = 1; // NOP
  }
  list[0xbb] = 0; // Halt.

// And then we setup the v3d pipeline to execute the control list.
  printf("V3D_CT0CS: 0x%08x\n", v3d[V3D_CT0CS]); 
  printf("Start Address: 0x%08x\n", bus_addr);
  // Current Address = start of our list (bus address)
  v3d[V3D_CT0CA] = bus_addr;
  // End Address = just after the end of our list (bus address) 
  // This also starts execution.
  v3d[V3D_CT0EA] = bus_addr + 0x100;
  
  // print status while running
  printf("V3D_CT0CS: 0x%08x, Address: 0x%08x\n", v3d[V3D_CT0CS], v3d[V3D_CT0CA]);

  // Wait a second to be sure the contorl list execution has finished
  sleep(1);
  // print the status again.
  printf("V3D_CT0CS: 0x%08x, Address: 0x%08x\n", v3d[V3D_CT0CS], v3d[V3D_CT0CA]);

// Release memory;
  unmapmem((void *) list, 0x100);
  mem_unlock(mbox, handle);
  mem_free(mbox, handle);
}

int main(int argc, char **argv) {
  mbox = mbox_open();

  // The blob now has this nice handy call which powers up the v3d pipeline.
  qpu_enable(mbox, 1);

  // map v3d's registers into our address space.
  v3d = (unsigned *) mapmem(0x20c00000, 0x1000);

  if(v3d[V3D_IDENT0] != 0x02443356) { // Magic number.
    printf("Error: V3D pipeline isn't powered up and accessable.\n");
    exit(-1);
  }

  // We now have access to the v3d registers, we should do something.
  testControlLists();

  return 0;
}





