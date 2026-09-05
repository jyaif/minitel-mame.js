// license:BSD-3-Clause
// copyright-holders:Steve Ellenoff, Manuel Abadia, Couriersud
//
// Portable MCS-51 Family Emulator, extracted from MAME 0.288 and specialised
// for the Intel 80C32. See mcs51.h for what was dropped.
//
// The term cycles is used here to really refer to clock oscillations, because
// 1 machine cycle actually takes 12 oscillations.

#include "mcs51.h"

// m_pc vectors
enum
{
	V_RESET = 0x000, // power on address
	V_IE0   = 0x003, // External Interrupt 0
	V_TF0   = 0x00b, // Timer 0 Overflow
	V_IE1   = 0x013, // External Interrupt 1
	V_TF1   = 0x01b, // Timer 1 Overflow
	V_RITI  = 0x023, // Serial Receive/Transmit
	V_TF2   = 0x02b  // Timer 2 Overflow (8052 only)
};

enum serial_state : u8
{
	SIO_IDLE,
	SIO_START_LE,
	SIO_START,
	SIO_DATA0,
	SIO_DATA1,
	SIO_DATA2,
	SIO_DATA3,
	SIO_DATA4,
	SIO_DATA5,
	SIO_DATA6,
	SIO_DATA7,
	SIO_DATA8,
	SIO_STOP,
};


mcs51_cpu_device::mcs51_cpu_device()
{
	// default to standard cmos interfacing
	for (auto &elem : m_forced_inputs)
		elem = 0;

	// unhooked ports read as all ones and swallow writes
	for (int i = 0; i < 4; i++)
	{
		port_in_cb[i] = []() -> u8 { return 0xff; };
		port_out_cb[i] = [](u8) { };
	}
}


/***************************************************************************
    SPECIAL FUNCTION REGISTERS

    Flattened from the address_map chain i80c52 -> i8052 -> mcs51. Registers
    the 80C32 does not decode read back as 0, matching an unmapped read in
    MAME's SFR address space.
***************************************************************************/

u8 mcs51_cpu_device::sfr_read(u8 r)
{
	switch (r)
	{
		case 0x80: return p0_r();
		case 0x81: return m_sp;
		case 0x82: return m_dptr & 0xff;
		case 0x83: return m_dptr >> 8;
		case 0x87: return m_pcon;
		case 0x88: return m_tcon;
		case 0x89: return m_tmod;
		case 0x8a: return m_tl0;
		case 0x8b: return m_tl1;
		case 0x8c: return m_th0;
		case 0x8d: return m_th1;
		case 0x90: return p1_r();
		case 0x98: return m_scon;
		case 0x99: return m_sbuf;
		case 0xa0: return p2_r();
		case 0xa8: return m_ie;
		case 0xa9: return m_saddr;
		case 0xb0: return p3_r();
		case 0xb7: return m_iph;
		case 0xb8: return m_ip;
		case 0xb9: return m_saden;
		case 0xc8: return m_t2con;
		case 0xca: return m_rcap2 & 0xff;
		case 0xcb: return m_rcap2 >> 8;
		case 0xcc: return m_t2 & 0xff;
		case 0xcd: return m_t2 >> 8;
		case 0xd0: return m_psw;
		case 0xe0: return m_acc;
		case 0xf0: return m_b;
		default:   return 0;
	}
}

void mcs51_cpu_device::sfr_write(u8 r, u8 data)
{
	switch (r)
	{
		case 0x80: p0_w(data); break;
		case 0x81: m_sp = data; break;
		case 0x82: dptr_w(0, data); break;
		case 0x83: dptr_w(1, data); break;
		case 0x87: m_pcon = data; break;
		case 0x88: m_tcon = data; break;
		case 0x89: m_tmod = data; break;
		case 0x8a: m_tl0 = data; break;
		case 0x8b: m_tl1 = data; break;
		case 0x8c: m_th0 = data; break;
		case 0x8d: m_th1 = data; break;
		case 0x90: p1_w(data); break;
		case 0x98: scon_w(data); break;
		case 0x99: sbuf_w(data); break;
		case 0xa0: p2_w(data); break;
		case 0xa8: m_ie = data; break;
		case 0xa9: m_saddr = data; break;
		case 0xb0: p3_w(data); break;
		case 0xb7: iph_w(data); break;
		case 0xb8: ip_w(data); break;
		case 0xb9: m_saden = data; break;
		case 0xc8: m_t2con = data; break;
		case 0xca: m_rcap2 = (m_rcap2 & 0xff00) | data; break;
		case 0xcb: m_rcap2 = (m_rcap2 & 0x00ff) | (data << 8); break;
		case 0xcc: m_t2 = (m_t2 & 0xff00) | data; break;
		case 0xcd: m_t2 = (m_t2 & 0x00ff) | (data << 8); break;
		case 0xd0: psw_w(data); break;
		case 0xe0: acc_w(data); break;
		case 0xf0: m_b = data; break;
		default:   break; // AUXR (0x8e), AUXR1 (0xa2) and undecoded addresses
	}
}

