// Segments in proc->gdt.
#define NSEGS     7
#define PAGE_USED	  1
#define PAGE_UNUSED	  0


#define LAST_TICK_ACS (1 << 31)
#define TOUCHED(var) (var >> 1 | LAST_TICK_ACS)
#define UNTOUCHED(var) (var >> 1)

// Per-CPU state
struct cpu {
  uchar id;                    // Local APIC ID; index into cpus[] below
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;        // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  
  // Cpu-local storage variables; see below
  struct cpu *cpu;
  struct proc *proc;           // The currently-running process.
};

extern struct cpu cpus[NCPU];
extern int ncpu;

// Per-CPU variables, holding pointers to the
// current cpu and to the current process.
// The asm suffix tells gcc to use "%gs:0" to refer to cpu
// and "%gs:4" to refer to proc.  seginit sets up the
// %gs segment register so that %gs refers to the memory
// holding those two variables in the local cpu's struct cpu.
// This is similar to how thread-local variables are implemented
// in thread libraries such as Linux pthreads.
extern struct cpu *cpu asm("%gs:0");       // &cpus[cpunum()]
extern struct proc *proc asm("%gs:4");     // cpus[cpunum()].proc

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

typedef void (*sighandler_t)(void);


// A virtual address 'la' has a three-part structure as follows:
//
// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |      Index     |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(va) --/ \--- PTX(va) --/ 

struct pageInfo {
	uint pdx;
	uint ptx;
	uint aCounter;  //only for nfu
	int isUsed;
	
	uint address; // virtual address = a
	char* kaddress; //kernel virtual address = mem
}; 

struct metaFIFO{
	int pages[MAX_PAGES_IN_FILE()];
	int counter;
};

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  volatile int pid;            // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  
  int pageFaultCount;
  int totalPagedOutCount;
  
  int pagesInFileCount;				// Number of pages in file
  int pagesInMemCount;				// Number of pages in memory
  
  struct pageInfo pagesInMemory[MAX_PSYC_PAGES];			// pages in memory
  struct pageInfo pagesInPageFile[MAX_PAGES_IN_FILE()];			// pages in page file 1 2 3 _ 5 6 7
  
  
  struct metaFIFO metaDataFIFO;			//for FIFO
  
  int pageFile;				// points to a file holding the pages
  char pageFileName[8];
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
char * createPageFileName(struct proc *);

int open(char*, int);
int write(int, char*, int);
int read(int, char*, int);
int close(int);
int lseekPage(int, uint);


int copyPageFile(int, int);


void insertToMetaData(int);
int removeFromMetaData(void);
int getPageToRemove(void);
void initMetaData(struct proc *);

void swapPages(uint a);
void updateNFUData(struct proc *);
void updateAll(void);
