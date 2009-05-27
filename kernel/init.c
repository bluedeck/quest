#include "multiboot.h"
#include "i386.h"
#include "kernel.h"
#include "filesys.h"
#include "smp.h"

extern descriptor idt[];

extern unsigned _readwrite_pages, _readonly_pages, _bootstrap_pages;
extern unsigned _kernelstart, _physicalkernelstart;

extern void initialise_sound(void);

/* We use this function to create a dummy TSS so that when we issue a
   switch_to/jmp_gate e.g. at the end of init() or __exit(), we have a
   valid previous TSS for the processor to store state info */
static unsigned short CreateDummyTSS( void ) {

  int i;
  descriptor *ad = (idt + 256); /* Get address of GDT from IDT address */

  /* Search 2KB GDT for first free entry */
    for( i = 1; i < 256; i++ )
	if( !( ad[ i ].fPresent ) )
	    break;

    if( i == 256 )
	panic( "No free selector for TSS" );

    ad[ i ].uLimit0 = sizeof( tss );
    ad[ i ].uLimit1 = 0;
    ad[ i ].pBase0 = (unsigned long)&dummyTSS & 0xFFFF;
    ad[ i ].pBase1 = ( (unsigned long)&dummyTSS >> 16 ) & 0xFF;
    ad[ i ].pBase2 = (unsigned long)&dummyTSS >> 24;
    ad[ i ].uType = 0x09;
    ad[ i ].uDPL = 0;		/* Only let kernel perform task-switching */
    ad[ i ].fPresent = 1;
    ad[ i ].f0 = 0;
    ad[ i ].fX = 0;
    ad[ i ].fGranularity = 0;	/* Set granularity of tss in bytes */
    
    return i << 3;

}



/* Allocate a basic TSS */
static unsigned short AllocTSS( void *pPageDirectory, void *pEntry, 
				int mod_num ) {

    int i;
    descriptor *ad = (idt + 256); /* Get address of GDT from IDT address */
    tss *pTSS = (tss *)ul_tss[mod_num];

    /* Search 2KB GDT for first free entry */
    for( i = 1; i < 256; i++ )
	if( !( ad[ i ].fPresent ) )
	    break;

    if( i == 256 )
	panic( "No free selector for TSS" );

    ad[ i ].uLimit0 = sizeof( ul_tss[mod_num] ) - 1;
    ad[ i ].uLimit1 = 0;
    ad[ i ].pBase0 = (unsigned long) pTSS & 0xFFFF;
    ad[ i ].pBase1 = ( (unsigned long) pTSS >> 16 ) & 0xFF;
    ad[ i ].pBase2 = (unsigned long) pTSS >> 24;
    ad[ i ].uType = 0x09;	/* 32-bit tss */
    ad[ i ].uDPL = 0;		/* Only let kernel perform task-switching */
    ad[ i ].fPresent = 1;
    ad[ i ].f0 = 0;
    ad[ i ].fX = 0;
    ad[ i ].fGranularity = 0;	/* Set granularity of tss in bytes */

    pTSS->pCR3 = pPageDirectory;
    pTSS->ulEIP = (unsigned long) pEntry;

    if( mod_num != 1) 
      pTSS->ulEFlags = F_1 | F_IF | F_IOPL0; 
    else
      pTSS->ulEFlags = F_1 | F_IF | F_IOPL; /* Give terminal server access to
					     * screen memory */

    pTSS->ulESP = 0x400000 - 100;
    pTSS->ulEBP = 0x400000 - 100;
    pTSS->usES = 0x23;
    pTSS->usCS = 0x1B;		
    pTSS->usSS = 0x23;		
    pTSS->usDS = 0x23;
    pTSS->usFS = 0x23;
    pTSS->usGS = 0x23;
    pTSS->usIOMap = 0xFFFF;
    pTSS->usSS0 = 0x10;
    pTSS->ulESP0 = (unsigned)KERN_STK + 0x1000;

    /* Return the index into the GDT for the segment */
    return i << 3;
}


