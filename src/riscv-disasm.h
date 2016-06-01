//
//  riscv-disasm.h
//

#ifndef riscv_disasm_h
#define riscv_disasm_h

typedef std::function<const char*(riscv_ptr, bool nearest)> riscv_symbol_name_fn;
typedef std::function<const char*(const char *type)> riscv_symbol_colorize_fn;

const char* riscv_null_symbol_lookup(riscv_ptr, bool nearest);
const char* riscv_null_symbol_colorize(const char *type);
void riscv_disasm_instruction(riscv_disasm &dec, std::deque<riscv_disasm> &dec_hist,
	riscv_ptr pc, riscv_ptr next_pc, riscv_ptr pc_offset, riscv_ptr gp,
	riscv_symbol_name_fn symlookup = riscv_null_symbol_lookup,
	riscv_symbol_colorize_fn colorize = riscv_null_symbol_colorize);

#endif
