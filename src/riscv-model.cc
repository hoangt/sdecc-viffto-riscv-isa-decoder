#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>

#include <unistd.h>

#include "riscv-util.h"
#include "riscv-model.h"

static const char* ARGS_FILE           = "args";
static const char* ENUMS_FILE          = "enums";
static const char* TYPES_FILE          = "types";
static const char* FORMATS_FILE        = "formats";
static const char* CODECS_FILE         = "codecs";
static const char* EXTENSIONS_FILE     = "extensions";
static const char* REGISTERS_FILE      = "registers";
static const char* CSRS_FILE           = "csrs";
static const char* OPCODES_FILE        = "opcodes";
static const char* CONSTRAINTS_FILE    = "constraints";
static const char* COMPRESSION_FILE    = "compression";
static const char* INSTRUCTIONS_FILE   = "instructions";
static const char* DESCRIPTIONS_FILE   = "descriptions";

template <typename T>
std::string join(std::vector<T> list, std::string sep)
{
	std::stringstream ss;
	for (auto i = list.begin(); i != list.end(); i++) {
		ss << (i != list.begin() ? sep : "") << *i;
	}
	return ss.str();
}

int64_t riscv_parse_value(const char* valstr)
{
	int64_t val;
	if (strncmp(valstr, "0x", 2) == 0) {
		val = strtoull(valstr + 2, nullptr, 16);
	} else if (strncmp(valstr, "0b", 2) == 0) {
		val = strtoull(valstr + 2, nullptr, 2);
	} else if (strncmp(valstr, "0", 1) == 0) {
		val = strtoull(valstr + 1, nullptr, 8);
	} else {
		val = strtoull(valstr, nullptr, 10);
	}
	return val;
}

riscv_bitrange::riscv_bitrange(std::string bitrange)
{
	std::vector<std::string> comps = split(bitrange, ":", false, false);
	if (comps.size() < 1 || comps.size() > 2) {
		panic("invalid bitrange: %s", bitrange.c_str());
	}
	if (comps.size() == 2) {
		msb = strtoul(comps[0].c_str(), nullptr, 10);
		lsb = strtoul(comps[1].c_str(), nullptr, 10);
	} else {
		msb = lsb = strtoul(comps[0].c_str(), nullptr, 10);
	}
}

std::string riscv_bitrange::to_string(std::string sep, bool collapse_single_bit_range)
{
	std::stringstream ss;
	ss << msb;
	if (!collapse_single_bit_range || msb != lsb) {
		ss << sep << lsb;
	}
	return ss.str();
}

riscv_bitspec::riscv_bitspec(std::string bitspec)
{
	/*
	 * example bitrange specs in gather[scatter](,...) format
	 *
	 *     0
	 *     11:7
	 *     12[5],6:2[4:0]
	 *     31:25[12|10:5],11:7[4:1|11]
	 *
	 * when [scatter] is ommitted, bits are right justified from bit 0
	 */

	std::vector<std::string> comps = split(bitspec, ",", false, false);
	for (std::string comp : comps) {
		size_t bopen = comp.find("[");
		size_t bclose = comp.find("]");
		if (bopen != std::string::npos && bclose != std::string::npos) {
			riscv_bitrange gather(comp.substr(0, bopen));
			std::string scatter_spec = comp.substr(bopen + 1, bclose - bopen - 1);
			riscv_bitrange_list scatter;
			for (auto scatter_comp : split(scatter_spec, "|", false, false)) {
				scatter.push_back(riscv_bitrange(scatter_comp));
			}
			segments.push_back(riscv_bitseg(gather, scatter));
		} else {
			riscv_bitrange gather(comp);
			riscv_bitrange_list scatter;
			segments.push_back(riscv_bitseg(gather, scatter));
		}
	}
}

bool riscv_bitspec::matches_bit(ssize_t bit)
{
	for (auto si = segments.begin(); si != segments.end(); si++) {
		if (bit <= si->first.msb && bit >= si->first.lsb) return true;
	}
	return false;
}

std::string riscv_bitspec::to_string()
{
	std::stringstream ss;
	for (auto si = segments.begin(); si != segments.end(); si++) {
		if (si != segments.begin()) ss << ",";
		ss << si->first.to_string(":") << "[";
		for (auto ti = si->second.begin(); ti != si->second.end(); ti++) {
			if (ti != si->second.begin()) ss << "|";
			ss << ti->to_string(":");
		}
		ss << "]";
	}
	return ss.str();
}

