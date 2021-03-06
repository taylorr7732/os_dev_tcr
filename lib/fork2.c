// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).
    
	// LAB 4: Your code here.

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
    
	// LAB 4: Your code here.
    
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn) {
    
	int r = 0;
    
	// LAB 4: Your code here.
    int perm = vpt[pn]&0xFFF;
    void* addr = (void*)pn*PGSIZE;
    if(perm & PTE_W || perm & PTE_COW){
        perm = (perm & ~PTE_W) | PTE_COW;
    }
    perm = perm & PTE_SYSCALL;
    
	if ((r = sys_page_map(0, addr, envid, addr, perm)) < 0)
		panic("Failed to sys_page_map the child in duppage: %e", r);
    if ((r = sys_page_map(0, addr, 0, addr, perm)) < 0)
		panic("Failed to sys_page_map self in duppage: %e", r);
    
    return r;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child processc
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	envid_t envid;
	int r;
    
	envid = sys_exofork();
	if (envid < 0)
		panic("Failed sys_exofork in fork: %e", envid);
	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
    
	//map all of our writeable pages as COW 
    int i, j;
    for(i=0; i< PDX(UTOP); i++){
        if(vpd[i]&PTE_P){ //if the page table is allocated
            for(j=0; j< NPTENTRIES; j++){
                int pn = i*NPDENTRIES+j;
                if(vpt[pn]&PTE_P && vpt[pn]&PTE_U){
                    duppage(envid, pn);
                }
            }
        }
    }
    
    //set up the child's User Exception Stack
    sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), (PTE_W|PTE_P|PTE_U));
    
    //set the child's upcall to the same as ours
    sys_env_set_pgfault_upcall(id, thisenv->env_pgfault_upcall);             
    
	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("Failed sys_env_set_status in fork: %e", r);
    
	return envid;

}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
