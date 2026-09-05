// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff, Manuel Abadia, Couriersud
//
// Portable MCS-51 Family Emulator, extracted from MAME 0.288
// (src/devices/cpu/mcs51/{i8051,i8052,i80c51,i80c52}.{h,cpp}).
//
// This is the Intel 80C32 configuration only: no internal ROM, 256 bytes of
// internal RAM, Timer 2, power-down support. Everything the 80C32 does not use
// -- other family variants, the DS5002FP, the disassembler, MAME's device,
// address-space, save-state and debugger machinery -- has been dropped, and the
// virtual dispatch the variants needed has been folded into direct calls.
//
// The opcode implementations in mcs51ops.cpp are used verbatim from MAME, so
// the member names they touch (m_program, m_idata, m_sfr, m_xdata, read_direct,
// ...) are preserved here.

#ifndef MINITEL_MCS51_H
#define MINITEL_MCS51_H

#pragma once

#include "types.h"

#include <functional>

enum
{
	MCS51_INT0_LINE = 0, // P3.2: External Interrupt 0
	MCS51_INT1_LINE,     // P3.3: External Interrupt 1
	MCS51_T0_LINE,       // P3.4: Timer 0 External Input
	MCS51_T1_LINE,       // P3.5: Timer 1 External Input
	MCS51_T2_LINE,       // P1.0: Timer 2 External Input
	MCS51_T2EX_LINE      // P1.1: Timer 2 Capture Reload Trigger
};

class mcs51_cpu_device
{
public:
	mcs51_cpu_device();

	// host wiring: the machine supplies program/xdata access and port I/O
	std::function<u8 (offs_t)>      program_r;
	std::function<u8 (offs_t)>      xdata_r;
	std::function<void (offs_t, u8)> xdata_w;
	std::function<u8 ()>            port_in_cb[4];
	std::function<void (u8)>        port_out_cb[4];

	void reset();

	// run until the cycle budget is exhausted; returns cycles actually consumed
	int execute_run(int cycles);

	void set_input_line(int irqline, int state);

	u16 pc() const { return m_pc; }

	// At least CMOS devices may be forced to read from ports configured as
	// output. All you need is a low impedance output connected to the port.
	void set_port_forced_input(u8 port, u8 forced_input) { m_forced_inputs[port] = forced_input; }

private:
	enum {
		PSW_CY = 7, PSW_AC = 6, PSW_FO = 5, PSW_RS = 3, PSW_OV = 2, PSW_P = 0
	};

	enum {
		IE_A = 7, IE_T2 = 6, IE_S = 4, IE_T1 = 3, IE_X1 = 2, IE_T0 = 1, IE_X0 = 0
	};

	enum {
		TCON_TF1 = 7, TCON_TR1 = 6, TCON_TF0 = 5, TCON_TR0 = 4,
		TCON_IE1 = 3, TCON_IT1 = 2, TCON_IE0 = 1, TCON_IT0 = 0
	};

	enum {
		SCON_SM0 = 7, SCON_SM1 = 6, SCON_SM2 = 5, SCON_REN = 4,
		SCON_TB8 = 3, SCON_RB8 = 2, SCON_TI = 1, SCON_RI = 0
	};

	enum {
		TMOD_GATE1 = 7, TMOD_CT1 = 6, TMOD_M1_1 = 5, TMOD_M1_0 = 4,
		TMOD_GATE0 = 3, TMOD_CT0 = 2, TMOD_M0_1 = 1, TMOD_M0_0 = 0
	};

	enum {
		PCON_SMOD = 7, PCON_GF1 = 3, PCON_GF0 = 2, PCON_PD = 1, PCON_IDL = 0
	};

	enum {
		T2CON_TF2 = 7, T2CON_EXF2 = 6, T2CON_RCLK = 5, T2CON_TCLK = 4,
		T2CON_EXEN2 = 3, T2CON_TR2 = 2, T2CON_CT2 = 1, T2CON_CP = 0
	};

	// The 80C32 has no internal ROM and 256 bytes of internal RAM.
	static constexpr int RAM_MASK = 0xff;
	static constexpr int NUM_INTERRUPTS = 6;