std::string riscv_bitspec::to_template()
{
	ssize_t msb = 0;
	for (auto si = segments.begin(); si != segments.end(); si++) {
		for (auto ti = si->second.begin(); ti != si->second.end(); ti++) {
			if (ti->msb > msb) msb = ti->msb;
		}
	}
	std::stringstream ss;
	ss << "imm_t<" << (msb + 1) << ", ";
	for (auto si = segments.begin(); si != segments.end(); si++) {
		if (si != segments.begin()) ss << ", ";
		ss << "S<" << si->first.to_string(",", false) << ", ";
		for (auto ti = si->second.begin(); ti != si->second.end(); ti++) {
			if (ti != si->second.begin()) ss << ",";
			ss << "B<" << ti->to_string(",") << ">";
		}
		ss << ">";
	}
	ss << ">";
	return ss.str();
}

void riscv_codec_node::clear()
{
	bits.clear();
	vals.clear();
	val_opcodes.clear();
	val_decodes.clear();
}

riscv_opcode_mask riscv_meta_model::decode_mask(std::string bit_spec)
{
	std::vector<std::string> spart = split(bit_spec, "=", false, false);
	if (spart.size() != 2) {
		panic("bit range %s must be in form n..m=v\n", bit_spec.c_str());
	}
	std::vector<std::string> rpart = split(spart[0], "..", false, false);
	ssize_t msb, lsb, val;
	if (rpart.size() == 1) {
		msb = lsb = strtoul(rpart[0].c_str(), nullptr, 10);
	} else if (rpart.size() == 2) {
		msb = strtoul(rpart[0].c_str(), nullptr, 10);
		lsb = strtoul(rpart[1].c_str(), nullptr, 10);
	} else {
		panic("bit range %s must be in form n..m=v\n", bit_spec.c_str());
	}
	if (spart[1].find("0x") == 0) {
		val = strtoul(spart[1].c_str() + 2, nullptr, 16);
	} else {
		val = strtoul(spart[1].c_str(), nullptr, 10);
	}

	return riscv_opcode_mask(riscv_bitrange(msb, lsb), val);
}

std::vector<riscv_bitrange> riscv_meta_model::bitmask_to_bitrange(std::vector<ssize_t> &bits)
{
	std::vector<riscv_bitrange> v;
	if (bits.size() > 0) {
		v.push_back(riscv_bitrange(bits[0], bits[0]));
		for (size_t i = 1; i < bits.size(); i++) {
			if (bits[i] + 1 == v.back().lsb) {
				v.back().lsb = bits[i];
			} else {
				v.push_back(riscv_bitrange(bits[i], bits[i]));
			}
		}
	}
	return v;
}

std::string riscv_meta_model::format_bitmask(std::vector<ssize_t> &bits, std::string var, bool comment)
{
	std::vector<riscv_bitrange> v = bitmask_to_bitrange(bits);
	std::stringstream ss;

	ssize_t total_length = bits.size();
	ssize_t range_start = bits.size();

	for (auto ri = v.begin(); ri != v.end(); ri++) {
		riscv_bitrange r = *ri;
		ssize_t range_end = range_start - (r.msb - r.lsb);
		ssize_t shift = r.msb - range_start + 1;
		if (ri != v.begin()) ss << " | ";
		ss << "((" << var << " >> " << shift << ") & 0b";
		for (ssize_t i = total_length; i > 0; i--) {
			if (i <= range_start && i >= range_end) ss << "1";
			else ss << "0";
		}
		ss << ")";
		range_start -= (r.msb - r.lsb) + 1;
	}

	if (comment) {
		ss << " /* " << var << "[";
		for (auto ri = v.begin(); ri != v.end(); ri++) {
			riscv_bitrange r = *ri;
			if (ri != v.begin()) ss << "|";
			if (r.msb == r.lsb) ss << r.msb;
			else ss << r.msb << ":" << r.lsb;
		}
		ss << "] */";
	}

	return ss.str();
}

std::string riscv_meta_model::opcode_mask(riscv_opcode_ptr opcode)
{
	std::stringstream ss;
	ss << std::left << std::setw(20) << "";
	for (auto &mask : opcode->masks) {
		ss << " " << mask.first.msb << ".." << mask.first.lsb << "=" << mask.second;
	}
	return ss.str();
}

