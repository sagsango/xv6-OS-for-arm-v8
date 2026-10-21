// initialize section
#include "types.h"
#include "param.h"
#include "arm.h"
#include "mmu.h"
#include "defs.h"
#include "memlayout.h"

void _uart_putc(int c)
{
    volatile uint8 * uart0 = (uint8*)UART0;
    *uart0 = c;
}


void _puts (char *s)
{
    while (*s != '\0') {
        _uart_putc(*s);
        s++;
    }
}

void _putint (char *prefix, uint val, char* suffix)
{
    char* arr = "0123456789ABCDEF";
    int idx;

    if (prefix) {
        _puts(prefix);
    }

    for (idx = sizeof(val) * 8 - 4; idx >= 0; idx -= 4) {
        _uart_putc(arr[(val >> idx) & 0x0F]);
    }

    if (suffix) {
        _puts(suffix);
    }
}

void _do_exception(void)
{
    _puts("Exception Raised\n");
    while(1);
}

// kernel page table, reserved in the kernel.ld
extern uint64 _kernel_pgtbl;
extern uint64 _user_pgtbl;
extern uint64 _K_l2_pgtbl;
extern uint64 _U_l2_pgtbl;

uint64 *kernel_pgtbl = &_kernel_pgtbl;
uint64 *user_pgtbl = &_user_pgtbl;

extern void * vectors;

// setup the boot page table: dev_mem whether it is device memory
void set_bootpgtbl (uint64 virt, uint64 phy, uint len, int dev_mem )
{
    uint64	pde;
    int         idx;
    int		pgdidx;
    int		pmdidx;
    uint64	*level2;

    for (idx = 0; idx < len; idx = idx + 0x200000) {

        pgdidx = PGD_IDX(virt);
        pmdidx = PMD_IDX(virt);

        pde = phy & PMD_MASK;

        if (!dev_mem) {
            // normal memory
            pde |= ACCESS_FLAG | SH_IN_SH | AP_RW_1 | NON_SECURE_PA | MEM_ATTR_IDX_4 | ENTRY_BLOCK | ENTRY_VALID | UXN;
        } else {
            // device memory
            pde |= ACCESS_FLAG | AP_RW_1 | MEM_ATTR_IDX_0 | ENTRY_BLOCK | ENTRY_VALID;
        }

        level2 = (uint64 *)(kernel_pgtbl[pgdidx] & PG_ADDR_MASK);
        level2[pmdidx] = pde;

        level2 = (uint64 *)(user_pgtbl[pgdidx] & PG_ADDR_MASK);
        level2[pmdidx] = pde;

        virt = virt + 0x200000;
        phy = phy + 0x200000;

    }
}