	// Internal stuff
	u16 m_ppc = 0;         // previous pc
	u16 m_pc = 0;          // current pc
	u8 m_rwm = 0;          // current instruction is a read/write/modify instruction

	int m_inst_cycles = 0; // cycles for the current instruction (temporary)
	u32 m_last_line_state = 0;
	int m_t0_cnt = 0;      // number of 0->1 transitions on T0 line
	int m_t1_cnt = 0;
	int m_t2_cnt = 0;
	int m_t2ex_cnt = 0;
	int m_cur_irq_prio = -1;
	u8 m_irq_active = 0;
	u8 m_irq_prio[8] = { 0 };

	u8 m_forced_inputs[4] = { 0 };

	// JB-related hacks
	u8 m_last_op = 0;
	u8 m_last_bit = 0;

	int m_icount = 0;

	struct mcs51_uart
	{
		u8 data_out;
		u8 data_in;
		u8 txbit;
		u8 txd;
		u8 rxbit;
		u8 rxb8;

		int smod_div;  // signal divided by 2^SMOD
		int rx_clk;
		int tx_clk;
	} m_uart = {};

	// Registers
	u16 m_dptr = 0;
	u8 m_acc = 0, m_psw = 0, m_b = 0, m_sp = 0, m_pcon = 0, m_tcon = 0, m_tmod = 0;
	u8 m_scon = 0, m_sbuf = 0, m_ie = 0, m_ip = 0, m_iph = 0;
	u8 m_p0 = 0, m_p1 = 0, m_p2 = 0, m_p3 = 0;
	u8 m_tl0 = 0, m_tl1 = 0, m_th0 = 0, m_th1 = 0;

	// 8052 extras
	u16 m_rcap2 = 0, m_t2 = 0;
	u8 m_t2con = 0;

	// 80C52 extras
	u8 m_saddr = 0, m_saden = 0;

	u8 m_internal_ram[RAM_MASK + 1] = { 0 };

	// ---------------------------------------------------------------------
	// Stand-ins for the MAME address spaces the opcode handlers reach through.
	// Keeping the names and the read_byte/write_byte shape lets mcs51ops.cpp
	// be used exactly as it appears in MAME.
	// ---------------------------------------------------------------------
	struct program_space
	{
		mcs51_cpu_device *cpu;
		u8 read_byte(offs_t a) const { return cpu->program_r(a); }
	} m_program{ this };

	struct xdata_space
	{
		mcs51_cpu_device *cpu;
		u8 read_byte(offs_t a) const { return cpu->xdata_r(a); }
		void write_byte(offs_t a, u8 d) const { cpu->xdata_w(a, d); }
	} m_xdata{ this };

	struct sfr_space
	{
		mcs51_cpu_device *cpu;
		u8 read_byte(offs_t a) const { return cpu->sfr_read(a); }
		void write_byte(offs_t a, u8 d) const { cpu->sfr_write(a, d); }
	} m_sfr{ this };

	struct idata_space
	{
		mcs51_cpu_device *cpu;
		u8 read_byte(offs_t a) const { return cpu->m_internal_ram[a & RAM_MASK]; }
		void write_byte(offs_t a, u8 d) const { cpu->m_internal_ram[a & RAM_MASK] = d; }
	} m_idata{ this };

	u8 sfr_read(u8 r);
	void sfr_write(u8 r, u8 data);

	u8 read_direct(u8 r) { return r < 0x80 ? m_idata.read_byte(r) : m_sfr.read_byte(r); }
	void write_direct(u8 r, u8 data) { if (r < 0x80) m_idata.write_byte(r, data); else m_sfr.write_byte(r, data); }

	// SFR accessors
	void psw_w(u8 data) { m_psw = (m_psw & 0x01) | (data & 0xfe); }
	void acc_w(u8 data);
	void dptr_w(offs_t offset, u8 data) { m_dptr = (m_dptr & ~(0xff << (offset * 8))) | (data << (offset * 8)); }
	void scon_w(u8 data);
	void sbuf_w(u8 data);
	void ip_w(u8 data) { m_ip = data; update_irq_prio(); }
	void iph_w(u8 data) { m_iph = data; update_irq_prio(); }