std::string riscv_meta_model::opcode_format(std::string prefix, riscv_opcode_ptr opcode, char dot, bool key)
{
	std::string name = key ? opcode->key : opcode->name;
	if (name.find("@") == 0) name = name.substr(1);
	std::replace(name.begin(), name.end(), '.', dot);
	return prefix + name;
}

std::string riscv_meta_model::opcode_comment(riscv_opcode_ptr opcode, bool no_comment, bool key)
{
	std::string opcode_name = opcode_format("", opcode, '.', key);
	return no_comment ? "" : format_string("/* %20s */ ", opcode_name.c_str());
}

std::string riscv_meta_model::opcode_isa_shortname(riscv_opcode_ptr opcode)
{
	auto &ext = opcode->extensions.front();
	std::string short_name = ext->prefix;
	short_name += ext->alpha_code;
	return short_name;
}

std::string riscv_meta_model::codec_type_name(riscv_codec_ptr codec)
{
	size_t o = codec->name.find("_");
	if (o == std::string::npos) o = codec->name.find("+");
	if (o == std::string::npos) return codec->name;
	return codec->name.substr(0, o);
}

std::vector<std::string> riscv_meta_model::parse_line(std::string line)
{
	// simple parsing routine that handles tokens separated by whitespace,
	// double quoted tokens containing whitespace and # comments

	std::vector<char> token;
	std::vector<std::string> comps;
	enum {
		whitespace,
		quoted_token,
		unquoted_token,
		comment
	} state = whitespace;

	size_t i = 0;
	while (i < line.size()) {
		char c = line[i];
		switch (state) {
			case whitespace:
				if (::isspace(c)) {
					i++;
				} else if (c == '#') {
					state = comment;
				} else if (c == '"') {
					state = quoted_token;
					i++;
				} else {
					state = unquoted_token;
				}
				break;
			case quoted_token:
				if (c == '"') {
					comps.push_back(std::string(token.begin(), token.end()));
					token.resize(0);
					state = whitespace;
				} else {
					token.push_back(c);
				}
				i++;
				break;
			case unquoted_token:
				if (::isspace(c)) {
					comps.push_back(std::string(token.begin(), token.end()));
					token.resize(0);
					state = whitespace;
				} else {
					token.push_back(c);
				}
				i++;
				break;
			case comment:
				i++;
				break;
		}
	}
	if (token.size() > 0) {
		comps.push_back(std::string(token.begin(), token.end()));
	}
	return comps;
}

std::vector<std::vector<std::string>> riscv_meta_model::read_file(std::string filename)
{
	std::vector<std::vector<std::string>> data;
	std::ifstream in(filename.c_str());
	std::string line;
	if (!in.is_open()) {
		panic("error opening %s\n", filename.c_str());
	}
	while (in.good())
	{
		std::getline(in, line);
		size_t hoffset = line.find("#");
		if (hoffset != std::string::npos) {
			line = ltrim(rtrim(line.substr(0, hoffset)));
		}
		std::vector<std::string> part = parse_line(line);
		if (part.size() == 0) continue;
		data.push_back(part);
	}
	in.close();
	return data;
}


riscv_extension_list riscv_meta_model::decode_isa_extensions(std::string isa_spec)
{
	riscv_extension_list list;
	if (isa_spec.size() == 0) {
		return list;
	}

	// canonicalise isa spec to lower case
	std::transform(isa_spec.begin(), isa_spec.end(), isa_spec.begin(), ::tolower);

	// find isa prefix and width
	ssize_t ext_isa_width = 0;
	std::string ext_prefix, ext_isa_width_str;
	for (auto &ext : extensions) {
		if (isa_spec.find(ext->prefix) == 0) {
			ext_prefix = ext->prefix;
		}
		if (ext_prefix.size() > 0) {
			ext_isa_width_str = std::to_string(ext->isa_width);
			if (isa_spec.find(ext_isa_width_str) == ext_prefix.size()) {
				ext_isa_width = ext->isa_width;
			}
		}
	}
	if (ext_prefix.size() == 0 || ext_isa_width == 0) {
		panic("illegal isa spec: %s", isa_spec.c_str());
	}

	// replace 'g' with 'imafd'
	size_t g_offset = isa_spec.find("g");
	if (g_offset != std::string::npos) {
		isa_spec = isa_spec.replace(isa_spec.begin() + g_offset,
			isa_spec.begin() + g_offset + 1, "imafd");
	}

	// lookup extensions
	ssize_t ext_offset = ext_prefix.length() + ext_isa_width_str.length();
	for (auto i = isa_spec.begin() + ext_offset; i != isa_spec.end(); i++) {
		std::string ext_name = isa_spec.substr(0, ext_offset) + *i;
		auto ext = extensions_by_name[ext_name];
		if (!ext) {
			panic("illegal isa spec: %s: missing extension: %s",
				isa_spec.c_str(), ext_name.c_str());
		}
		if (std::find(list.begin(), list.end(), ext) != list.end()) {
			panic("illegal isa spec: %s: duplicate extension: %s",
				isa_spec.c_str(), ext_name.c_str());
		}
		list.push_back(ext);
	}
	return list;
}

