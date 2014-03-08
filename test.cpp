#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>

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
  while(v3d[V3D_CT0CS] & 0x20);

  // print the status again.
  printf("V3D_CT0CS: 0x%08x, Address: 0x%08x\n", v3d[V3D_CT0CS], v3d[V3D_CT0CA]);

// Release memory;
  unmapmem((void *) list, 0x100);
  mem_unlock(mbox, handle);
  mem_free(mbox, handle);
}

void addbyte(uint8_t **list, uint8_t d) {
  *((*list)++) = d;
}

void addshort(uint8_t **list, uint16_t d) {
  *((*list)++) = (d) & 0xff;
  *((*list)++) = (d >> 8)  & 0xff;
}

void addword(uint8_t **list, uint32_t d) {
  *((*list)++) = (d) & 0xff;
  *((*list)++) = (d >> 8)  & 0xff;
  *((*list)++) = (d >> 16) & 0xff;
  *((*list)++) = (d >> 24) & 0xff;
}

void addfloat(uint8_t **list, float f) {
  uint32_t d = *((uint32_t *)&f);
  *((*list)++) = (d) & 0xff;
  *((*list)++) = (d >> 8)  & 0xff;
  *((*list)++) = (d >> 16) & 0xff;
  *((*list)++) = (d >> 24) & 0xff;
}


