//
//  riscv-csr.cc
//

#include <map>

#include "riscv-types.h"
#include "riscv-csr.h"

const riscv_csr_metadata riscv_csr_table[] = {
	{ 0x001, riscv_csr_perm_urw, "fflags",    "Floating-Point Accrued Exceptions" },
	{ 0x002, riscv_csr_perm_urw, "frm",       "Floating-Point Dynamic Rounding Mode" },
	{ 0x003, riscv_csr_perm_urw, "fcsr",      "Floating-Point Control and Status Register (frm + fflags)" },
	{ 0xC00, riscv_csr_perm_uro, "cycle",     "Cycle counter for RDCYCLE instruction" },
	{ 0xC01, riscv_csr_perm_uro, "time",      "Timer for RDTIME instruction" },
	{ 0xC02, riscv_csr_perm_uro, "instret",   "Instructions-retired counter for RDINSTRET instruction" },
	{ 0xC80, riscv_csr_perm_uro, "cycleh",    "Upper 32 bits of cycle, RV32I only" },
	{ 0xC81, riscv_csr_perm_uro, "timeh",     "Upper 32 bits of time, RV32I only" },
	{ 0xC82, riscv_csr_perm_uro, "instreth",  "Upper 32 bits of instret, RV32I only" },
	{ 0x100, riscv_csr_perm_srw, "sstatus",   "Supervisor status register" },
	{ 0x101, riscv_csr_perm_srw, "stvec",     "Supervisor trap handler base address" },
	{ 0x104, riscv_csr_perm_srw, "sie",       "Supervisor interrupt-enable register" },
	{ 0x121, riscv_csr_perm_srw, "stimecmp",  "Wall-clock timer compare value" },
	{ 0xD01, riscv_csr_perm_sro, "stime",     "Supervisor wall-clock time register" },
	{ 0xD81, riscv_csr_perm_sro, "stimeh",    "Upper 32 bits of stime, RV32I only" },
	{ 0x140, riscv_csr_perm_srw, "sscratch",  "Scratch register for supervisor trap handlers" },
	{ 0x141, riscv_csr_perm_srw, "sepc",      "Supervisor exception program counter" },
	{ 0xD42, riscv_csr_perm_sro, "scause",    "Supervisor trap cause" },
	{ 0xD43, riscv_csr_perm_sro, "sbadaddr",  "Supervisor bad address" },
	{ 0x144, riscv_csr_perm_srw, "sip",       "Supervisor interrupt pending" },
	{ 0x180, riscv_csr_perm_srw, "sptbr",     "Page-table base register" },
	{ 0x181, riscv_csr_perm_srw, "sasid",     "Address-space ID" },
	{ 0x900, riscv_csr_perm_srw, "cyclew",    "Cycle counter for RDCYCLE instruction" },
	{ 0x901, riscv_csr_perm_srw, "timew",     "Timer for RDTIME instruction" },
	{ 0x902, riscv_csr_perm_srw, "instretw",  "Instructions-retired counter for RDINSTRET instruction" },
	{ 0x980, riscv_csr_perm_srw, "cyclehw",   "Upper 32 bits of cycle, RV32I only" },
	{ 0x981, riscv_csr_perm_srw, "timehw",    "Upper 32 bits of time, RV32I only" },
	{ 0x982, riscv_csr_perm_srw, "instrethw", "Upper 32 bits of instret, RV32I only" },
	{ 0x200, riscv_csr_perm_hrw, "hstatus",   "Hypervisor status register" },
	{ 0x201, riscv_csr_perm_hrw, "htvec",     "Hypervisor trap handler base address" },
	{ 0x202, riscv_csr_perm_hrw, "htdeleg",   "Hypervisor trap delegation register" },
	{ 0x221, riscv_csr_perm_hrw, "htimecmp",  "Hypervisor wall-clock timer compare value" },
	{ 0xE01, riscv_csr_perm_hro, "htime",     "Hypervisor wall-clock time register" },
	{ 0xE81, riscv_csr_perm_hro, "htimeh",    "Upper 32 bits of htime, RV32I only" },
	{ 0x240, riscv_csr_perm_hrw, "hscratch",  "Scratch register for hypervisor trap handlers" },
	{ 0x241, riscv_csr_perm_hrw, "hepc",      "Hypervisor exception program counter" },
	{ 0x242, riscv_csr_perm_hrw, "hcause",    "Hypervisor trap cause" },
	{ 0x243, riscv_csr_perm_hrw, "hbadaddr",  "Hypervisor bad address" },
	{ 0xA01, riscv_csr_perm_hrw, "stimew",    "Supervisor wall-clock timer" },
	{ 0xA81, riscv_csr_perm_hrw, "stimehw",   "Upper 32 bits of supervisor wall-clock timer, RV32I only" },
	{ 0xF00, riscv_csr_perm_mro, "mcpuid",    "CPU description" },
	{ 0xF01, riscv_csr_perm_mro, "mimpid",    "Vendor ID and version number" },
	{ 0xF10, riscv_csr_perm_mro, "mhartid",   "Hardware thread ID" },
	{ 0x300, riscv_csr_perm_mrw, "mstatus",   "Machine status register" },
	{ 0x301, riscv_csr_perm_mrw, "mtvec",     "Machine trap-handler base address" },
	{ 0x302, riscv_csr_perm_mrw, "mtdeleg",   "Machine trap delegation register" },
	{ 0x304, riscv_csr_perm_mrw, "mie",       "Machine interrupt-enable register" },
	{ 0x321, riscv_csr_perm_mrw, "mtimecmp",  "Machine wall-clock timer compare value" },
	{ 0x300, riscv_csr_perm_mrw, "mstatus",   "Machine status register" },
	{ 0x301, riscv_csr_perm_mrw, "mtvec",     "Machine trap-handler base address" },
	{ 0x302, riscv_csr_perm_mrw, "mtdeleg",   "Machine trap delegation register" },
	{ 0x304, riscv_csr_perm_mrw, "mie",       "Machine interrupt-enable register" },
	{ 0x321, riscv_csr_perm_mrw, "mtimecmp",  "Machine wall-clock timer compare value" },
	{ 0x340, riscv_csr_perm_mrw, "mscratch",  "Scratch register for machine trap handlers" },
	{ 0x341, riscv_csr_perm_mrw, "mepc",      "Machine exception program counter" },
	{ 0x342, riscv_csr_perm_mrw, "mcause",    "Machine trap cause" },
	{ 0x343, riscv_csr_perm_mrw, "mbadaddr",  "Machine bad address" },
	{ 0x344, riscv_csr_perm_mrw, "mip",       "Machine interrupt pending" },
	{ 0x380, riscv_csr_perm_mrw, "mbase",     "Base register" },
	{ 0x381, riscv_csr_perm_mrw, "mbound",    "Bound register" },
	{ 0x382, riscv_csr_perm_mrw, "mibase",    "Instruction base register" },
	{ 0x383, riscv_csr_perm_mrw, "mibound",   "Instruction bound register" },
	{ 0x384, riscv_csr_perm_mrw, "mdbase",    "Data base register" },
	{ 0x385, riscv_csr_perm_mrw, "mdbound",   "Data bound register" },
	{ 0xB01, riscv_csr_perm_mrw, "htimew",    "Hypervisor wall-clock timer" },
	{ 0xB81, riscv_csr_perm_mrw, "htimehw",   "Upper 32 bits of hypervisor wall-clock timer, RV32I only" },
	{ 0x780, riscv_csr_perm_mrw, "mtohost",   "Output register to host" },
	{ 0x781, riscv_csr_perm_mrw, "mfromhost", "Input register from host" },
	{ 0x000, riscv_csr_perm_none, nullptr,    nullptr }
};


struct riscv_csr_map : std::map<riscv_hu,const riscv_csr_metadata*>
{
	riscv_csr_map() {
		for (const auto *ent = riscv_csr_table; ent->csr_value; ent++)
			(*this)[ent->csr_value] = ent;
	}
};

const riscv_csr_metadata* riscv_lookup_csr_metadata(riscv_hu csr_value)
{
	static riscv_csr_map csr_map;
	return csr_map[csr_value];
}
