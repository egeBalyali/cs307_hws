#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm_dbg.h"

#define NOPS (16)

#define OPC(i) ((i) >> 12)
#define DR(i) (((i) >> 9) & 0x7)
#define SR1(i) (((i) >> 6) & 0x7)
#define SR2(i) ((i) & 0x7)
#define FIMM(i) ((i >> 5) & 01)
#define IMM(i) ((i) & 0x1F)
#define SEXTIMM(i) sext(IMM(i), 5)
#define FCND(i) (((i) >> 9) & 0x7)
#define POFF(i) sext((i) & 0x3F, 6)
#define POFF9(i) sext((i) & 0x1FF, 9)
#define POFF11(i) sext((i) & 0x7FF, 11)
#define FL(i) (((i) >> 11) & 1)
#define BR(i) (((i) >> 6) & 0x7)
#define TRP(i) ((i) & 0xFF)

/* New OS declarations */

// OS bookkeeping constants
#define PAGE_SIZE       (4096)  // Page size in bytes
#define OS_MEM_SIZE     (2)     // OS Region size. Also the start of the page tables' page
#define Cur_Proc_ID     (0)     // id of the current process
#define Proc_Count      (1)     // total number of processes, including ones that finished executing.
#define OS_STATUS       (2)     // Bit 0 shows whether the PCB list is full or not
#define OS_FREE_BITMAP  (3)     // Bitmap for free pages

// Process list and PCB related constants
#define PCB_SIZE  (3)  // Number of fields in a PCB
#define PID_PCB   (0)  // Holds the pid for a process
#define PC_PCB    (1)  // Value of the program counter for the process
#define PTBR_PCB  (2)  // Page table base register for the process

#define CODE_SIZE       (2)  // Number of pages for the code segment
#define HEAP_INIT_SIZE  (2)  // Number of pages for the heap segment initially

bool running = true;

typedef void (*op_ex_f)(uint16_t i);
typedef void (*trp_ex_f)();

enum { trp_offset = 0x20 };
enum regist { R0 = 0, R1, R2, R3, R4, R5, R6, R7, RPC, RCND, PTBR, RCNT };
enum flags { FP = 1 << 0, FZ = 1 << 1, FN = 1 << 2 };

uint16_t mem[UINT16_MAX] = {0};
uint16_t reg[RCNT] = {0};
uint16_t PC_START = 0x3000;

void initOS();
int createProc(char *fname, char *hname);
void loadProc(uint16_t pid);
uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write);  // Can use 'bool' instead
int freeMem(uint16_t ptr, uint16_t ptbr);
static inline uint16_t mr(uint16_t address);
static inline void mw(uint16_t address, uint16_t val);
static inline void tbrk();
static inline void thalt();
static inline void tyld();
static inline void trap(uint16_t i);

