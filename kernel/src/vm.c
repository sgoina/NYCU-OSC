#include "vm.h"
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

#define PROT_KERNEL  (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PROT_DEVICE  (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)

#define SATP_SV39           (8UL << 60)
#define MAKE_SATP(pgd_pa)   (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))

#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))


static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pgd[ENTRIES_PER_TABLE] = { 0 };

static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };
    
static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    uart_pte[ENTRIES_PER_TABLE] = { 0 };
    
#define PLIC_PTE_COUNT (0x04000000UL / PMD_SIZE)
static unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    plic_pte[PLIC_PTE_COUNT][ENTRIES_PER_TABLE] = { { 0 } };

void setup_vm()
{
    // TODO: Set up page tables for identity mapping and kernel mapping
    // 取得各個 Page Table 的物理位址 (MMU 尚未開啟時使用)
    unsigned long pgd_pa = (unsigned long)pgd;
    if (pgd_pa >= PAGE_OFFSET) pgd_pa -= PAGE_OFFSET;
    unsigned long *pgd_phys = (unsigned long *)pgd_pa;

    // 請在 setup_vm() 進入迴圈前，先定義好 UART 與 PLIC 的實體資訊
    unsigned long target_uart_pa = 0xd4017000UL;
    //unsigned long uart_size      = 0x00000100UL;
    unsigned long target_plic_pa = 0xe0000000UL;
    unsigned long plic_size      = 0x04000000UL;

    // --- Build identity + higher-half mappings for 4GB RAM ---
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pmd_pa = (unsigned long)pmd[i];
        if (pmd_pa >= PAGE_OFFSET) pmd_pa -= PAGE_OFFSET;
        unsigned long *pmd_phys = (unsigned long *)pmd_pa;

        unsigned long pa_base = (unsigned long)i * PGD_SIZE;
        unsigned long va_base = PAGE_OFFSET + pa_base;

        // 設定 PGD 指向 PMD 的目錄 (使用 PTE_V)
        unsigned long id_pgd_idx = (pa_base >> PGD_SHIFT) & 0x1FF;
        unsigned long high_pgd_idx = (va_base >> PGD_SHIFT) & 0x1FF;
        
        pgd_phys[id_pgd_idx] = MAKE_PTE(pmd_pa, PTE_V);
        pgd_phys[high_pgd_idx] = MAKE_PTE(pmd_pa, PTE_V);

        // 填寫 512 個 PMD entry (2MB 巨型分頁)
        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pa = pa_base + ((unsigned long)j * PMD_SIZE);
            
            // 找出 UART 所在的 2MB 區塊起點
            unsigned long uart_2mb_base = target_uart_pa & ~((1UL << 21) - 1);

            if (pa == uart_2mb_base) {
                // 1. 取得 PTE 表的實體位址
                unsigned long pte_pa = (unsigned long)uart_pte;
                if (pte_pa >= PAGE_OFFSET) pte_pa -= PAGE_OFFSET;
                unsigned long *pte_phys = (unsigned long *)pte_pa;

                // 2. 將 PMD entry 指向 PTE 表 (注意：指向下一級 Page Table 只需要 PTE_V，不需要 RWX 權限)
                pmd_phys[j] = MAKE_PTE(pte_pa, PTE_V);

                // 3. 填寫 PTE 表，將這 2MB 切成 512 個 4KB 頁面
                for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                    unsigned long page_pa = pa + ((unsigned long)k * PAGE_SIZE);
                    
                    // 只有確切落在 UART 範圍內的 4KB 頁面才設定為 PROT_DEVICE
                    if (page_pa >= target_uart_pa && page_pa < (target_uart_pa + PAGE_SIZE)) {
                        pte_phys[k] = MAKE_PTE(page_pa, PROT_DEVICE);
                    } 
                    // 其他空間設為一般 Kernel 權限 (或是依需求設為 0 不映射)
                    else {
                        pte_phys[k] = MAKE_PTE(page_pa, PROT_KERNEL);
                    }
                }
            } 
            // PLIC 範圍 (使用 3-level mapping，4KB 細粒度)
            else if (pa >= target_plic_pa && pa < (target_plic_pa + plic_size)) {
                // 計算當前的 pa 是 PLIC 範圍內的第幾個 2MB 區塊 (0 ~ 31)
                unsigned long plic_block_idx = (pa - target_plic_pa) / PMD_SIZE;

                // 1. 取得對應 PTE 表的實體位址
                unsigned long pte_pa = (unsigned long)plic_pte[plic_block_idx];
                if (pte_pa >= PAGE_OFFSET) pte_pa -= PAGE_OFFSET;
                unsigned long *pte_phys = (unsigned long *)pte_pa;

                // 2. 將 PMD entry 指向該 PTE 表 (只需要 PTE_V)
                pmd_phys[j] = MAKE_PTE(pte_pa, PTE_V);

                // 3. 填寫 PTE 表，將這 2MB 切成 512 個 4KB 頁面
                for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                    unsigned long page_pa = pa + ((unsigned long)k * PAGE_SIZE);
                    
                    // PLIC 的範圍內全都是 MMIO，所以全部設為 Device 權限 (不可執行)
                    pte_phys[k] = MAKE_PTE(page_pa, PROT_DEVICE);
                }
            }
            // 一般的 RAM 空間 (維持 2MB 巨型分頁)
            else {
                pmd_phys[j] = MAKE_PTE(pa, PROT_KERNEL);
            }
        }
    }

    // 啟動 MMU
    asm volatile(
        "sfence.vma zero, zero\n"
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP(pgd_pa))
        : "memory"
    );
}

void drop_identity_map()
{
    // TODO: Drop identity mapping
    // 因為前面 Identity 和 Higher-half 都是共用 pmd 表，
    // 所以我們只要把 PGD 的低位址 entry 拔掉，就能完美斷開映射！
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pa_base = (unsigned long)i * PGD_SIZE;
        pgd[(pa_base >> PGD_SHIFT) & 0x1FF] = 0;
    }

    // 刷新 TLB
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
}