void load_pgtlb (uint64* kern_pgtbl, uint64* user_pgtbl)
{
    char	arch;
    uint32	val32;
    uint64	val64;

	  /* XXX: MIDR_EL1 : Provides identification information for the PE,
											 including an implementer code for the device and
											 a device ID number.

											 The MIDR_EL1 (Main ID Register) can be read by 
											 software running at EL1 (Kernel mode), EL2 
											 (Hypervisor mode), or EL3 (Secure Monitor mode).
		*/
    // read the main id register to make sure we are running on ARMv6
    asm("MRS %[r], MIDR_EL1":[r]"=r" (val32): :);

    if (val32 >> 24 == 0x41) {
        _puts ("Implementer: ARM Limited\n");
    }

    arch = (val32 >> 16) & 0x0F;

    if ((arch != 7) && (arch != 0xF)) {
        _puts ("Need AARM v6 or higher\n");
    }


	  /* XXX: CurrentEL : (Current Exception Level)
												to hold the current execution/exception level at
												which the processor is running.

												Can be read in EL1, EL2, EL3.

												But not in EL0, read in EL0 will result this,
												the processor will immediately block the execution
												and trigger a Synchronous Exception (Trap) to a 
												higher privilege level (usually EL1)
		*/
								
    //EL?
    asm("MRS %[r], CurrentEL":[r]"=r" (val32): :);

    val32 = (val32 & 0x0C) >> 2;
    switch(val32) {
        case 0:
            _puts("Current EL: EL0\n");
            break;
        case 1:
            _puts("Current EL: EL1\n");
            break;
        case 2:
            _puts("Current EL: EL2\n");
            break;
        case 3:
            _puts("Current EL: EL3\n");
            break;
        default:
            _puts("Current EL: Unknown\n");
    }

    // flush TLB and cache
    _puts("Flushing TLB and Instr Cache\n");


		/* XXX: The IC instruction requires an operation suffix that specifies what
		 * level of cache to clear and what argument to pass:
		 * • IC IALLU (Instruction Cache Invalidate All to PoU): Invalidates all
					instruction caches up to the Point of Unification (PoU) for the current
					processor core. It does not require a register argument.  

		 * • IC IALLUIS (Instruction Cache Invalidate All to PoU, Inner Shareable):
			Broadcasts the invalidation to all CPU cores in the same Inner Shareable 
			domain. This ensures that every core's instruction cache is cleared simultaneously. 

		 * • IC IVAU, <Xt> (Instruction Cache Invalidate by Virtual Address to PoU):
		   Invalidates a single instruction cache line containing the Virtual Addres
			 passed in the general-purpose register <Xt>.
		*/
    //flush Instr Cache
    asm("IC IALLUIS": : :);


		/* XXX: TLBI VMALLE1; 
								Invalidate all Stage 1 & 2 TLB entries for current VMID at EL1/EL0
		*/
    //flush TLB
    asm("TLBI VMALLE1" : : :);
		
		/* XXX: Ensure the TLB invalidation completes across the
						we will wait untill then
		*/
    asm("DSB SY" : : :);

		/* XXX: CPACR_EL1 :
									Controls access to trace, SME, Streaming SVE, SVE, and Advanced SIMD
		 *						and floating-point functionality.
		*/
    // no trapping on FP/SIMD instructions
    val32 = 0x03 << 20;
    asm("MSR CPACR_EL1, %[v]": :[v]"r" (val32):);

		/* XXX: 
					MDSCR_EL1, Monitor Debug System Control Register, EL1
		*/
    // monitor debug: all disabled
    asm("MSR MDSCR_EL1, xzr":::);

		/* XXX: MAIR_EL1:
						The key point is that MAIR_EL1 does not assign each attribute to a
						specific physical memory region. It defines eight possible
						memory-attribute encodings. Your page tables determine which region
						uses each attribute through the AttrIndx field in each page or block
						descriptor.

				Attr0
					0x00
					Device-nGnRnE
				Attr1
					0x04
					Device-nGnRE
				Attr2
					0x0C
					Device-GRE
				Attr3
					0x44
					Normal, non-cacheable
				Attr4
					0xFF
					Normal, write-back cacheable
				Attr5
					0x00
					Device-nGnRnE
				Attr6
					0x00
					Device-nGnRnE
				Attr7
					0x00
					Device-nGnRnE

			  In an AArch64 4-level page table, the AttrIndx field is bits [4:2] of 
				each valid block or page descriptor. These 3 bits select the corresponding
				attribute index in MAIR_EL1.
	 */
    // set memory attribute indirection register
    _puts("Setting Memory Attribute Indirection Register (MAIR_EL1)\n");
    val64 = (uint64)0xFF440C0400;
    asm("MSR MAIR_EL1, %[v]": :[v]"r" (val64):);
		/* XXX: Why ISB here?
						
						 ISB: is used to synchronize the processor after changing a system register.
						 ISB: The processor ensures that subsequent instructions are fetched and
									executed after the synchronization point, so they observe the relevant
									updated architectural state.
		*/
    asm("ISB": : :);

    // set vector base address register
    _puts("Setting Vector Base Address Register (VBAR_EL1)\n");
    val64 = (uint64)&vectors;
    //val64 = val64 + (uint64)KERNBASE;
    asm("MSR VBAR_EL1, %[v]": :[v]"r" (val64):);

    // set translation control register
    _puts("Setting Translation Control Register (TCR_EL1)\n");
    val64 = (uint64)0x34B5203520;
    asm("MSR TCR_EL1, %[v]": :[v]"r" (val64):);
    asm("ISB": : :);

    // set translation table base register 1 (kernel)
    _puts("Setting Translation Table Base Register 1 (TTBR1_EL1)\n");
    val64 = (uint64)kernel_pgtbl;
    asm("MSR TTBR1_EL1, %[v]": :[v]"r" (val64):);

    // set translation table base register 0 (user)
    _puts("Setting Translation Table Base Register 0 (TTBR0_EL1)\n");
    val64 = (uint64)user_pgtbl;
    asm("MSR TTBR0_EL1, %[v]": :[v]"r" (val64):);
    asm("ISB":::);

    // set system control register
    _puts("Setting System Control Register (SCTLR_EL1)\n");
    asm("MRS %[r], SCTLR_EL1":[r]"=r" (val32): :); //0x0000000000c50838
    val32 = val32 | 0x01;
    asm("MSR SCTLR_EL1, %[v]": :[v]"r" (val32):);
    asm("ISB": : :);

    _puts("System Configure Completed...\n\n");

}

