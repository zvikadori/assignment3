#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "fcntl.h"



int swapIn(void);

 /* if (proc->pageFile == -1)
		proc->pageFile = open(createPageFileName(proc->pid), O_CREATE | O_RDWR);*/

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()
struct segdesc gdt[NSEGS];

//stores a full page from the address va,  into a file with file discriptor - fd.
int
storeToFile(int fd, void *va, int size, uint page){
	lseekPage(fd, page);
	return write(fd, (char *)va, size);
}

//loads a full page(when using size = PGSIZE) from fd to va.
int
loadFromFile(int fd, void *va, int size, uint page){
	lseekPage(fd, page);
	return read(fd, (char *)va, size);
}


// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpunum()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);

  // Map cpu, and curproc
  c->gdt[SEG_KCPU] = SEG(STA_W, &c->cpu, 8, 0);

  lgdt(c->gdt, sizeof(c->gdt));
  loadgs(SEG_KCPU << 3);
  
  // Initialize cpu-local storage.
  cpu = c;
  proc = 0;
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)p2v(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table 
    // entries, if necessary.
    *pde = v2p(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;
  
  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
// 
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP, 
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
  { (void*) KERNBASE, 0,             EXTMEM,    PTE_W},  // I/O space
  { (void*) KERNLINK, V2P(KERNLINK), V2P(data), 0}, // kernel text+rodata
  { (void*) data,     V2P(data),     PHYSTOP,   PTE_W},  // kernel data, memory
  { (void*) DEVSPACE, DEVSPACE,      0,         PTE_W},  // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm()
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (p2v(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start, 
                (uint)k->phys_start, k->perm) < 0)
      return 0;
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(v2p(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  pushcli();
  cpu->gdt[SEG_TSS] = SEG16(STS_T32A, &cpu->ts, sizeof(cpu->ts)-1, 0);
  cpu->gdt[SEG_TSS].s = 0;
  cpu->ts.ss0 = SEG_KDATA << 3;
  cpu->ts.esp0 = (uint)proc->kstack + KSTACKSIZE;
  ltr(SEG_TSS << 3);
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  lcr3(v2p(p->pgdir));  // switch to new address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;
  
  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, v2p(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, p2v(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
  pte_t *pte;
  int removeFromMemCell;
  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;
    
  a = PGROUNDUP(oldsz);   // a is a VA
  for(; a < newsz; a += PGSIZE){
	mem = kalloc();
	if(mem == 0){
	  cprintf("allocuvm out of memory\n");
	  deallocuvm(pgdir, newsz, oldsz);
	  return 0;
	}
	
	if (proc->pagesInMemCount < 15){
		cprintf("%d , %d... %d\n", a ,mem, proc->pagesInMemCount);
		proc->pagesInMemory[proc->pagesInMemCount].address = a;
		proc->pagesInMemory[proc->pagesInMemCount].kaddress = mem;
		insertToMetaData(proc->pagesInMemCount);
		proc->pagesInMemCount++;
	
	} else {
		//should swap a page into the memory, free that page, 
		removeFromMemCell = swapIn( );
		cprintf("remove frome cell is %d\n", removeFromMemCell);	
		if (removeFromMemCell < 0){
			cprintf("cannot swap, expect errors!!\n");
			return 0;
		}
		//find pte of page to be removed from mem
		pte = walkpgdir(proc->pgdir, (void *)proc->pagesInMemory[removeFromMemCell].address, 0);
		//if (!pte) {
			//cprintf("cannot swap, Page file is FULL!!!, expect errors!\n");
			//return 0;
		//}
		
		//dealloc old page from mem...
		kfree(proc->pagesInMemory[removeFromMemCell].kaddress);
		
		//page
		*pte &= ~PTE_P;
		*pte |= PTE_PG;
		
		//inserting new page to mem arr
		proc->pagesInMemory[removeFromMemCell].address = a;
		proc->pagesInMemory[removeFromMemCell].kaddress = mem;
		proc->pagesInMemory[removeFromMemCell].isUsed = PAGE_USED;
		insertToMetaData(removeFromMemCell);
		
		proc->pagesInFileCount++;
	}
	memset(mem, 0, PGSIZE);
	mappages(pgdir, (char*)a, PGSIZE, v2p(mem), PTE_W|PTE_U);    
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a += (NPTENTRIES - 1) * PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = p2v(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = p2v(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)p2v(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, v2p(mem), PTE_W|PTE_U) < 0)
      goto bad;
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)p2v(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//selects a page needed to be written to the page file
//writes it to the page file
//informs that the page is paged and is not present anymore.
//does not treats meta data! see allocuvm
int swapIn(){
	int removeFromMemCell = removeFromMetaData();
	int i;
	
	for(i = 0; i < MAX_PAGES_IN_FILE(); i++){
		if (proc->pagesInPageFile[i].isUsed == PAGE_UNUSED){
			break;
		}
	}
	
	
	if (i == MAX_PAGES_IN_FILE() ){ //shpuld never be true!!!
		cprintf("cannot swap, Page file is FULL!!!, expect errors!\n");
		return -1;
	}

	//moving page info from mem arr to file arr
	proc->pagesInPageFile[i].isUsed = PAGE_USED;
	proc->pagesInPageFile[i].address = proc->pagesInMemory[removeFromMemCell].address;
	proc->pagesInPageFile[i].kaddress = proc->pagesInMemory[removeFromMemCell].kaddress;
	
	proc->pagesInMemory[removeFromMemCell].isUsed = PAGE_UNUSED;
	lseekPage(proc->pageFile, i);
	write(proc->pageFile, proc->pagesInMemory[removeFromMemCell].kaddress, PGSIZE);
	
	return removeFromMemCell;
	
}

//pfAddress = page fault address(should be rounded!)
int swapPages(uint pfAddress){
	#ifndef NONE
	int removeFromMemCell = swapIn();
	pte_t *pte;
	char * mem;
	//int i;
	uint a;
	
	if (removeFromMemCell < 0){
		cprintf("cannot swap, expect errors!!\n");
		return 0;
	}
	//find pte of page to be removed from mem
	pte = walkpgdir(proc->pgdir, (void *)proc->pagesInMemory[removeFromMemCell].address, 0);
	if (!pte) {
		cprintf("cannot swap, Page file is FULL!!!, expect errors!\n");
		return 0;
	}
	
	//dealloc old page from mem...
	kfree(proc->pagesInMemory[removeFromMemCell].kaddress);
	
	//page
	*pte &= ~PTE_P;
	*pte |= PTE_PG;
	
	mem = kalloc(); 
	if (mem == 0){
		cprintf("cannot bring page from the file");
		return 0;
	}
	
	//lseek
	read(proc->pageFile, mem, PGSIZE);
	a = PGROUNDDOWN(pfAddress); //can be pofKaddress
	//cprintf("a is %d", a);
	pte = walkpgdir(proc->pgdir, (void *) a, 0); //bring the pte of a from memory, we want to edit it for the new mem...
	*pte = v2p(mem) | PTE_W | PTE_U | PTE_P;
	*pte &= ~PTE_PG;
	
	//inserting new page to mem arr
	proc->pagesInMemory[removeFromMemCell].address = a;
	proc->pagesInMemory[removeFromMemCell].kaddress = mem;
	proc->pagesInMemory[removeFromMemCell].isUsed = PAGE_USED;
	insertToMetaData(removeFromMemCell);
	
//	proc->pagesInFileCount++;
	#endif
	return 1;
	
}