void mcs51_cpu_device::scon_w(u8 data)
{
	u8 old = m_scon;
	m_scon = data;
	if (!BIT(old, 4) && BIT(m_scon, 4))
	{
		if (!BIT(m_scon, 6, 2))
			logerror("mode 0 serial input is not emulated\n");
		m_uart.rxbit = SIO_IDLE;
	}
}

void mcs51_cpu_device::sbuf_w(u8 data)
{
	m_sbuf = data;
	m_uart.data_out = m_sbuf;
	m_uart.txbit = SIO_START;
}

u8 mcs51_cpu_device::p3_r()
{
	return m_rwm ? m_p3 :
		(m_p3 | m_forced_inputs[3]) & port_in_cb[3]()
		& ~(BIT(m_last_line_state, unsigned(MCS51_INT0_LINE)) ? 4 : 0)
		& ~(BIT(m_last_line_state, unsigned(MCS51_INT1_LINE)) ? 8 : 0);
}

void mcs51_cpu_device::p3_w(u8 data)
{
	m_p3 = data;
	// P3.1 = SFR(P3) & TxD
	if (!m_uart.txd)
		port_out_cb[3](m_p3 & ~0x02);
	else
		port_out_cb[3](m_p3);
}


void mcs51_cpu_device::clear_current_irq()
{
	if (m_cur_irq_prio >= 0)
		m_irq_active &= ~(1 << m_cur_irq_prio);
	if (m_irq_active & 4)
		m_cur_irq_prio = 2;
	else if (m_irq_active & 2)
		m_cur_irq_prio = 1;
	else if (m_irq_active & 1)
		m_cur_irq_prio = 0;
	else
		m_cur_irq_prio = -1;
}

/* Generate an external ram address for read/writing using indirect addressing
   mode.

   The lowest 8 bits of the address are passed in (from the R0/R1 register).
   During 16bit access the high order byte of the address is output on port 2,
   so we assume that most hardware will use port 2 for 8bit access as well. */

offs_t mcs51_cpu_device::external_ram_iaddr(offs_t offset, offs_t mem_mask)
{
	if (mem_mask == 0x00ff)
		return (offset & 0x00ff) | (m_p2 << 8);
	return offset;
}

void mcs51_cpu_device::transmit(int state)
{
	if (m_uart.txd != state)
	{
		m_uart.txd = state;

		// P3.1 = SFR(P3) & TxD
		if (BIT(m_p3, 1))
		{
			if (state)
				port_out_cb[3](m_p3);
			else
				port_out_cb[3](m_p3 & ~0x02);
		}
	}
}

// 8052 variant: timer 2 can also drive the baud rate
void mcs51_cpu_device::handle_8bit_uart_clock(int source)
{
	if (source == 1)
	{
		m_uart.tx_clk += (BIT(m_t2con, T2CON_TCLK) ? 0 : !m_uart.smod_div);
		m_uart.rx_clk += (BIT(m_t2con, T2CON_RCLK) ? 0 : !m_uart.smod_div);
	}
	if (source == 2)
	{
		m_uart.tx_clk += (BIT(m_t2con, T2CON_TCLK) ? 1 : 0);
		m_uart.rx_clk += (BIT(m_t2con, T2CON_RCLK) ? 1 : 0);
	}
}