riscv_opcode_ptr riscv_meta_model::create_opcode(std::string opcode_name, std::string extension)
{
	// create key for the opcode
	riscv_opcode_ptr opcode = lookup_opcode_by_key(opcode_name);
	if (opcode) {
		// if the opcode exists rename the previous opcode using isa extension
		opcode->key = opcode_name + "." + opcode->extensions.front()->name;
		opcodes_by_key.erase(opcode_name);
		opcodes_by_key[opcode->key] = opcode;

		// and add the new opcode with its isa extension
		std::string opcode_key = opcode_name + std::string(".") + extension;
		if (opcodes_by_key.find(opcode_key) != opcodes_by_key.end()) {
			panic("opcode with same extension already exists: %s",
				opcode_key.c_str());
		}
		opcode = opcodes_by_key[opcode_key] = std::make_shared<riscv_opcode>(
			opcode_key, opcode_name
		);
		opcodes.push_back(opcode);
		opcode->num = opcodes.size();
	} else {
		opcode = opcodes_by_key[opcode_name] = std::make_shared<riscv_opcode>(
			opcode_name, opcode_name
		);
		opcodes.push_back(opcode);
		opcode->num = opcodes.size();
	}

	// add opcode to the opcode by name list, creating a new list if one doesn't exist
	auto opcode_list_i  = opcodes_by_name.find(opcode_name);
	if (opcode_list_i == opcodes_by_name.end()) {
		opcodes_by_name[opcode_name] = { opcode };
	} else {
		opcode_list_i->second.push_back(opcode);
	}

	return opcode;
}

riscv_opcode_ptr riscv_meta_model::lookup_opcode_by_key(std::string opcode_key)
{
	auto i = opcodes_by_key.find(opcode_key);
	if (i != opcodes_by_key.end()) return i->second;
	return riscv_opcode_ptr();
}

riscv_opcode_list riscv_meta_model::lookup_opcode_by_name(std::string opcode_name)
{
	auto i = opcodes_by_name.find(opcode_name);
	if (i != opcodes_by_name.end()) return i->second;
	return riscv_opcode_list();
}

bool riscv_meta_model::is_arg(std::string mnem)
{
	return (args_by_name.find(mnem) != args_by_name.end());
}

bool riscv_meta_model::is_ignore(std::string mnem)
{
	return (mnem.find("=ignore") != std::string::npos);
}

bool riscv_meta_model::is_mask(std::string mnem)
{
	return (mnem.find("=") != std::string::npos);
}

bool riscv_meta_model::is_codec(std::string mnem)
{
	return (codecs_by_name.find(mnem) != codecs_by_name.end());
}

bool riscv_meta_model::is_extension(std::string mnem)
{
	return (extensions_by_name.find(mnem) != extensions_by_name.end());
}

void riscv_meta_model::parse_arg(std::vector<std::string> &part)
{
	if (part.size() < 6) {
		panic("args requires 6 parameters: %s", join(part, " ").c_str());
	}
	auto arg = args_by_name[part[0]] = std::make_shared<riscv_arg>(
		part[0], part[1], part[2], part[3], part[4], part[5]
	);
	args.push_back(arg);
}

