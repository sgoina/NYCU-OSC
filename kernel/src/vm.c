#include "defint.h"
#include "mem_alloc.h"
#include "utils.h"
#include "vm.h"

// __attribute__ ((attribute-list)): setting attribute for this variable
// Assign page tables to be in ".data" section to avoid being in ".bss" section to be clear.
unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pgd[ENTRIES_PER_TABLE] = { 0 };

unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    pmd[LINEAR_MAP_GIB][ENTRIES_PER_TABLE] = { { 0 } };
    
unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    uart_pte[ENTRIES_PER_TABLE] = { 0 };
    
#define PLIC_PTE_COUNT (0x04000000UL / PMD_SIZE) // the size of PLIC from devicetree
unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    plic_pte[PLIC_PTE_COUNT][ENTRIES_PER_TABLE] = { { 0 } };
    
#define FB_PTE_COUNT (0x01000000UL / PMD_SIZE) // the size of framebuffer from devicetree
unsigned long __attribute__((section(".data"), aligned(PAGE_SIZE)))
    fb_pte[FB_PTE_COUNT][ENTRIES_PER_TABLE] = { { 0 } };
  
// Set up page tables for identity mapping and kernel mapping
void setup_vm()
{
    unsigned long pgd_pa = (unsigned long)pgd;
    
    // MMIO info
    unsigned long target_uart_pa = 0xd4017000UL;
    //unsigned long uart_size      = 0x00000100UL;
    unsigned long uart_2mb_base = target_uart_pa & ~((1UL << 21) - 1); // Begin address in 2MB block of UART
    unsigned long target_plic_pa = 0xe0000000UL;
    unsigned long plic_size      = 0x04000000UL;
    unsigned long target_fb_pa   = 0x7f000000UL;
    unsigned long fb_size        = 0x01000000UL;

    // Build identity + higher-half mappings
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pmd_pa = (unsigned long)pmd[i];

        unsigned long pgd_base_pa = (unsigned long)i * PGD_SIZE;
        unsigned long pgd_base_va = phys_to_virt(pgd_base_pa);

        // PGD entry points to PMD
        unsigned long id_pgd_idx = (pgd_base_pa >> PGD_SHIFT) & 0x1FF;
        unsigned long high_pgd_idx = (pgd_base_va >> PGD_SHIFT) & 0x1FF;
        
        pgd[id_pgd_idx] = MAKE_PTE(pmd_pa, PTE_V);
        pgd[high_pgd_idx] = MAKE_PTE(pmd_pa, PTE_V);

        // Set PMD
        for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
            unsigned long pa = pgd_base_pa + ((unsigned long)j * PMD_SIZE);
            
            // If PMD entry is for UART
            if (pa == uart_2mb_base) {
                unsigned long uart_pte_pa = (unsigned long)uart_pte;
                // PMD entry points to uart_pte
                pmd[i][j] = MAKE_PTE(uart_pte_pa, PTE_V);
                // Set UART PTE. Because UART is only 0x100 Bytes, so need to determine the address is for which
                for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                    unsigned long uart_pa = pa + ((unsigned long)k * PAGE_SIZE);
                    
                    // The pte entry sets to PROT_DEVICE for UART address space
                    if (uart_pa >= target_uart_pa && uart_pa < (target_uart_pa + PAGE_SIZE))
                        uart_pte[k] = MAKE_PTE(uart_pa, PROT_DEVICE);
                    // Other pte entry sets to PROT_KERNEL
                    else 
                        uart_pte[k] = MAKE_PTE(uart_pa, PROT_DEVICE);
                }
            } 
            // If PMD entry is for PLIC
            else if (pa >= target_plic_pa && pa < (target_plic_pa + plic_size)) {
                // The index of block for PLIC
                unsigned long plic_block_idx = (pa - target_plic_pa) / PMD_SIZE;

                unsigned long plic_pte_pa = (unsigned long)plic_pte[plic_block_idx];
                // PMD entry points to plic_pte
                pmd[i][j] = MAKE_PTE(plic_pte_pa, PTE_V);

                // Set PLIC PTE
                for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                    unsigned long plic_pa = pa + ((unsigned long)k * PAGE_SIZE);
                    plic_pte[plic_block_idx][k] = MAKE_PTE(plic_pa, PROT_DEVICE);
                }
            }
            // If PMD entry is for framebuffer
            else if (pa >= target_fb_pa && pa < target_fb_pa + fb_size) {
                // The index of block for framebuffer
                unsigned long fb_block_idx = (pa - target_fb_pa) / PMD_SIZE;
                
                unsigned long fb_pte_pa = (unsigned long)fb_pte[fb_block_idx];
                // PMD entry points to plic_pte
                pmd[i][j] = MAKE_PTE(fb_pte_pa, PTE_V);

                // Set PLIC PTE
                for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                    unsigned long fb_pa = pa + ((unsigned long)k * PAGE_SIZE);
                    fb_pte[fb_block_idx][k] = MAKE_PTE(fb_pa, PROT_DEVICE);
                }
            }
            // If PMD entry is for normal space
            else {
                if (pa < RAM_UPPER_BOUND)
                    pmd[i][j] = MAKE_PTE(pa, PROT_KERNEL);
                else 
                    pmd[i][j] = MAKE_PTE(pa, PROT_DEVICE);
            }
        }
    }

    // Start MMU
    // sfence.vma vaddr, asid: Flush TLB
    // vaddr for specific virtual address, 0 means all virtual address
    // asid for specific address space ID, 0 means all address space ID
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP(pgd_pa))
        : "memory"
    );
}