void mcs51_cpu_device::transmit_receive(int source)
{
	int mode = (BIT(m_scon, SCON_SM0) << 1) | BIT(m_scon, SCON_SM1);

	if (source == 1) // timer1
		m_uart.smod_div = (m_uart.smod_div + 1) & !BIT(m_pcon, PCON_SMOD);

	switch (mode)
	{
		// 8 bit shifter - rate set by clock freq / 12
		case 0:
			if (source == 0)
			{
				// TODO: mode 0 serial input is unemulated
				// FIXME: output timing is highly simplified and incorrect
				switch (m_uart.txbit)
				{
				case SIO_IDLE:
					break;
				case SIO_START:
					m_p3 |= 0x03;
					port_out_cb[3](m_p3);
					m_uart.txbit = SIO_DATA0;
					break;
				case SIO_DATA0: case SIO_DATA1: case SIO_DATA2: case SIO_DATA3:
				case SIO_DATA4: case SIO_DATA5: case SIO_DATA6: case SIO_DATA7:
					m_p3 &= ~0x03;
					if (BIT(m_uart.data_out, m_uart.txbit - SIO_DATA0))
						m_p3 |= 1U << 0;
					port_out_cb[3](m_p3);

					if (m_uart.txbit == SIO_DATA7)
					{
						set_ti(1);
						m_uart.txbit = SIO_STOP;
					}
					else
						m_uart.txbit++;
					break;
				case SIO_STOP:
					m_p3 |= 0x03;
					port_out_cb[3](m_p3);
					m_uart.txbit = SIO_IDLE;
					break;
				}
			}
			return;
		// 8 bit uart (+ start,stop bit) - baud set by timer1 or timer2
		case 1:
		case 3:
			handle_8bit_uart_clock(source);
			break;
		// 9 bit uart
		case 2:
			m_uart.rx_clk += (source == 0) ? (BIT(m_pcon, PCON_SMOD) ? 6 : 3) : 0;
			m_uart.tx_clk += (source == 0) ? (BIT(m_pcon, PCON_SMOD) ? 6 : 3) : 0;
			break;
	}

	// transmit
	if (m_uart.tx_clk >= 16)
	{
		m_uart.tx_clk &= 0x0f;

		switch (m_uart.txbit)
		{
		case SIO_IDLE:
			transmit(1);
			break;
		case SIO_START:
			transmit(0);
			m_uart.txbit = SIO_DATA0;
			break;
		case SIO_DATA0: case SIO_DATA1: case SIO_DATA2: case SIO_DATA3:
		case SIO_DATA4: case SIO_DATA5: case SIO_DATA6: case SIO_DATA7:
			transmit(BIT(m_uart.data_out, m_uart.txbit - SIO_DATA0));

			// mode 1 has no data8/parity bit
			if (mode == 1 && m_uart.txbit == SIO_DATA7)
				m_uart.txbit = SIO_STOP;
			else
				m_uart.txbit++;
			break;
		case SIO_DATA8: // data8/parity bit
			transmit(BIT(m_scon, SCON_TB8));
			m_uart.txbit = SIO_STOP;
			break;
		case SIO_STOP:
			transmit(1);
			set_ti(1);
			m_uart.txbit = SIO_IDLE;
			break;
		}
	}

	// receive
	if (m_uart.rx_clk >= 16 || m_uart.rxbit < SIO_START)
	{
		m_uart.rx_clk &= 0x0f;

		if (BIT(m_scon, SCON_REN))
		{
			// directly read RXD input
			int const data = BIT(port_in_cb[3](), 0);

			switch (m_uart.rxbit)
			{
			case SIO_IDLE:
				if (data)
					m_uart.rxbit = SIO_START_LE;
				break;
			case SIO_START_LE: // start bit leading edge
				if (!data)
				{
					m_uart.rxbit = SIO_START;
					m_uart.rx_clk = 8;
				}
				break;
			case SIO_START:
				if (!data)
				{
					m_uart.data_in = 0;
					m_uart.rxbit = SIO_DATA0;
				}
				else
				{
					// false start bit
					m_uart.rxbit = SIO_START_LE;
				}
				break;
			case SIO_DATA0: case SIO_DATA1: case SIO_DATA2: case SIO_DATA3:
			case SIO_DATA4: case SIO_DATA5: case SIO_DATA6: case SIO_DATA7:
				if (data)
					m_uart.data_in |= 1U << (m_uart.rxbit - SIO_DATA0);

				// mode 1 has no data8/parity bit
				if (mode == 1 && m_uart.rxbit == SIO_DATA7)
					m_uart.rxbit = SIO_STOP;
				else
					m_uart.rxbit++;
				break;
			case SIO_DATA8: // data8/parity bit
				m_uart.rxb8 = data;
				m_uart.rxbit = SIO_STOP;
				break;
			case SIO_STOP:
				if (!BIT(m_scon, SCON_RI))
				{
					switch (mode)
					{
					case 1:
						m_sbuf = m_uart.data_in;
						if (!BIT(m_scon, SCON_SM2))
						{
							// RB8 contains stop bit
							set_rb8(data);
							set_ri(1);
						}
						else if (data)
						{
							// RI if valid stop bit
							set_ri(1);
						}
						break;
					case 2:
					case 3:
						m_sbuf = m_uart.data_in;
						set_rb8(m_uart.rxb8);

						// no RI if SM2 && !RB8
						if (!BIT(m_scon, SCON_SM2) || BIT(m_scon, SCON_RB8))
							set_ri(1);
						break;
					}
				}

				// next state depends on stop bit validity
				if (data)
					m_uart.rxbit = SIO_START_LE;
				else
					m_uart.rxbit = SIO_IDLE;
				break;
			}
		}
	}
}