/* Create an address space for boot modules */
static unsigned short LoadModule( multiboot_module *pmm, int mod_num ) {

  unsigned long *plPageDirectory = get_phys_addr( pg_dir[mod_num] );
  unsigned long *plPageTable = get_phys_addr( pg_table[mod_num] );
  void *pStack = get_phys_addr( ul_stack[mod_num] );
  Elf32_Ehdr *pe = pmm->pe;
  Elf32_Phdr *pph = (void *) pmm->pe + pe->e_phoff;
  void *pEntry = (void *) pe->e_entry;
  int i, c, j;
  unsigned long *stack_virt_addr;

  /* Populate ring 3 page directory with kernel mappings */
  memcpy( &plPageDirectory[1023], (void *)(((unsigned)get_pdbr())+4092), 4 );

  /* Populate ring 3 page directory with entries for its private address
     space */
  plPageDirectory[0] = (unsigned long) plPageTable | 7;

  plPageDirectory[1022] = (unsigned long)get_phys_addr( kls_pg_table[mod_num] ) | 3;
  kls_pg_table[mod_num][0] = (unsigned long)get_phys_addr( kl_stack[mod_num] ) | 3; 

  /* Walk ELF header */
  for( i = 0; i < pe->e_phnum; i++ ) {

    if( pph->p_type == PT_LOAD ) {
      /* map pages loaded from file */
      c = ( pph->p_filesz + 0xFFF ) >> 12; /* #pages to load for module */

      for( j = 0; j < c; j++ )
	plPageTable[ ( (unsigned long) pph->p_vaddr >> 12 ) + j ] =
	  (unsigned long) pmm->pe + ( pph->p_offset & 0xFFFFF000 ) +
	  ( j << 12 ) + 7;

      /* zero remainder of final page */
      memset( (void *) pmm->pe + pph->p_offset + pph->p_filesz,
	      0, ( pph->p_memsz - pph->p_filesz ) & 0x0FFF );

      /* map additional zeroed pages */
      c = ( pph->p_memsz + 0xFFF ) >> 12;

      /* Allocate space for bss section.  Use temporary virtual memory for
       * memset call to clear physical frame(s)
       */
      for( ; j <= c; j++ ) {
	unsigned long page_frame = (unsigned long) AllocatePhysicalPage();
	void *virt_addr = MapVirtualPage( page_frame | 3 );
	memset( virt_addr, 0, 0x1000 );
	plPageTable[ ( (unsigned long) pph->p_vaddr >> 12 ) + j ] =
	  page_frame + 7;
	 UnmapVirtualPage( virt_addr );
      }
    } 
	
    pph = (void *) pph + pe->e_phentsize;
  }

  /* map stack */
  plPageTable[1023] = (unsigned long) pStack | 7;

    /* --??-- 
     *
     * Special case for tss[1] acting, for now, as our screen/terminal server:
     * Need to map screen memory. Here we map it in the middle of our 
     * page table
     *
     */
  if (mod_num == 1) 
    plPageTable[512] = (unsigned long) 0x000B8000 | 7;

  stack_virt_addr = MapVirtualPage( (unsigned long)pStack | 3 );
  
  /* This sets up the module's stack with command-line args from grub */
  memcpy( (char *)stack_virt_addr + 0x1000 - 80, pmm->string, 80 );
  *(unsigned *)((char *)stack_virt_addr + 0x1000 - 84) = 0; /* argv[1] */
  *(unsigned *)((char *)stack_virt_addr + 0x1000 - 88) = 0x400000 - 80; /* argv[0] */
  *(unsigned *)((char *)stack_virt_addr + 0x1000 - 92) = 0x400000 - 88; /* argv */
  *(unsigned *)((char *)stack_virt_addr + 0x1000 - 96) = 1; /* argc -- hard-coded right now */
  /* Dummy return address placed here for the simulated "call" to our
     library */
  *(unsigned *)((char *)stack_virt_addr + 0x1000 - 100) = 0; /* NULL return address -- never used */
  
  UnmapVirtualPage( stack_virt_addr );
    
  return AllocTSS( plPageDirectory, pEntry, mod_num );
}


/* Programmable interrupt controller settings */
void initialise_pic( void ) {
  
  /* Remap IRQs to int 0x20-0x2F 
   * Need to set initialization command words (ICWs) and
   * operation command words (OCWs) to PIC master/slave
   */

  /* Master PIC */
  outb( 0x11, 0x20 );		/* 8259 (ICW1) - xxx10x01 */
  outb( 0x20, 0x21 );		/* 8259 (ICW2) - set IRQ0... to int 0x20... */
  outb( 0x04, 0x21 );		/* 8259 (ICW3) - connect IRQ2 to slave 8259 */
  outb( 0x0D, 0x21 );		/* 8259 (ICW4) - Buffered master, normal EOI, 8086 mode */

  /* Slave PIC */
  outb( 0x11, 0xA0 );		/* 8259 (ICW1) - xxx10x01 */
  outb( 0x28, 0xA1 );		/* 8259 (ICW2) - set IRQ8...to int 0x28... */
  outb( 0x02, 0xA1 );		/* 8259 (ICW3) - slave ID #2 */
  outb( 0x09, 0xA1 );		/* 8259 (ICW4) - Buffered slave, normal EOI, 8086 mode */

  outb( 0xDE, 0x21 );		/* 8259 (OCW1/IMR master) - enable only 
				   IRQ0/timer for now and IRQ5/hard-coded 
				   soundcard --??-- in future, need to probe
				   for soundcard IRQ */
  outb( 0xFF, 0xA1 );		/* 8259 (OCW1/IMR slave) - mask IRQs in slave PIC for now */
  
}


/* Programmable interval timer settings */
void initialise_pit ( void ) {

  outb( 0x34, 0x43 );		/* 8254 (control word) - counter 0, mode 2 */

  /* Set interval timer to interrupt once every 1/HZth second */
  outb( ( PIT_FREQ / HZ ) & 0xFF, 0x40 ); /* counter 0 low byte */
  outb( ( PIT_FREQ / HZ ) >> 8, 0x40 ); /* counter 0 high byte */
}