void testBinner() {
// Like above, we allocate/lock/map some videocore memory
  // I'm just shoving everything in a single buffer because I'm lazy
  // 8Mb, 4k alignment
  unsigned int handle = mem_alloc(mbox, 0x800000, 0x1000, MEM_FLAG_COHERENT | MEM_FLAG_ZERO);
  if (!handle) {
    printf("Error: Unable to allocate memory");
    return;
  }
  uint32_t bus_addr = mem_lock(mbox, handle); 
  uint8_t *list = (uint8_t*) mapmem(bus_addr, 0x800000);

  uint8_t *p = list;

// Configuration stuff
  // Tile Binning Configuration.
  //   Tile state data is 48 bytes per tile, I think it can be thrown away
  //   as soon as binning is finished.
  addbyte(&p, 112);
  addword(&p, bus_addr + 0x6200); // tile allocation memory address
  addword(&p, 0x8000); // tile allocation memory size
  addword(&p, bus_addr + 0x100); // Tile state data address
  addbyte(&p, 30); // 1920/64
  addbyte(&p, 17); // 1080/64 (16.875)
  addbyte(&p, 0x04); // config

  // Start tile binning.
  addbyte(&p, 6);

  // Primitive type
  addbyte(&p, 56);
  addbyte(&p, 0x32); // 16 bit triangle

  // Clip Window
  addbyte(&p, 102);
  addshort(&p, 0);
  addshort(&p, 0);
  addshort(&p, 1920); // width
  addshort(&p, 1080); // height

  // State
  addbyte(&p, 96);
  addbyte(&p, 0x03); // enable both foward and back facing polygons
  addbyte(&p, 0x00); // depth testing disabled
  addbyte(&p, 0x02); // enable early depth write

  // Viewport offset
  addbyte(&p, 103);
  addshort(&p, 0);
  addshort(&p, 0);

// The triangle
  // No Vertex Shader state (takes pre-transformed vertexes, 
  // so we don't have to supply a working coordinate shader to test the binner.
  addbyte(&p, 65);
  addword(&p, bus_addr + 0x80); // Shader Record

  // primitive index list
  addbyte(&p, 32);
  addbyte(&p, 0x04); // 8bit index, trinagles
  addword(&p, 3); // Length
  addword(&p, bus_addr + 0x70); // address
  addword(&p, 2); // Maximum index

// End of bin list
  // Flush
  addbyte(&p, 5);
  // Nop
  addbyte(&p, 1);
  // Halt
  addbyte(&p, 0);

  int length = p - list;
  assert(length < 0x80);

// Shader Record
  p = list + 0x80;
  addbyte(&p, 0x01); // flags
  addbyte(&p, 6*4); // stride
  addbyte(&p, 0xcc); // num uniforms (not used)
  addbyte(&p, 0); // num varyings
  addword(&p, bus_addr + 0xfe00); // Fragment shader code
  addword(&p, bus_addr + 0xff00); // Fragment shader uniforms
  addword(&p, bus_addr + 0xa0); // Vertex Data

// Vertex Data
  p = list + 0xa0;
  // Vertex: Top, red
  addshort(&p, (1920/2) << 4); // X in 12.4 fixed point
  addshort(&p, 200 << 4); // Y in 12.4 fixed point
  addfloat(&p, 1.0f); // Z
  addfloat(&p, 1.0f); // 1/W
  addfloat(&p, 1.0f); // Varying 0 (Red)
  addfloat(&p, 0.0f); // Varying 1 (Green)
  addfloat(&p, 0.0f); // Varying 2 (Blue)

  // Vertex: bottom left, Green
  addshort(&p, 560 << 4); // X in 12.4 fixed point
  addshort(&p, 800 << 4); // Y in 12.4 fixed point
  addfloat(&p, 1.0f); // Z
  addfloat(&p, 1.0f); // 1/W
  addfloat(&p, 0.0f); // Varying 0 (Red)
  addfloat(&p, 1.0f); // Varying 1 (Green)
  addfloat(&p, 0.0f); // Varying 2 (Blue)

  // Vertex: bottom right, Blue
  addshort(&p, 1460 << 4); // X in 12.4 fixed point
  addshort(&p, 800 << 4); // Y in 12.4 fixed point
  addfloat(&p, 1.0f); // Z
  addfloat(&p, 1.0f); // 1/W
  addfloat(&p, 0.0f); // Varying 0 (Red)
  addfloat(&p, 0.0f); // Varying 1 (Green)
  addfloat(&p, 1.0f); // Varying 2 (Blue)

// Vertex list
  p = list + 0x70;
  addbyte(&p, 0); // top
  addbyte(&p, 1); // bottom left
  addbyte(&p, 2); // bottom right

// fragment shader
  p = list + 0xfe00;
  addword(&p, 0xffffffff);
  addword(&p, 0xe0020ba7); /* ldi tlbc, 0xffffffff */
  addword(&p, 0x009e7000);
  addword(&p, 0x500009e7); /* nop; nop; sbdone */
  addword(&p, 0x009e7000);
  addword(&p, 0x300009e7); /* nop; nop; thrend */
  addword(&p, 0x009e7000);
  addword(&p, 0x100009e7); /* nop; nop; nop */
  addword(&p, 0x009e7000);
  addword(&p, 0x100009e7); /* nop; nop; nop */

// Render control list
  p = list + 0xe200;

  // Clear color
  addbyte(&p, 114);
  addword(&p, 0);
  addword(&p, 0);
  addword(&p, 0);
  addbyte(&p, 0);

  // Tile Rendering Mode Configuration
  addbyte(&p, 113);
  addword(&p, bus_addr + 0x10000); // framebuffer addresss
  addshort(&p, 1920); // width
  addshort(&p, 1080); // height
  addbyte(&p, 0x04); // framebuffer mode (linear rgba8888)
  addbyte(&p, 0x00);

  // Do a store of the first tile to force the tile buffer to be cleared
  // Tile Coordinates
  addbyte(&p, 115);
  addbyte(&p, 0);
  addbyte(&p, 0);
  // Store Tile Buffer General
  addbyte(&p, 28);
  addshort(&p, 0); // Store nothing (just clear)
  addword(&p, 0); // no address is needed

  // Link all binned lists together
  for(int x = 0; x < 30; x++) {
    for(int y = 0; y < 17; y++) {

      // Tile Coordinates
      addbyte(&p, 115);
      addbyte(&p, x);
      addbyte(&p, y);
      
      // Call Tile sublist
      addbyte(&p, 17);
      addword(&p, bus_addr + 0x6200 + (y * 30 + x) * 32);

      // Last tile needs a special store instruction
      if(x == 29 && y == 16) {
        // Store resolved tile color buffer and signal end of frame
        addbyte(&p, 25);
      } else {
        // Store resolved tile color buffer
        addbyte(&p, 24);
      }
    }
  }

  int render_length = p - (list + 0xe200);


// Run our control list
  printf("Binner control list constructed\n");
  printf("Start Address: 0x%08x, length: 0x%x\n", bus_addr, length);

  v3d[V3D_CT0CA] = bus_addr;
  v3d[V3D_CT0EA] = bus_addr + length;
  printf("V3D_CT0CS: 0x%08x, Address: 0x%08x\n", v3d[V3D_CT0CS], v3d[V3D_CT0CA]);

  // Wait for control list to execute
  while(v3d[V3D_CT0CS] & 0x20);
  
  printf("V3D_CT0CS: 0x%08x, Address: 0x%08x\n", v3d[V3D_CT0CS], v3d[V3D_CT0CA]);
  printf("V3D_CT1CS: 0x%08x, Address: 0x%08x\n", v3d[V3D_CT1CS], v3d[V3D_CT1CA]);


  v3d[V3D_CT1CA] = bus_addr + 0xe200;
  v3d[V3D_CT1EA] = bus_addr + 0xe200 + render_length;

  sleep(1);
  //while(v3d[V3D_CT1CS] & 0x20);
  
  printf("V3D_CT1CS: 0x%08x, Address: 0x%08x\n", v3d[V3D_CT1CS], v3d[V3D_CT1CA]);
  v3d[V3D_CT1CS] = 0x20;

  // just dump the buffer to a file
  FILE *f = fopen("binner_dump.bin", "w");
  for(int i = 0; i < 0x800000; i++) {
    fputc(list[i], f);
  }
  fclose(f);
  printf("Buffer containing binned tile lists dumpped to binner_dump.bin\n");

// Release resources
  unmapmem((void *) list, 0x800000);
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
  testBinner();

  return 0;
}