void riscv_meta_model::parse_enum(std::vector<std::string> &part)
{
	if (part.size() < 4) {
		panic("args requires 4 parameters: %s", join(part, " ").c_str());
	}
	auto enumv = enums_by_name[part[0]] = std::make_shared<riscv_enum>(
		part[0], part[1], part[2], part[3]
	);
	enums.push_back(enumv);
}

void riscv_meta_model::parse_type(std::vector<std::string> &part)
{
	if (part.size() < 2) {
		panic("types requires 2 or more parameters: %s", join(part, " ").c_str());
	}
	auto type = types_by_name[part[0]] = std::make_shared<riscv_type>(
		part[0], part[1]
	);
	for (size_t i = 2; i < part.size(); i++) {
		std::vector<std::string> spec = split(part[i], "=", false, false);
		type->parts.push_back(riscv_named_bitspec(riscv_bitspec(spec[0]), spec.size() > 1 ? spec[1] : ""));
	}
	types.push_back(type);
}

void riscv_meta_model::parse_codec(std::vector<std::string> &part)
{
	if (part.size() < 2) {
		panic("codecs requires 2 parameters: %s", join(part, " ").c_str());
	}
	auto codec = codecs_by_name[part[0]] = std::make_shared<riscv_codec>(
		part[0], part[1]
	);
	codecs.push_back(codec);
}

void riscv_meta_model::parse_extension(std::vector<std::string> &part)
{
	if (part.size() < 5) {
		panic("extensions requires 5 parameters: %s", join(part, " ").c_str());
	}
	std::string isa = part[0] + part[1] + part[2];
	auto extension = extensions_by_name[isa] = std::make_shared<riscv_extension>(
		part[0], part[1], part[2], part[3], part[4]
	);
	extensions.push_back(extension);
}

void riscv_meta_model::parse_format(std::vector<std::string> &part)
{
	if (part.size() < 1) {
		panic("formats requires at least 1 parameters: %s", join(part, " ").c_str());
	}
	auto format = formats_by_name[part[0]] = std::make_shared<riscv_format>(
		part[0], part.size() > 1 ? part[1] : ""
	);
	formats.push_back(format);
}

void riscv_meta_model::parse_register(std::vector<std::string> &part)
{
	if (part.size() < 5) {
		panic("registers requires 5 parameters: %s", join(part, " ").c_str());
	}
	auto reg = registers_by_name[part[0]] = std::make_shared<riscv_register>(
		part[0], part[1], part[2], part[3], part[4]
	);
	registers.push_back(reg);
}

void riscv_meta_model::parse_csr(std::vector<std::string> &part)
{
	if (part.size() < 4) {
		panic("csrs requires 4 parameters: %s", join(part, " ").c_str());
	}
	auto csr = csrs_by_name[part[2]] = std::make_shared<riscv_csr>(
		part[0], part[1], part[2], part[3]
	);
	csrs.push_back(csr);
}

void riscv_meta_model::parse_opcode(std::vector<std::string> &part)
{
	std::vector<std::string> extensions;
	for (size_t i = 1; i < part.size(); i++) {
		std::string mnem = part[i];
		std::transform(mnem.begin(), mnem.end(), mnem.begin(), ::tolower);
		if (is_extension(mnem)) {
			extensions.push_back(mnem);
		}
	}

	std::string opcode_name = part[0];
	if (extensions.size() == 0) {
		panic("no extension assigned for opcode: %s", opcode_name.c_str());
	}
	auto opcode = create_opcode(opcode_name, extensions.front());

	for (size_t i = 1; i < part.size(); i++) {
		std::string mnem = part[i];
		std::transform(mnem.begin(), mnem.end(), mnem.begin(), ::tolower);
		if (is_arg(mnem)) {
			opcode->args.push_back(args_by_name[mnem]);
		} else if (is_ignore(mnem)) {
			// presently we ignore masks labeled as ignore
		} else if (is_mask(mnem)) {
			opcode->masks.push_back(decode_mask(mnem));
		} else if (is_codec(mnem)) {
			opcode->codec = codecs_by_name[mnem];
			opcode->format = formats_by_name[opcode->codec->format];
			if (!opcode->format) {
				panic("opcode %s codec %s has unknown format: %s",
					opcode_name.c_str(), opcode->codec->format.c_str());
			}
			std::string type_name = codec_type_name(opcode->codec);
			opcode->type = types_by_name[type_name];
			if (!opcode->type) {
				panic("opcode %s codec %s has unknown type: %s",
					opcode_name.c_str(), opcode->codec->name.c_str(), type_name.c_str());
			}
		} else if (is_extension(mnem)) {
			auto extension = extensions_by_name[mnem];
			opcode->extensions.push_back(extension);
			if (opcode->extensions.size() == 1) {
				extension->opcodes.push_back(opcode);
			}
		} else {
			debug("opcode %s: unknown arg: %s", opcode_name.c_str(), mnem.c_str());
		}
	}

	if (!opcode->codec) {
		panic("opcode has no codec: %s", opcode_name.c_str());
	}
	if (opcode->extensions.size() == 0) {
		panic("opcode has no extensions: %s", opcode_name.c_str());
	}
}