void init( multiboot* pmb ) {

  int i, j, k, c, num_cpus;
  unsigned short tss[NR_MODS];
  memory_map_t *mmap;
  unsigned long limit; 
  Elf32_Phdr *pph;
  Elf32_Ehdr *pe;
    
  /* clear screen */
  for( i = 0; i < 80 * 25; i++ ) {
	pchVideo[ i * 2 ] = ' ';
	pchVideo[ i * 2 + 1 ] = 7;
  }

  for (mmap = (memory_map_t *) pmb->mmap_addr;
       (unsigned long) mmap < pmb->mmap_addr + pmb->mmap_length;
       mmap = (memory_map_t *) ((unsigned long) mmap
				+ mmap->size + 4 /*sizeof (mmap->size)*/)) {
    
    /* 
     * Set mm_table bitmap entries to 1 for all pages of RAM that are free. 
     */
    if (mmap->type == 1) {	/* Available RAM -- see 'info multiboot' */
      for (i = 0; i < (mmap->length_low >> 12); i++)
	BITMAP_SET(mm_table,(mmap->base_addr_low >> 12)+i);
      limit = (mmap->base_addr_low >> 12)+i;
      
      if (limit > mm_limit)
	mm_limit = limit;
    }
  }

  /* 
   * Clear bitmap entries for kernel and bootstrap memory areas,
   * so as not to use them in dynamic memory allocation. 
   */
  for (i = 0; i < (unsigned)&_bootstrap_pages + (unsigned)&_readwrite_pages + 
	 (unsigned)&_readonly_pages; i++)
    BITMAP_CLR(mm_table,256+i);	/* Kernel starts at 256th physical frame
				 * See quest.ld
				 */

  /* 
   * --??-- Possible optimization is to free mm_table entries corresponding
   * to memory above mm_limit on machines with less physical memory than 
   * table keeps track of -- currently 4GB. 
   */

  /* Here, clear mm_table entries for any loadable modules. */
  for( i = 0; i < pmb->mods_count; i++ ) {
    
    pe = pmb->mods_addr[i].pe;

    pph = (void *) pe + pe->e_phoff;
    
     for( j = 0; j < pe->e_phnum; j++ ) {
       
       if( pph->p_type == PT_LOAD ) {
	 c = ( pph->p_filesz + 0xFFF ) >> 12; /* #pages required for module */
	 
	 for( k = 0; k < c; k++ )
	   BITMAP_CLR(mm_table, (((unsigned long) pe + pph->p_offset) >> 12) + k);
       }
       pph = (void *) pph + pe->e_phentsize;
     }
  }

#if 0
  /* Test that mm_table is setup correct */
  for (i = 0; i < 2000; i++) 
    putchar (BITMAP_TST(mm_table,i) ? '1' : '0');
  while (1);
#endif

  /* Now safe to call AllocatePhysicalPage() as all free/allocated memory is 
   *  marked in the mm_table 
   */

  /* Start up other processors, which may allocate pages for stacks */
  num_cpus = smp_init(); 
  if (num_cpus > 1) {
    print("Multi-processing detected.  Number of CPUs: ");
    putx(num_cpus);
    putchar('\n');
  } else {
    print("Uni-processor mode.\n");
  }

  /* Initialise the programmable interrupt controller (PIC) */
  initialise_pic ();

  /* Initialise the programmable interval timer (PIT) */
  initialise_pit ();

  if( !pmb->mods_count )
    panic( "No modules available" );

  for( i = 0; i < pmb->mods_count; i++ ) {
    tss[i] = LoadModule( pmb->mods_addr + i, i);
    LookupTSS( tss[i] )->priority = MIN_PRIO;
  }

#if 0
  /* --??-- Test reading the MBR in LBA mode 
     Signature (last two bytes) should be 0x55AA */
  {
    extern void ReadSectorLBA( void *offset, unsigned long lba );
    char buf[512];
    
    ReadSectorLBA ( buf, 0 );

    putx( buf[510] );
    putx( buf[511] );
  }
#endif

  /* Mount root filesystem */
  if ( !ext2fs_mount() ) 
    panic( "Filesystem mount failed" );

  /* Initialise soundcard, if one exists */
  initialise_sound ();

#if 0
  /* --??-- To be removed, just testing the disk IO in CHS mode */
  {
    extern void ReadSector( void *offset, int cylinder, int head, int sector );
    char buf[512];
    
    ReadSector( buf, 0, 0, 1 );

    putx( buf[510] );
    putx( buf[511] );

    ext2fs_dir( "/boot/grub/grub.conf" );
    ext2fs_read( buf, 119 );

    for( i = 0; i < 117; i++)
      putchar( buf[i] );

    /* while (1); */
  }
#endif

  dummyTSS_selector = CreateDummyTSS();

  ltr( dummyTSS_selector );

  runqueue_append( LookupTSS( tss[ 0 ] )->priority, tss[ 0 ] );	/* Shell module */
  
  schedule();
}