void mcs51_cpu_device::update_timer_t0(int cycles)
{
	int mode = (BIT(m_tmod, TMOD_M0_1) << 1) | BIT(m_tmod, TMOD_M0_0);
	u32 count;

	if (BIT(m_tcon, TCON_TR0))
	{
		// counter / external input
		u32 delta = BIT(m_tmod, TMOD_CT0) ? m_t0_cnt : cycles;

		// taken, reset
		m_t0_cnt = 0;

		/* TODO: Not sure about IE0. The manual specifies INT0=high,
		 * which in turn means CLEAR_LINE.
		 * IE0 may be edge triggered depending on IT0 */
		if (BIT(m_tmod, TMOD_GATE0) && !BIT(m_tcon, TCON_IE0))
			delta = 0;

		switch (mode)
		{
			case 0: // 13 Bit Timer Mode
				count = ((m_th0 << 5) | (m_tl0 & 0x1f));
				count += delta;
				if (count & 0xffffe000) // Check for overflow
					set_tf0(1);
				m_th0 = (count >> 5) & 0xff;
				m_tl0 = count & 0x1f;
				break;
			case 1: // 16 Bit Timer Mode
				count = ((m_th0 << 8) | m_tl0);
				count += delta;
				if (count & 0xffff0000) // Check for overflow
					set_tf0(1);
				m_th0 = (count >> 8) & 0xff;
				m_tl0 = count & 0xff;
				break;
			case 2: // 8 Bit Autoreload
				count = ((u32)m_tl0) + delta;
				if (count & 0xffffff00) // Check for overflow
				{
					set_tf0(1);
					count += m_th0; // Reload timer
				}
				// Update new values of the counter
				m_tl0 = count & 0xff;
				break;
			case 3:
				// Split Timer 1
				count = ((u32)m_tl0) + delta;
				if (count & 0xffffff00) // Check for overflow
					set_tf0(1);
				m_tl0 = count & 0xff; // Update new values of the counter
				break;
		}
	}
	if (BIT(m_tcon, TCON_TR1))
	{
		switch (mode)
		{
		case 3:
			// Split Timer 2
			count = ((u32)m_th0) + cycles; // No gate control or counting !
			if (count & 0xffffff00) // Check for overflow
				set_tf1(1);
			m_th0 = count & 0xff; // Update new values of the counter
			break;
		}
	}
}

/*
Intel documentation:
Timer 1 may still be used in modes 0, 1, and 2, while timer 0 is in mode 3.
With one important exception: No interrupts will be generated by timer 1 while
timer 0 is using the TF1 overflow flag
*/