	u8 p0_r() { return m_rwm ? m_p0 : (m_p0 | m_forced_inputs[0]) & port_in_cb[0](); }
	void p0_w(u8 data) { m_p0 = data; port_out_cb[0](m_p0); }
	u8 p1_r() { return m_rwm ? m_p1 : (m_p1 | m_forced_inputs[1]) & port_in_cb[1](); }
	void p1_w(u8 data) { m_p1 = data; port_out_cb[1](m_p1); }
	u8 p2_r() { return m_rwm ? m_p2 : (m_p2 | m_forced_inputs[2]) & port_in_cb[2](); }
	void p2_w(u8 data) { m_p2 = data; port_out_cb[2](m_p2); }
	u8 p3_r();
	void p3_w(u8 data);

	template<int bit> void set_bit(u8 &reg, bool state) {
		if (state)
			reg |= 1 << bit;
		else
			reg &= ~(1 << bit);
	}

	void set_cy (bool state) { set_bit<PSW_CY>(m_psw, state); }
	void set_ac (bool state) { set_bit<PSW_AC>(m_psw, state); }
	void set_fo (bool state) { set_bit<PSW_FO>(m_psw, state); }
	void set_rs (u8 state)   { m_psw = (m_psw & ~(3 << PSW_RS)) | (state << PSW_RS); }
	void set_ov (bool state) { set_bit<PSW_OV>(m_psw, state); }
	void set_p  (bool state) { set_bit<PSW_P >(m_psw, state); }

	void set_tf1(bool state) { set_bit<TCON_TF1>(m_tcon, state); }
	void set_tr1(bool state) { set_bit<TCON_TR1>(m_tcon, state); }
	void set_tf0(bool state) { set_bit<TCON_TF0>(m_tcon, state); }
	void set_tr0(bool state) { set_bit<TCON_TR0>(m_tcon, state); }
	void set_ie1(bool state) { set_bit<TCON_IE1>(m_tcon, state); }
	void set_it1(bool state) { set_bit<TCON_IT1>(m_tcon, state); }
	void set_ie0(bool state) { set_bit<TCON_IE0>(m_tcon, state); }
	void set_it0(bool state) { set_bit<TCON_IT0>(m_tcon, state); }

	void set_rb8(bool state) { set_bit<SCON_RB8>(m_scon, state); }
	void set_ti (bool state) { set_bit<SCON_TI >(m_scon, state); }
	void set_ri (bool state) { set_bit<SCON_RI >(m_scon, state); }

	void set_pd (bool state) { set_bit<PCON_PD >(m_pcon, state); }
	void set_idl(bool state) { set_bit<PCON_IDL>(m_pcon, state); }

	void set_tf2 (bool state) { set_bit<T2CON_TF2 >(m_t2con, state); }
	void set_exf2(bool state) { set_bit<T2CON_EXF2>(m_t2con, state); }

	void transmit(int state);

	static const u8 mcs51_cycles[256];
	static const u8 parity_value[256];

	void clear_current_irq();
	offs_t external_ram_iaddr(offs_t offset, offs_t mem_mask);
	void handle_8bit_uart_clock(int source);
	void handle_irq(int irqline, int state, u32 new_state, u32 tr_state);

	void push_pc();
	void pop_pc();
	u8 bit_address_r(u8 offset);
	void bit_address_w(u8 offset, u8 bit);
	void do_add_flags(u8 a, u8 data, u8 c);
	void do_sub_flags(u8 a, u8 data, u8 c);
	void transmit_receive(int source);
	void update_timer_t0(int cycles);
	void update_timer_t1(int cycles);
	void update_timer_t2(int cycles);
	void update_irq_prio();
	void execute_op(u8 op);
	void check_irqs();
	void burn_cycles(int cycles);
	void acall(u8 r);
	void set_reg(u8 r, u8 v);
	u8 r_reg(u8 r);

