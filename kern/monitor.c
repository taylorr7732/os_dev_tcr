// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/mmu.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display backtrace of called functions", mon_backtrace },
    { "alloc_page", "Allocates a page of physical memory", mon_alloc_page },
    { "page_status", "See if page at given pa is allocated", mon_page_status },
    { "free_page", "Free a page of content", mon_free_page },
    { "top_free_page", "Returns pa of the next free page to be allocated",
        mon_top_free_page},
    {"next",  "Step one step after called breakpoint", mon_next},
    {"continue", "Resume running user program", mon_continue},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Stack backtrace:\n");
	uint32_t * ebp = (uint32_t *) read_ebp();
	while( ebp != 0x0 ) {
		uint32_t eip = *(ebp+1);
		cprintf("  ebp %8x  eip %8x  args %08x %08x %08x %08x %08x\n",
			ebp, eip, *(ebp+2), *(ebp+3), *(ebp+4), *(ebp+5),
			*(ebp+6));
		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);	
		cprintf("         %s:%d: ", info.eip_file, info.eip_line);
		const char * func = info.eip_fn_name;
		int i = 0;
		for(i; i<info.eip_fn_namelen; i++) {
			cprintf("%c", *(func+i));
		}
		cprintf("+%d\n", eip-info.eip_fn_addr);
  		ebp = (uint32_t *) *ebp;
	}

	return 0;
}

int mon_alloc_page(int argc, char **argv,  struct Trapframe *tf) {

  struct Page * new_page = page_alloc(ALLOC_ZERO);
  new_page->pp_ref++;
  cprintf("\t0x%x\n", page2pa(new_page));
  return 0;
}
    
int mon_page_status(int argc, char **argv, struct Trapframe *tf) {

  if(argc != 2) {
    cprintf("Only give pa as arguments!\n");
    return 1;
  } else {
    physaddr_t pa = (physaddr_t) get_hex(argv[1]);
    struct Page * page = pa2page(pa);
    if(page->pp_ref > 0)
      cprintf("Page allocated\n");
    else
      cprintf("Page free\n");
  }
  return 0;
}

int mon_free_page(int argc, char **argv, struct Trapframe *tf) {

  if(argc != 2) {
    cprintf("Only give pa as arguments!\n");
    return 1;
  } else {
    physaddr_t pa = get_hex(argv[1]);
    struct Page * page = pa2page(pa);
    if(page->pp_ref > 0) {
      page->pp_ref--;
      if(page->pp_ref == 0) {
        page_free(page);
      }
    } else {
      cprintf("Page is already free\n");
    }
  }
  return 0;
}

int mon_top_free_page(int argc, char **argv, struct Trapframe *tf) {
  struct Page * page = page_alloc(ALLOC_ZERO);
  cprintf("Next free page: %x\n", page2pa(page));
  page_free(page);
  return 0;
}


int mon_next(int argc, char **argv, struct Trapframe *tf) {
  if(tf->tf_trapno == T_BRKPT || tf->tf_trapno == T_DEBUG) {
    tf->tf_eflags = tf->tf_eflags | (1<<8);

    lcr3(PADDR(curenv->env_pgdir));
    env_pop_tf(tf);
  }
   
  return 0;
}

int mon_continue(int argc, char **argv, struct Trapframe *tf) {
  if(tf->tf_trapno == T_BRKPT || tf->tf_trapno == T_DEBUG) {
    tf->tf_eflags = tf->tf_eflags & ~(1<<8);

    lcr3(PADDR(curenv->env_pgdir));
    env_pop_tf(tf);
  }
   
  return 0;
}


uint32_t get_hex(char * string_hex) {
  if(string_hex[0] != '0' && string_hex[1] != 'x') {
    cprintf("Incorrect format for address\n");
    return 0;
  }
  uint32_t sum = 0;
  size_t i;

  int length = 0;
  //get length
  for( i = 2; string_hex[i] != '\0'; i++) {
    length++;
  }
  for(i = 2; string_hex[i] != '\0'; i++) {
    size_t shift_size = (length-i+1)<<2;
    switch( string_hex[i] ) {
        case '0': sum = sum | (0x0<<shift_size);
            break;
        case '1': sum = sum | (0x1<<shift_size);
            break;
        case '2': sum = sum | (0x2<<shift_size);
            break;
        case '3': sum = sum | (0x3<<shift_size);
            break;
        case '4': sum = sum | (0x4<<shift_size);
            break;
        case '5': sum = sum | (0x5<<shift_size);
            break;
        case '6': sum = sum | (0x6<<shift_size);
            break;
        case '7': sum = sum | (0x7<<shift_size);
            break;
        case '8': sum = sum | (0x8<<shift_size);
            break;
        case '9': sum = sum | (0x9<<shift_size);
            break;
        case 'a': sum = sum | (0xa<<shift_size);
            break;
        case 'b': sum = sum | (0xb<<shift_size);
            break;
        case 'c': sum = sum | (0xc<<shift_size);
            break;
        case 'd': sum = sum | (0xd<<shift_size);
            break;
        case 'e': sum = sum | (0xe<<shift_size);
            break;
        case 'f': sum = sum | (0xf<<shift_size);
            break;
        default: cprintf("Incorrect format for address\n");
            return 0;
    }
  }
  return sum;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);
	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