void mcs51_cpu_device::update_timer_t1(int cycles)
{
	u8 mode = (BIT(m_tmod, TMOD_M1_1) << 1) | BIT(m_tmod, TMOD_M1_0);
	u8 mode_0 = (BIT(m_tmod, TMOD_M0_1) << 1) | BIT(m_tmod, TMOD_M0_0);
	u32 count;

	if (mode_0 != 3)
	{
		if (BIT(m_tcon, TCON_TR1))
		{
			u32 overflow = 0;

			// counter / external input
			u32 delta = BIT(m_tmod, TMOD_CT1) ? m_t1_cnt : cycles;

			// taken, reset
			m_t1_cnt = 0;

			if (BIT(m_tmod, TMOD_GATE1) && !BIT(m_tcon, TCON_IE1))
				delta = 0;

			switch (mode)
			{
				case 0: // 13 Bit Timer Mode
					count = ((m_th1 << 5) | (m_tl1 & 0x1f));
					count += delta;
					overflow = count & 0xffffe000; // Check for overflow
					m_th1 = (count >> 5) & 0xff;
					m_tl1 = count & 0x1f;
					break;
				case 1: // 16 Bit Timer Mode
					count = ((m_th1 << 8) | m_tl1);
					count += delta;
					overflow = count & 0xffff0000; // Check for overflow
					m_th1 = (count >> 8) & 0xff;
					m_tl1 = count & 0xff;
					break;
				case 2: // 8 Bit Autoreload
					count = ((u32)m_tl1) + delta;
					overflow = count & 0xffffff00; // Check for overflow
					if (overflow)
						count += m_th1; // Reload timer
					// Update new values of the counter
					m_tl1 = count & 0xff;
					break;
				case 3:
					// do nothing
					break;
			}
			if (overflow)
			{
				set_tf1(1);
				transmit_receive(1);
			}
		}
	}
	else
	{
		u32 overflow = 0;
		u32 delta = cycles;

		// taken, reset
		m_t1_cnt = 0;

		switch (mode)
		{
			case 0: // 13 Bit Timer Mode
				count = ((m_th1 << 5) | (m_tl1 & 0x1f));
				count += delta;
				overflow = count & 0xffffe000; // Check for overflow
				m_th1 = (count >> 5) & 0xff;
				m_tl1 = count & 0x1f;
				break;
			case 1: // 16 Bit Timer Mode
				count = ((m_th1 << 8) | m_tl1);
				count += delta;
				overflow = count & 0xffff0000; // Check for overflow
				m_th1 = (count >> 8) & 0xff;
				m_tl1 = count & 0xff;
				break;
			case 2: // 8 Bit Autoreload
				count = ((u32)m_tl1) + delta;
				overflow = count & 0xffffff00; // Check for overflow
				if (overflow)
					count += m_th1; // Reload timer
				// Update new values of the counter
				m_tl1 = count & 0xff;
				break;
			case 3:
				// do nothing
				break;
		}
		if (overflow)
		{
			transmit_receive(1);
		}
	}
}

// 8052 variant: Timer 2
void mcs51_cpu_device::update_timer_t2(int cycles)
{
	if (BIT(m_t2con, T2CON_TR2))
	{
		int mode = ((BIT(m_t2con, T2CON_TCLK) | BIT(m_t2con, T2CON_RCLK)) << 1) | BIT(m_t2con, T2CON_CP);
		int delta = BIT(m_t2con, T2CON_CT2) ? m_t2_cnt : (mode & 2) ? cycles * (12 / 2) : cycles;

		u32 count = m_t2 + delta;
		m_t2_cnt = 0;

		switch (mode)
		{
			case 0: // 16 Bit Auto Reload
				if (count & 0xffff0000)
				{
					set_tf2(1);
					count += m_rcap2;
				}
				else if (BIT(m_t2con, T2CON_EXEN2) && m_t2ex_cnt > 0)
				{
					count += m_rcap2;
					m_t2ex_cnt = 0;
				}
				m_t2 = count;
				break;
			case 1: // 16 Bit Capture
				if (count & 0xffff0000)
					set_tf2(1);
				m_t2 = count;

				if (BIT(m_t2con, T2CON_EXEN2) && m_t2ex_cnt > 0)
				{
					m_rcap2 = m_t2;
					m_t2ex_cnt = 0;
				}
				break;
			case 2:
			case 3: // Baud rate
				if (count & 0xffff0000)
				{
					count += m_rcap2;
					transmit_receive(2);
				}
				m_t2 = count;
				break;
		}
	}
}

void mcs51_cpu_device::update_irq_prio()
{
	for (int i = 0; i < 8; i++)
		m_irq_prio[i] = ((m_ip >> i) & 1) | (((m_iph >> i) & 1) << 1);
}

/*
Check for pending Interrupts and process.

Note about priority & interrupting interrupts..
1) A high priority interrupt cannot be interrupted by anything!
2) A low priority interrupt can ONLY be interrupted by a high priority interrupt
3) If more than 1 Interrupt Flag is set the internal order is:
   IE0, TF0, IE1, TF1, RI+TI, TF2+EXF2
*/