// Drop identity mapping
void drop_identity_map()
{
    // Only drop lower PGD entry because PMD are share for identity/higher mapping
    for (int i = 0; i < LINEAR_MAP_GIB; i++) {
        unsigned long pa_base = (unsigned long)i * PGD_SIZE;
        pgd[(pa_base >> PGD_SHIFT) & 0x1FF] = 0;
    }

    asm volatile("sfence.vma zero, zero" ::: "memory");
}


void pagewalk(unsigned long *user_pgd, unsigned long va, unsigned long pa, unsigned long prot) {
    unsigned long vpn2 = (va >> PGD_SHIFT) & 0x1FF;
    unsigned long vpn1 = (va >> PMD_SHIFT) & 0x1FF;
    unsigned long vpn0 = (va >> PTE_SHIFT) & 0x1FF;

    // Check PGD
    if (!(user_pgd[vpn2] & PTE_V)) {
        void* new_page = allocate(PAGE_SIZE); // allocate a entry for pgd[vpn2]
        memset(new_page, 0, PAGE_SIZE);
        unsigned long new_page_pa = (unsigned long)virt_to_phys(new_page);
        user_pgd[vpn2] = MAKE_PTE(new_page_pa, PTE_V);
    }
    
    // From PGD get the PMD base address
    unsigned long pmd_base_pa = (user_pgd[vpn2] >> 10) << 12;
    unsigned long* pmd_base = (unsigned long*)phys_to_virt(pmd_base_pa);

    // Check PMD
    if (!(pmd_base[vpn1] & PTE_V)) {
        void* new_page = allocate(PAGE_SIZE); // allocate a entry for pmd[vpn1]
        memset(new_page, 0, PAGE_SIZE);
        unsigned long new_page_pa = (unsigned long)virt_to_phys(new_page);
        pmd_base[vpn1] = MAKE_PTE(new_page_pa, PTE_V);
    }

    // From PMD get the pte base address
    unsigned long pte_base_pa = (pmd_base[vpn1] >> 10) << 12;
    unsigned long* pte_base = (unsigned long*)phys_to_virt(pte_base_pa);

    // Make a pte and place it into page tagle
    pte_base[vpn0] = MAKE_PTE(pa, prot);
}

void map_pages(unsigned long *user_pgd, unsigned long va, unsigned long size, unsigned long pa, unsigned long prot) {
    // align to 4KB
    unsigned long end_va = va + size;
    va = va & ~(PAGE_SIZE - 1);
    pa = pa & ~(PAGE_SIZE - 1);

    while (va < end_va) {
        pagewalk(user_pgd, va, pa, prot);
        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
}

// Get the physical address of the pte
unsigned long* get_pte(unsigned long *pgd, unsigned long va) {
    unsigned long vpn2 = (va >> PGD_SHIFT) & 0x1FF;
    unsigned long vpn1 = (va >> PMD_SHIFT) & 0x1FF;
    unsigned long vpn0 = (va >> PTE_SHIFT) & 0x1FF;

    // Check PGD
    unsigned long pgd_entry = pgd[vpn2];
    if (!(pgd_entry & PTE_V))
        return NULL;
    
    // Check PMD
    unsigned long pmd_base_pa = (pgd_entry >> 10) << 12;
    unsigned long *pmd_base = (unsigned long *)phys_to_virt(pmd_base_pa);
    unsigned long pmd_entry = pmd_base[vpn1];
    if (!(pmd_entry & PTE_V))
        return NULL;

    // Check PTE
    unsigned long pte_base_pa = (pmd_entry >> 10) << 12;
    unsigned long *pte_base = (unsigned long *)phys_to_virt(pte_base_pa);
    
    if (!(pte_base[vpn0] & PTE_V))
        return NULL;
    return &pte_base[vpn0];
}

// Free User page table
void free_page_tables(unsigned long *pgd) {
    if (!pgd)
        return;
    // 256 ~ 512 are kernel space, not to free
    for (int i = 0; i < 256; i++) {
        if (pgd[i] & PTE_V) {
            unsigned long *pmd = (unsigned long *)phys_to_virt((pgd[i] >> 10) << 12); // Get pmd base address
            for (int j = 0; j < ENTRIES_PER_TABLE; j++) {
                if (pmd[j] & PTE_V) {
                    unsigned long *pte = (unsigned long *)phys_to_virt((pmd[j] >> 10) << 12); // Get pte base address
                    for (int k = 0; k < ENTRIES_PER_TABLE; k++) {
                        if (pte[k] & PTE_V) {
                            unsigned long pa = (pte[k] >> 10) << 12;
                            free((void *)phys_to_virt(pa));
                        }
                    }
                    free((void *)pte);
                }
            }
            free((void *)pmd);
        }
    }
    free((void *)pgd);
}
