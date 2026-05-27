#define PAGE_OFFSET   0xffffffc000000000UL

#define USER_CODE_VA  0x0000000000000000UL
#define USER_STACK_VA 0x0000003ffff00000UL
#define USER_SP_VA    0x0000004000000000UL
#define USER_SIG_STACK_VA 0x0000003000000000UL
// 尋找一塊夠大的連續虛擬記憶體 (由一個 Base Address 往上找)
// 定義一個不會跟原本 Code (0x0) 或 Stack (0x3ffffff000) 衝突的起始位址
#define MMAP_BASE     0x0000001000000000UL

/* Memory map */
#define PAGE_SIZE     (1UL << 12) 
#define PMD_SIZE      (1UL << 21)
#define PGD_SIZE      (1UL << 30)

/* VA bit-field shifts (Sv39) */
#define PGD_SHIFT     30 
#define PMD_SHIFT     21
#define PTE_SHIFT     12

#define ENTRIES_PER_TABLE  512

#define KERNEL_PGD_INDEX   ((PAGE_OFFSET >> PGD_SHIFT) & 0x1FF)

#define LINEAR_MAP_GIB     4

/* PTE descriptor bits (Sv39) */
#define PTE_V  (1UL << 0)  
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_G  (1UL << 5)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)
// 🌟 定義 COW 的標籤 (通常使用 PTE 結構中保留給 OS 軟體使用的 bit)
// 在 RISC-V 中，bit 8 (RSW) 很適合拿來標記 COW
#define PTE_COW (1 << 8)

#define PROT_KERNEL  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PROT_DEVICE  (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define PROT_USER_BASE (PTE_V | PTE_U | PTE_A | PTE_D)
#define PROT_USER_RX   (PROT_USER_BASE | PTE_R | PTE_X)
#define PROT_USER_RW   (PROT_USER_BASE | PTE_R | PTE_W)
#define PROT_USER_RWX  (PROT_USER_BASE | PTE_R | PTE_W | PTE_X)

// PROT flags 定義
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_ANONYMOUS 0x20
#define MAP_POPULATE  0x8000

#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))
#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))

#define phys_to_virt(pa)    (pa + PAGE_OFFSET)
#define virt_to_phys(va)    (va - PAGE_OFFSET)

void setup_vm();

void drop_identity_map();

void map_pages(unsigned long *user_pgd, unsigned long va, unsigned long size, unsigned long pa, unsigned long prot);

unsigned long* get_pte(unsigned long *pgd, unsigned long va);