void mcs51_cpu_device::check_irqs()
{
	u8 ints = (BIT(m_tcon, TCON_IE0) | (BIT(m_tcon, TCON_TF0) << 1) | (BIT(m_tcon, TCON_IE1) << 2) | (BIT(m_tcon, TCON_TF1) << 3) | ((BIT(m_scon, SCON_RI) | BIT(m_scon, SCON_TI)) << 4));
	u8 int_vec = 0;
	int priority_request = -1;

	// Abort if all interrupts disabled or none pending
	u8 int_mask = BIT(m_ie, IE_A) ? m_ie : 0x00;

	// 8052 variant: fold in timer 2
	ints |= ((BIT(m_t2con, T2CON_TF2) | BIT(m_t2con, T2CON_EXF2)) << 5);
	ints &= int_mask;

	if (!ints)
		return;

	// 80C51 variant: any interrupt terminates idle mode, an external interrupt
	// also wakes from power-down
	set_idl(0);
	if (ints & (BIT(m_tcon, TCON_IE0) | BIT(m_tcon, TCON_IE1)))
		set_pd(0);
	if (BIT(m_pcon, PCON_PD))
		return;

	for (int i = 0; i < NUM_INTERRUPTS; i++)
	{
		if (ints & (1 << i))
		{
			if (m_irq_prio[i] > priority_request)
			{
				priority_request = m_irq_prio[i];
				int_vec = (i << 3) | 3;
			}
		}
	}

	/* Skip the interrupt request if currently processing interrupt
	 * and the new request does not have a higher priority */
	if (m_irq_active && (priority_request <= m_cur_irq_prio))
		return;

	// indicate we took the external IRQ
	if (int_vec == V_IE0)
	{
		// Hack to work around polling latency issue with JB INT0
		if (m_last_op == 0x20 && m_last_bit == 0xb2)
			m_pc = m_ppc + 3;
	}
	else if (int_vec == V_IE1)
	{
		// Hack to work around polling latency issue with JB INT1
		if (m_last_op == 0x20 && m_last_bit == 0xb3)
			m_pc = m_ppc + 3;
	}

	// Save current pc to stack, set pc to new interrupt vector
	push_pc();
	m_pc = int_vec;

	// interrupts take 24 cycles
	m_inst_cycles += 2;

	// Set current Irq & Priority being serviced
	m_cur_irq_prio = priority_request;
	m_irq_active |= (1 << priority_request);

	// Clear any interrupt flags that should be cleared since we're servicing the irq!
	switch (int_vec)
	{
		case V_IE0:
			// External Int Flag only cleared when configured as Edge Triggered..
			if (BIT(m_tcon, TCON_IT0))
				set_ie0(0);
			break;
		case V_TF0:
			// Timer 0 - Always clear Flag
			set_tf0(0);
			break;
		case V_IE1:
			// External Int Flag only cleared when configured as Edge Triggered..
			if (BIT(m_tcon, TCON_IT1))
				set_ie1(0);
			break;
		case V_TF1:
			// Timer 1 - Always clear Flag
			set_tf1(0);
			break;
		case V_RITI:
			// no flags are cleared, TI and RI remain set until reset by software
			break;
		case V_TF2:
			// no flags are cleared according to manual
			break;
	}
}

void mcs51_cpu_device::burn_cycles(int cycles)
{
	while (cycles--)
	{
		m_icount--;

		// update timers
		update_timer_t0(1);
		update_timer_t1(1);
		update_timer_t2(1);

		// check and update status of serial port
		transmit_receive(0);
	}
}

void mcs51_cpu_device::handle_irq(int irqline, int state, u32 new_state, u32 tr_state)
{
	switch (irqline)
	{
		// External Interrupt 0
		case MCS51_INT0_LINE:
			// Line Asserted?
			if (state != CLEAR_LINE)
			{
				// Need cleared->active line transition? (Logical 1-0 Pulse on the line)
				if (BIT(m_tcon, TCON_IT0))
				{
					if (BIT(tr_state, unsigned(MCS51_INT0_LINE)))
						set_ie0(1);
				}
				else
				{
					set_ie0(1); // Nope, just set it..
				}
			}
			else
			{
				if (!BIT(m_tcon, TCON_IT0)) // clear if level triggered
					set_ie0(0);
			}
			break;

		// External Interrupt 1
		case MCS51_INT1_LINE:
			if (state != CLEAR_LINE)
			{
				if (BIT(m_tcon, TCON_IT1))
				{
					if (BIT(tr_state, unsigned(MCS51_INT1_LINE)))
						set_ie1(1);
				}
				else
					set_ie1(1);
			}
			else
			{
				if (!BIT(m_tcon, TCON_IT1)) // clear if level triggered
					set_ie1(0);
			}
			break;

		case MCS51_T0_LINE:
			if (BIT(tr_state, unsigned(MCS51_T0_LINE)) && BIT(m_tcon, TCON_TR0))
				m_t0_cnt++;
			break;

		case MCS51_T1_LINE:
			if (BIT(tr_state, unsigned(MCS51_T1_LINE)) && BIT(m_tcon, TCON_TR1))
				m_t1_cnt++;
			break;

		// 8052 variant
		case MCS51_T2_LINE:
			if (BIT(tr_state, unsigned(MCS51_T2_LINE)) && BIT(m_tcon, TCON_TR1))
				m_t2_cnt++;
			break;

		case MCS51_T2EX_LINE:
			if (BIT(tr_state, unsigned(MCS51_T2EX_LINE)))
			{
				set_exf2(1);
				m_t2ex_cnt++;
			}
			break;
	}
}

