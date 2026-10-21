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

		/* XXX:
					VBAR_EL1 is an AArch64 system register that holds the exception
					vector table base address for Exception Level 1 (EL1).  When an
					exception is taken to EL1, the processor uses VBAR_EL1 and the
					exception type to determine which handler entry to execute.


					In AArch64, the exception vector table pointed to by VBAR_EL1 has 16 entries, each 128 bytes (0x80) apart.
					Number of entries: 16
					Size of each vector slot: 128 bytes
					Total vector table size: 16 × 128 = 2048
																	 16×128=2048 bytes = 2 KB
					Required alignment: 2 KB (0x800)


					VBAR_EL1 containes the exception handler for current (EL1) and the
					lower EL (EL0), for an example:-
					VBAR_EL1 = 0x80000000
										Address        Offset     Meaning
					-------------------------------------------------
					0x80000000     +0x000     Current EL, SP0, Sync
					0x80000080     +0x080     Current EL, SP0, IRQ
					0x80000100     +0x100     Current EL, SP0, FIQ
					0x80000180     +0x180     Current EL, SP0, SError

					0x80000200     +0x200     Current EL, SPx, Sync
					0x80000280     +0x280     Current EL, SPx, IRQ
					0x80000300     +0x300     Current EL, SPx, FIQ
					0x80000380     +0x380     Current EL, SPx, SError

					0x80000400     +0x400     Lower EL, AArch64, Sync  <-- SVC from EL0
					0x80000480     +0x480     Lower EL, AArch64, IRQ
					0x80000500     +0x500     Lower EL, AArch64, FIQ
					0x80000580     +0x580     Lower EL, AArch64, SError

					0x80000600     +0x600     Lower EL, AArch32, Sync
					0x80000680     +0x680     Lower EL, AArch32, IRQ
					0x80000700     +0x700     Lower EL, AArch32, FIQ
					0x80000780     +0x780     Lower EL, AArch32, SError
		*/
    // set vector base address register
    _puts("Setting Vector Base Address Register (VBAR_EL1)\n");
    val64 = (uint64)&vectors;
    //val64 = val64 + (uint64)KERNBASE;
    asm("MSR VBAR_EL1, %[v]": :[v]"r" (val64):);


		/* XXX:
				## `TCR_EL1` — compact bit-field table

| Bits | Field | Purpose |
| --- | --- | --- |
| `[5:0]` | `T0SZ` | `TTBR0` virtual address size |
| `[7]` | `EPD0` | Disable `TTBR0` table walks |
| `[9:8]` | `IRGN0` | Inner cacheability, table walks |
| `[11:10]` | `ORGN0` | Outer cacheability, table walks |
| `[13:12]` | `SH0` | `TTBR0` walk shareability |
| `[15:14]` | `TG0` | `TTBR0` page granule |
| `[21:16]` | `T1SZ` | `TTBR1` virtual address size |
| `[22]` | `A1` | Select TTBR for ASID |
| `[23]` | `EPD1` | Disable `TTBR1` table walks |
| `[25:24]` | `IRGN1` | Inner cacheability, table walks |
| `[27:26]` | `ORGN1` | Outer cacheability, table walks |
| `[29:28]` | `SH1` | `TTBR1` walk shareability |
| `[31:30]` | `TG1` | `TTBR1` page granule |
| `[34:32]` | `IPS` | Physical address size |
| `[36]` | `AS` | ASID size |
| `[37]` | `TBI0` | Ignore top byte for `TTBR0` addresses |
| `[38]` | `TBI1` | Ignore top byte for `TTBR1` addresses |
| `[39]` | `HA` | Hardware access-flag updates |
| `[40]` | `HD` | Hardware dirty-state management |
| `[41]` onward | Optional controls | Extensions such as `HPD`, `HWU`, `TBID`, `NFD`, `E0PD`, and `TCMA`, depending on architecture version |
Note: This summarizes common fields; optional fields and exact definitions depend on the ARM architecture revision and implemented extensions.



If you write `0x34B5203520` to `TCR_EL1`, the CPU interprets it as a 
64-bit register value whose individual bits configure the MMU. It 
does not interpret it as a single mode or command.

## Decode the value
The value is `0x00000034B5203520` when zero-extended to 64 bits.

| Field | Bits | Value | Meaning |
| --- | --- | --- | --- |
| `T0SZ` | `[5:0]` | `0x20` = 32 | 32-bit `TTBR0` virtual address region |
| `EPD0` | `[7]` | 0 | `TTBR0` walks enabled |
| `IRGN0` | `[9:8]` | 2 | Reserved encoding |
| `ORGN0` | `[11:10]` | 0 | Normal non-cacheable walk attribute |
| `SH0` | `[13:12]` | 2 | Outer-shareable |
| `TG0` | `[15:14]` | 0 | 4 KB granule |
| `T1SZ` | `[21:16]` | `0x20` = 32 | 32-bit `TTBR1` virtual address region |
| `TG1` | `[31:30]` | 0 | Reserved encoding |
| `IPS` | `[34:32]` | 5 | 48-bit physical address size |
| Other fields | Various | — | Must be decoded against the target CPU's architecture revision |

Important: This value contains reserved encodings in fields such as `IRGN0` and 
`TG1`. Do not write it into `TCR_EL1` on a real system without verifying the 
architecture-specific encodings and supported features. Reserved values can 
cause unpredictable behavior or prevent correct MMU operation.



			XXX: Its for EL0 and EL1 memory the important result is that both
			TTBR0_EL1 and TTBR1_EL1 are configured for 32-bit virtual-address regions
			with 4 KB granules. Assuming the CPU supports the selected features and
			the rest of the MMU configuration is valid, each region uses 3 page-table
			levels: L1 → L2 → L3 

			XXX: VA -> PA
			For the above configuration va -> pa will look like this:-
			EL0 VA = 0x12345678
Split it according to the table above:
Component       VA bits      Value
L1 index        [31:30]      0x0
L2 index        [29:21]      0x091
L3 index        [20:12]      0x145
Page offset     [11:0]       0x678

The hardware performs the following conceptual steps:
	Read TTBR0_EL1 to obtain the L1 table base.
	Read L1 entry 0x0. If it points to an L2 table, follow that address.
	Read L2 entry 0x091. If it points to an L3 table, follow that address.
	Read L3 entry 0x145 to obtain the physical page base and check permissions.
	Add the page offset 0x678 to that physical page base.

*/
    // set translation control register
    _puts("Setting Translation Control Register (TCR_EL1)\n");
    val64 = (uint64)0x34B5203520;
    asm("MSR TCR_EL1, %[v]": :[v]"r" (val64):);
    asm("ISB": : :);

		/* XXX: Kernel page table */
    // set translation table base register 1 (kernel)
    _puts("Setting Translation Table Base Register 1 (TTBR1_EL1)\n");
    val64 = (uint64)kernel_pgtbl;
    asm("MSR TTBR1_EL1, %[v]": :[v]"r" (val64):);

		/* XXX: User page table */
    // set translation table base register 0 (user)
    _puts("Setting Translation Table Base Register 0 (TTBR0_EL1)\n");
    val64 = (uint64)user_pgtbl;
    asm("MSR TTBR0_EL1, %[v]": :[v]"r" (val64):);
    asm("ISB":::);

		/* XXX: SCTLR_EL1 msb = SCTLR_EL1.M
						SCTLR_EL1.M = 0 — MMU disabled
						SCTLR_EL1.M = 1 — MMU enabled

						Before setting SCTLR_EL1.M, you must ensure that: TCR_EL1 is
						configured correctly.  TTBR0_EL1 and TTBR1_EL1 point to valid
						translation tables.  The page-table descriptors and memory
						attributes are valid.  The instructions and data accessed
						immediately after enabling translation remain accessible under the
						new address mapping.
		*/
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