	// opcode handlers (mcs51ops.cpp, verbatim from MAME)
	void add_a_byte(u8 r);
	void add_a_mem(u8 r);
	void add_a_ir(u8 r);
	void add_a_r(u8 r);
	void addc_a_byte(u8 r);
	void addc_a_mem(u8 r);
	void addc_a_ir(u8 r);
	void addc_a_r(u8 r);
	void ajmp(u8 r);
	void anl_mem_a(u8 r);
	void anl_mem_byte(u8 r);
	void anl_a_byte(u8 r);
	void anl_a_mem(u8 r);
	void anl_a_ir(u8 r);
	void anl_a_r(u8 r);
	void anl_c_bitaddr(u8 r);
	void anl_c_nbitaddr(u8 r);
	void cjne_a_byte(u8 r);
	void cjne_a_mem(u8 r);
	void cjne_ir_byte(u8 r);
	void cjne_r_byte(u8 r);
	void clr_bitaddr(u8 r);
	void clr_c(u8 r);
	void clr_a(u8 r);
	void cpl_bitaddr(u8 r);
	void cpl_c(u8 r);
	void cpl_a(u8 r);
	void da_a(u8 r);
	void dec_a(u8 r);
	void dec_mem(u8 r);
	void dec_ir(u8 r);
	void dec_r(u8 r);
	void div_ab(u8 r);
	void djnz_mem(u8 r);
	void djnz_r(u8 r);
	void inc_a(u8 r);
	void inc_mem(u8 r);
	void inc_ir(u8 r);
	void inc_r(u8 r);
	void inc_dptr(u8 r);
	void jb(u8 r);
	void jbc(u8 r);
	void jc(u8 r);
	void jmp_iadptr(u8 r);
	void jnb(u8 r);
	void jnc(u8 r);
	void jnz(u8 r);
	void jz(u8 r);
	void lcall(u8 r);
	void ljmp(u8 r);
	void mov_a_byte(u8 r);
	void mov_a_mem(u8 r);
	void mov_a_ir(u8 r);
	void mov_a_r(u8 r);
	void mov_mem_byte(u8 r);
	void mov_mem_mem(u8 r);
	void mov_ir_byte(u8 r);
	void mov_r_byte(u8 r);
	void mov_mem_ir(u8 r);
	void mov_mem_r(u8 r);
	void mov_dptr_byte(u8 r);
	void mov_bitaddr_c(u8 r);
	void mov_ir_mem(u8 r);
	void mov_r_mem(u8 r);
	void mov_mem_a(u8 r);
	void mov_ir_a(u8 r);
	void mov_r_a(u8 r);
	void movc_a_iapc(u8 r);
	void mov_c_bitaddr(u8 r);
	void movc_a_iadptr(u8 r);
	void movx_a_idptr(u8 r);
	void movx_a_ir(u8 r);
	void movx_idptr_a(u8 r);
	void movx_ir_a(u8 r);
	void mul_ab(u8 r);
	void nop(u8 r);
	void orl_mem_a(u8 r);
	void orl_mem_byte(u8 r);
	void orl_a_byte(u8 r);
	void orl_a_mem(u8 r);
	void orl_a_ir(u8 r);
	void orl_a_r(u8 r);
	void orl_c_bitaddr(u8 r);
	void orl_c_nbitaddr(u8 r);
	void pop(u8 r);
	void push(u8 r);
	void ret(u8 r);
	void reti(u8 r);
	void rl_a(u8 r);
	void rlc_a(u8 r);
	void rr_a(u8 r);
	void rrc_a(u8 r);
	void setb_c(u8 r);
	void setb_bitaddr(u8 r);
	void sjmp(u8 r);
	void subb_a_byte(u8 r);
	void subb_a_mem(u8 r);
	void subb_a_ir(u8 r);
	void subb_a_r(u8 r);
	void swap_a(u8 r);
	void xch_a_mem(u8 r);
	void xch_a_ir(u8 r);
	void xch_a_r(u8 r);
	void xchd_a_ir(u8 r);
	void xrl_mem_a(u8 r);
	void xrl_mem_byte(u8 r);
	void xrl_a_byte(u8 r);
	void xrl_a_mem(u8 r);
	void xrl_a_ir(u8 r);
	void xrl_a_r(u8 r);
	void illegal(u8 r);
};

#endif // MINITEL_MCS51_H