void mcs51_cpu_device::set_input_line(int irqline, int state)
{
	/* From the manual:
	 *
	 * <cite>In operation all the interrupt flags are latched into the
	 * interrupt control system during State 5 of every machine cycle.
	 * The samples are polled during the following machine cycle.</cite>
	 *
	 * ==> Since we do not emulate sub-states, this assumes that the signal is
	 * present for at least one cycle (12 states)
	 */
	u32 new_state = (m_last_line_state & ~(1 << irqline)) | ((state != CLEAR_LINE) << irqline);

	// detect 0->1 transitions
	u32 tr_state = (~m_last_line_state) & new_state;

	handle_irq(irqline, state, new_state, tr_state);
	m_last_line_state = new_state;
}

int mcs51_cpu_device::execute_run(int cycles)
{
	m_icount = cycles;

	do
	{
		// check interrupts
		check_irqs();

		// if in powerdown and external IRQ did not wake us up, just return
		if (BIT(m_pcon, PCON_PD))
		{
			int const used = cycles - m_icount;
			m_icount = 0;
			return used;
		}

		// if not idling, process next opcode
		if (!(BIT(m_pcon, PCON_IDL) && !BIT(m_pcon, PCON_PD)))
		{
			m_ppc = m_pc;
			u8 op = m_program.read_byte(m_pc++);

			m_inst_cycles += mcs51_cycles[op];
			execute_op(op);
		}
		else
			m_inst_cycles++;

		// burn the cycles
		burn_cycles(m_inst_cycles);

		m_inst_cycles = 0;

	} while (m_icount > 0);

	return cycles - m_icount;
}

// Reset registers to the initial values
void mcs51_cpu_device::reset()
{
	m_last_line_state = 0;
	m_t0_cnt = 0;
	m_t1_cnt = 0;
	m_t2_cnt = 0;
	m_t2ex_cnt = 0;

	// Flag as NO IRQ in Progress
	m_irq_active = 0;
	m_cur_irq_prio = -1;
	m_last_op = 0;
	m_last_bit = 0;

	// these are all defined reset states
	m_rwm = 0;
	m_ppc = m_pc;
	m_pc = 0;
	m_sp = 7;
	m_psw = 0;
	m_acc = 0;
	m_dptr = 0;
	m_b = 0;
	m_ip = 0;
	m_iph = 0;
	m_ie = 0;
	m_scon = 0;
	m_tcon = 0;
	m_tmod = 0;
	m_pcon = 0;
	m_th1 = 0;
	m_th0 = 0;
	m_tl1 = 0;
	m_tl0 = 0;

	// 8052 / 80C52 extras
	m_t2con = 0;
	m_rcap2 = 0;
	m_t2 = 0;
	m_saddr = 0;
	m_saden = 0;

	m_sbuf = 0;
	m_inst_cycles = 0;
	m_icount = 0;

	m_uart.data_out = 0;
	m_uart.data_in = 0;
	m_uart.rx_clk = 0;
	m_uart.tx_clk = 0;
	m_uart.txbit = SIO_IDLE;
	m_uart.txd = 1;
	m_uart.rxbit = SIO_IDLE;
	m_uart.rxb8 = 0;
	m_uart.smod_div = 0;

	// set the port configurations to all 1's
	p3_w(0xff);
	p2_w(0xff);
	p1_w(0xff);
	p0_w(0xff);

	update_irq_prio();
}