static inline uint16_t sext(uint16_t n, int b) { return ((n >> (b - 1)) & 1) ? (n | (0xFFFF << b)) : n; }
static inline void uf(enum regist r) {
    if (reg[r] == 0)
        reg[RCND] = FZ;
    else if (reg[r] >> 15)
        reg[RCND] = FN;
    else
        reg[RCND] = FP;
}
static inline void add(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] + (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void and(uint16_t i)  { reg[DR(i)] = reg[SR1(i)] & (FIMM(i) ? SEXTIMM(i) : reg[SR2(i)]); uf(DR(i)); }
static inline void ldi(uint16_t i)  { reg[DR(i)] = mr(mr(reg[RPC]+POFF9(i))); uf(DR(i)); }
static inline void not(uint16_t i)  { reg[DR(i)]=~reg[SR1(i)]; uf(DR(i)); }
static inline void br(uint16_t i)   { if (reg[RCND] & FCND(i)) { reg[RPC] += POFF9(i); } }
static inline void jsr(uint16_t i)  { reg[R7] = reg[RPC]; reg[RPC] = (FL(i)) ? reg[RPC] + POFF11(i) : reg[BR(i)]; }
static inline void jmp(uint16_t i)  { reg[RPC] = reg[BR(i)]; }
static inline void ld(uint16_t i)   { reg[DR(i)] = mr(reg[RPC] + POFF9(i)); uf(DR(i)); }
static inline void ldr(uint16_t i)  { reg[DR(i)] = mr(reg[SR1(i)] + POFF(i)); uf(DR(i)); }
static inline void lea(uint16_t i)  { reg[DR(i)] =reg[RPC] + POFF9(i); uf(DR(i)); }
static inline void st(uint16_t i)   { mw(reg[RPC] + POFF9(i), reg[DR(i)]); }
static inline void sti(uint16_t i)  { mw(mr(reg[RPC] + POFF9(i)), reg[DR(i)]); }
static inline void str(uint16_t i)  { mw(reg[SR1(i)] + POFF(i), reg[DR(i)]); }
static inline void rti(uint16_t i)  {} // unused
static inline void res(uint16_t i)  {} // unused
static inline void tgetc()        { reg[R0] = getchar(); }
static inline void tout()         { fprintf(stdout, "%c", (char)reg[R0]); }
static inline void tputs() {
  uint16_t *p = mem + reg[R0];
  while(*p) {
    fprintf(stdout, "%c", (char) *p);
    p++;
  }
}
static inline void tin()      { reg[R0] = getchar(); fprintf(stdout, "%c", reg[R0]); }
static inline void tputsp()   { /* Not Implemented */ }
static inline void tinu16()   { fscanf(stdin, "%hu", &reg[R0]); }
static inline void toutu16()  { fprintf(stdout, "%hu\n", reg[R0]); }

trp_ex_f trp_ex[10] = {tgetc, tout, tputs, tin, tputsp, thalt, tinu16, toutu16, tyld, tbrk};
static inline void trap(uint16_t i) { trp_ex[TRP(i) - trp_offset](); }
op_ex_f op_ex[NOPS] = {/*0*/ br, add, ld, st, jsr, and, ldr, str, rti, not, ldi, sti, jmp, res, lea, trap};

/**
  * Load an image file into memory.
  * @param fname the name of the file to load
  * @param offsets the offsets into memory to load the file
  * @param size the size of the file to load
*/
void ld_img(char *fname, uint16_t *offsets, uint16_t size) {
    FILE *in = fopen(fname, "rb");
    if (NULL == in) {
        fprintf(stderr, "Cannot open file %s.\n", fname);
        exit(1);
    }

    for (uint16_t s = 0; s < size; s += PAGE_SIZE) {
        uint16_t *p = mem + offsets[s / PAGE_SIZE];
        uint16_t writeSize = (size - s) > PAGE_SIZE ? PAGE_SIZE : (size - s);
        fread(p, sizeof(uint16_t), (writeSize), in);
    }
    
    fclose(in);
}

void run(char *code, char *heap) {
  while (running) {
    uint16_t i = mr(reg[RPC]++);
    op_ex[OPC(i)](i);
  }
}

// YOUR CODE STARTS HERE
size_t getFileSize(FILE *file) {
    fseek(file, 0, SEEK_END); // Move to the end of the file
    size_t size = ftell(file); // Get the position (file size)
    rewind(file); // Reset the file pointer to the beginning
    return size / sizeof(uint16_t); // Return size in terms of 16-bit words
}
#define VALID_MASK (0x0001)
#define WRITE_MASK (0x0004)
#define PTE_SHIFT (11)
#define PCB_START (12)
uint16_t translateAddress(uint16_t va);


uint16_t getPCB(uint16_t pid)
{
  uint16_t pcb = PCB_START + pid * PCB_SIZE;
  return pcb;
}
uint16_t getPageTableStart(uint16_t pid)
{
  //they start from 3rd table
  uint16_t _pageTableStart = PAGE_SIZE;
  return (_pageTableStart + pid * 32); 
}
void initOS() {
  mem[Cur_Proc_ID] = 0xFFFF;
  mem[Proc_Count] = 0x0000;
  mem[OS_STATUS] = 0x0000;
  mem[OS_FREE_BITMAP] = 0x1FFF; //first three pages are full
  mem[OS_FREE_BITMAP+1] = 0xFFFF;
  return;
}
// Process functions to implement
int createProc(char *fname, char *hname) {
  if (mem[OS_STATUS] & VALID_MASK){
    //os_stat is not zero, it is full
    printf("The OS memory region is full. Cannot create a new PCB.\n");
    return -1;
  }
  uint16_t pid = mem[Proc_Count];
  mem[Proc_Count] += 1;
  if (mem[Proc_Count] == 1361)
  {
    mem[OS_STATUS] = 0xFFFF;
  }
  uint16_t self_pcb = getPCB(pid);
  mem[self_pcb + PID_PCB] = pid;
  mem[self_pcb + PC_PCB] = 0x3000; //processes start from 0x3000
  mem[self_pcb + PTBR_PCB] = getPageTableStart(pid);
  FILE *code_file = fopen(fname, "r");
  if (!code_file){
      fprintf(stderr, "Cannot read code file: %s\n", fname);
      return -1;
  }
  uint16_t offsets[2];
  for (int i = 0; i < CODE_SIZE; i++){
    uint16_t worked = allocMem(mem[self_pcb + PTBR_PCB], i + 6, 0xFFFF, 0);
    if (worked){
      offsets[i] = (mem[mem[self_pcb + PTBR_PCB] + i + 6] >> PTE_SHIFT) << PTE_SHIFT;
    }
    else{
      printf("Cannot create code segment.\n");
      return -1;
    }
  }
  size_t writ = getFileSize(code_file);
  fclose(code_file);
  ld_img(fname, offsets,writ);
  for (int j = 0; j < HEAP_INIT_SIZE; j++){
    uint16_t worked = allocMem(mem[self_pcb + PTBR_PCB], j + CODE_SIZE + 6, 0xFFFF, 0xFFFF);
    if (worked){
      offsets[j] = (mem[mem[self_pcb + PTBR_PCB] + j + CODE_SIZE + 6] >> PTE_SHIFT) << PTE_SHIFT;
    }
    else{
      printf("Cannot create heap segment.\n");
      return -1;
    }
  }
  FILE *heap_file = fopen(hname, "r");
  writ = getFileSize(heap_file);
  fclose(code_file);
  ld_img(hname, offsets,writ);
  return 1;
}

void loadProc(uint16_t pid) {
  
  mem[Cur_Proc_ID] = pid;

  uint16_t pcb = getPCB(pid);
  reg[RPC] =  mem[pcb + PC_PCB];
  reg[PTBR] = mem[pcb + PTBR_PCB];

}
//for example the first code starts with 0x3000 shifted 11 is 6, it is the first though. VPN should be 5 less than first 5 bits
//pre: ptbr is base register for page table, vpn is the first 5 bit of virt address
uint16_t allocMem(uint16_t ptbr, uint16_t vpn, uint16_t read, uint16_t write) {
    for (uint16_t i = 0; i < 32; i++) {
        uint16_t *bitmap = (i < 16) ? &mem[OS_FREE_BITMAP] : &mem[OS_FREE_BITMAP + 1];
        uint16_t bit = 0x8000 >> (i % 16); // Mask for current page frame

        // Check if the page is free
        if (*bitmap & bit) {
            *bitmap &= ~bit; // Mark page as used in the bitmap

            // Calculate and store the PTE in the page table
            //printf("write is %d", write);
            uint16_t pte = (i << PTE_SHIFT) | ((read == 0xFFFF) << 1) | ((write == 0xFFFF) << 2) | VALID_MASK;
            //printf("new pte is 0x%04X\n", pte);
            mem[ptbr + vpn] = pte; // Write PTE directly at ptbr + vpn
            return 1; // Allocation successful
        }
    }
    fprintf(stderr, "No free page available.\n");
    return 0; // Failure
}


int freeMem(uint16_t vpn, uint16_t ptbr) {
  //printf("FREE MEM İS CALLED \n");
  uint16_t start_of_own_pagetable = ptbr;
  uint16_t pte = mem[start_of_own_pagetable + vpn];
  if ((pte & VALID_MASK) == 0) //not valid place
  {
    return 0;
  }
  else
  {
    mem[start_of_own_pagetable + vpn] -= 0x0001; //make valid bit 0 
    uint16_t phys_frame_num = pte >>PTE_SHIFT;
    if (phys_frame_num < 16)
    {
      mem[OS_FREE_BITMAP] |= (0x8000 >> phys_frame_num);
      return 0;
    }
    else if (phys_frame_num < 32)
    {
      mem[OS_FREE_BITMAP + 1] |= (0x8000 >> (phys_frame_num - 16));
      return 0;
    }
  }
  return 50;
}
uint16_t getPTE(uint16_t va)
{
  uint16_t pcb_address = getPCB(mem[Cur_Proc_ID]);
  uint16_t pte_start = mem[pcb_address + PTBR_PCB];
  uint16_t vpn = va >> PTE_SHIFT; // Extract VPN

  return mem[(pte_start + vpn)];
}
uint16_t translateAddress(uint16_t va) {
    uint16_t pcb_address = getPCB(mem[Cur_Proc_ID]);
    uint16_t pte_start = mem[pcb_address + PTBR_PCB];
    
    uint16_t vpn = va >> PTE_SHIFT; // Extract VPN
    uint16_t offset = va & 0x07FF; // Offset within the page

    //printf("ptbr is %d vpn is ")
    
    //printf("va is %d and pte_start is %d and vpn is %d",va, pte_start, vpn );
    uint16_t pte = pte_start + vpn; 

    if (!(mem[pte] & VALID_MASK)) { // Check if the page is valid
    //printf("vpn is %d\n", vpn);
        fprintf(stderr, "Invalid page table entry for pte: 0x%04X\n", pte); //vpn 0dan başlıyo
        exit(1); // Terminate to prevent further invalid accesses
    }
    pte = mem[pte];
    uint16_t phys_frame = pte >> PTE_SHIFT;
    //printf("translated number is %d \n", ((phys_frame << PTE_SHIFT) + offset));
    return (phys_frame << PTE_SHIFT) + offset;
}
// Instructions to implement
static inline void tbrk() { 
  uint16_t vpn = reg[R0] >> PTE_SHIFT;
  uint16_t pcb_start = getPCB(mem[Cur_Proc_ID]);
  uint16_t pte_start = mem[pcb_start + PTBR_PCB];

  if ((mem[pte_start + vpn] & VALID_MASK) == 0) {
    //not allocated
    printf("Heap increase requested by process %d.\n", mem[Cur_Proc_ID]);
    //printf("regr0 is %d", reg[R0]);
    allocMem(mem[pcb_start + PTBR_PCB], vpn, (reg[R0] & 0x0002 ? UINT16_MAX : 0), (reg[R0] & 0x0004 ? UINT16_MAX : 0));
  }
  else{
    freeMem(vpn, mem[pcb_start + PTBR_PCB]);
  }
}
static inline void tyld() {
  uint16_t pcb_start = getPCB(mem[Cur_Proc_ID]);
  mem[pcb_start + PC_PCB] = reg[RPC];
  mem[pcb_start + PTBR_PCB] = reg[PTBR];
  uint16_t original_start = pcb_start;
  pcb_start += PCB_SIZE; 
  while(pcb_start != original_start)
  {
    if((pcb_start + PID_PCB) == PCB_START && mem[pcb_start + PID_PCB] != 0xFFFF){
      //printf("line331We are switching from process %d to %d.\n",mem[Cur_Proc_ID],mem[pcb_start + PID_PCB] );
      printf("We are switching from process %d to %d.\n",mem[Cur_Proc_ID],mem[pcb_start + PID_PCB] );
      loadProc(mem[pcb_start + PID_PCB]);
      return;
    }
    if (mem[pcb_start + PID_PCB] != 0 && mem[pcb_start + PID_PCB] != 0xFFFF)
    {
      printf("We are switching from process %d to %d.\n",mem[Cur_Proc_ID],mem[pcb_start + PID_PCB] );
      loadProc(mem[pcb_start + PID_PCB]);
      return;
    }
    pcb_start = (pcb_start + PCB_SIZE) % (1360 * PCB_SIZE);
    if (pcb_start == 0)
      pcb_start = PCB_START;
  }
  //printf("came to line 345, it will call itself");
  loadProc(mem[original_start + PID_PCB]);
}
static inline void changeProcAfterHalt() {
  uint16_t pcb_start = getPCB(mem[Cur_Proc_ID]);
  uint16_t original_start = pcb_start;
  pcb_start += PCB_SIZE;
  
  while(pcb_start != original_start)
  {
    //printf("original_start is %d pcb_start is at %d ",original_start,pcb_start);
    if((pcb_start + PID_PCB) == PCB_START && mem[pcb_start + PID_PCB] != 0xFFFF)
    {
      loadProc(mem[pcb_start + PID_PCB]);
      return;
    }
    if (mem[pcb_start + PID_PCB] != 0 && mem[pcb_start + PID_PCB] != 0xFFFF)
    {
      loadProc(mem[pcb_start + PID_PCB]);
      return;
    }
    pcb_start = (pcb_start + PCB_SIZE) % (1360 * PCB_SIZE);
    if (pcb_start == 0)
      pcb_start = PCB_START;
  }
  loadProc(mem[original_start + PID_PCB]);
}
// Instructions to modify
static inline void thalt() {
  uint16_t pcb_start = getPCB(mem[Cur_Proc_ID]);
  mem[pcb_start + PID_PCB] = 0xFFFF;
  uint16_t pte_start = mem[pcb_start + PTBR_PCB];
  //printf("pte location %d and pte is %d\n", (pte_start + i), mem[pte_start + i]);
  for ( int i = 0;i < 32; i++){
    if ((mem[pte_start + i] & VALID_MASK) == 1){
      freeMem(i, mem[(pcb_start + PTBR_PCB)]);
    }
  }
  uint16_t i = PCB_START;
  uint16_t no_process_left = 1;
  if (mem[PCB_START + PID_PCB] != 0xFFFF){
    no_process_left = 0;
  }
  while(no_process_left && (i < (PCB_SIZE * 32 + PCB_START)))
  {
    if (mem[i + PID_PCB] != 0x0000 && mem[i + PID_PCB] != 0xFFFF){
      no_process_left = 0;
      break;
    }
    i += PCB_SIZE;
  }
  if (no_process_left)
  {running = false;}
  else{
    changeProcAfterHalt(); 
  }
}

static inline uint16_t mr(uint16_t address) {
  //printf("mr calls translate with %d", address);
  uint16_t phys_address = translateAddress(address);
  return mem[phys_address];
}

static inline void mw(uint16_t address, uint16_t val) {
  //printf("mw calls translate with %d", address);

  if ((getPTE(address) & WRITE_MASK) == 0)
  {
    printf("Cannot write to a read-only page.\n");
    exit(1);
  }
  uint16_t phys_address = translateAddress(address);
  mem[phys_address] = val;
}

// YOUR CODE ENDS HERE