void riscv_meta_model::parse_constraint(std::vector<std::string> &part)
{
	if (part.size() < 2) {
		panic("constraints requires 2 parameters: %s", join(part, " ").c_str());
	}
	auto constraint = constraints_by_name[part[0]] = std::make_shared<riscv_constraint>(
		part[0], part[1]
	);
	constraints.push_back(constraint);
}

void riscv_meta_model::parse_compression(std::vector<std::string> &part)
{
	if (part.size() < 2) {
		panic("invalid compression file requires at least 2 parameters: %s",
			join(part, " ").c_str());
	}
	for (auto comp_opcode : lookup_opcode_by_name(part[0])) {
		for (auto opcode : lookup_opcode_by_name(part[1])) {
			riscv_constraint_list constraint_list;
			for (size_t i = 2; i < part.size(); i++) {
				auto ci = constraints_by_name.find(part[i]);
				if (ci == constraints_by_name.end()) {
					panic("compressed opcode %s references unknown constraint %s",
						part[0].c_str(), part[i].c_str());
				}
				constraint_list.push_back(ci->second);
			}
			auto comp = std::make_shared<riscv_compressed>(
				comp_opcode, opcode, constraint_list
			);
			comp_opcode->compressed = comp;
			opcode->compressions.push_back(comp);
			compressions.push_back(comp);
		}
	}
}

void riscv_meta_model::parse_instruction(std::vector<std::string> &part)
{
	if (part.size() < 2) return;
	std::string opcode_name = part[0];
	std::string opcode_long_name = part[1];
	std::string opcode_instruction = part.size() > 2 ? part[2] : "";
	for (auto opcode : lookup_opcode_by_name(opcode_name)) {
		opcode->long_name = opcode_long_name;
		opcode->instruction = opcode_instruction;
	}
}

void riscv_meta_model::parse_description(std::vector<std::string> &part)
{
	if (part.size() < 1) return;
	std::string opcode_name = part[0];
	std::string opcode_description = part.size() > 1 ? part[1] : "";
	for (auto opcode : lookup_opcode_by_name(opcode_name)) {
		opcode->description = opcode_description;
	}
}

bool riscv_meta_model::read_metadata(std::string dirname)
{
	for (auto part : read_file(dirname + std::string("/") + ARGS_FILE)) parse_arg(part);
	for (auto part : read_file(dirname + std::string("/") + ENUMS_FILE)) parse_enum(part);
	for (auto part : read_file(dirname + std::string("/") + TYPES_FILE)) parse_type(part);
	for (auto part : read_file(dirname + std::string("/") + FORMATS_FILE)) parse_format(part);
	for (auto part : read_file(dirname + std::string("/") + CODECS_FILE)) parse_codec(part);
	for (auto part : read_file(dirname + std::string("/") + EXTENSIONS_FILE)) parse_extension(part);
	for (auto part : read_file(dirname + std::string("/") + REGISTERS_FILE)) parse_register(part);
	for (auto part : read_file(dirname + std::string("/") + CSRS_FILE)) parse_csr(part);
	for (auto part : read_file(dirname + std::string("/") + OPCODES_FILE)) parse_opcode(part);
	for (auto part : read_file(dirname + std::string("/") + CONSTRAINTS_FILE)) parse_constraint(part);
	for (auto part : read_file(dirname + std::string("/") + COMPRESSION_FILE)) parse_compression(part);
	for (auto part : read_file(dirname + std::string("/") + INSTRUCTIONS_FILE)) parse_instruction(part);
	for (auto part : read_file(dirname + std::string("/") + DESCRIPTIONS_FILE)) parse_description(part);
	return true;
}