extern void * edata_entry;
extern void * svc_stktop;
extern void * edata;
extern void * end;

extern void jump_stack (void);
extern void kmain (void);

// clear the BSS section for the main kernel, see kernel.ld
void clear_bss (void)
{
    _puts("clearing BSS section for the main kernel\n");
    memset(&edata, 0x00, &end-&edata);
}

void start (void)
{
    uint64	l2pgtbl;
    uint	index;

    _puts("starting xv6 for ARMv8...\n");

    // Set PGD Entries (4 entries...supporting 32 bits)
    for(index = 0; index < 4; index++) {
        l2pgtbl = (uint64)&_K_l2_pgtbl;
        l2pgtbl += index * 4096;
        l2pgtbl = l2pgtbl | ENTRY_TABLE | ENTRY_VALID;
        kernel_pgtbl[index] = l2pgtbl;
    }

    for(index = 0; index < 4; index++) {
        l2pgtbl = (uint64)&_U_l2_pgtbl;
        l2pgtbl += index * 4096;
        l2pgtbl = l2pgtbl | ENTRY_TABLE | ENTRY_VALID;
        user_pgtbl[index] = l2pgtbl;
    }

    // double map the low memory, required to enable paging
    // we do not map all the physical memory
    set_bootpgtbl((uint64)PHY_START, (uint64)PHY_START, INIT_KERN_SZ, 0);
    set_bootpgtbl((uint64)KERNBASE + (uint64)PHY_START, (uint64)PHY_START, INIT_KERN_SZ, 0);

    //add to prevent crash on _puts
    set_bootpgtbl((uint64)DEVBASE2, (uint64)DEVBASE2, DEV_MEM_SZ, 1); // V, P, SZ, ISDEV

    // V, P, len, is_mem
    set_bootpgtbl((uint64)KERNBASE+(uint64)DEVBASE1, (uint64)DEVBASE1, DEV_MEM_SZ, 1); // V, P, SZ, ISDEV
    set_bootpgtbl((uint64)KERNBASE+(uint64)DEVBASE2, (uint64)DEVBASE2, DEV_MEM_SZ, 1); // V, P, SZ, ISDEV
    set_bootpgtbl((uint64)KERNBASE+(uint64)DEVBASE3, (uint64)DEVBASE2, DEV_MEM_SZ, 1); // V, P, SZ, ISDEV

    load_pgtlb (kernel_pgtbl, user_pgtbl);

    // Chnage SP from physical to virtual
    jump_stack ();

    // We can now call normal kernel functions at high memory
    clear_bss ();

    _puts("Starting Kernel\n");
    kmain ();
}
