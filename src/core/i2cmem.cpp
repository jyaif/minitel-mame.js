// license:BSD-3-Clause
// copyright-holders:smf
//
// 24C02 I2C memory, extracted from MAME 0.288
// (src/devices/machine/i2cmem.cpp). See i2cmem.h for what was specialised.

#include "i2cmem.h"

#include <cstring>


void i2c_24c02_device::reset()
{
	std::memset(m_data, 0xff, sizeof(m_data));
	std::memset(m_page, 0, sizeof(m_page));

	m_scl = 0;
	m_sdaw = 0;
	m_sdar = 1;
	m_state = STATE_IDLE;
	m_bits = 0;
	m_shift = 0;
	m_devsel = 0;
	m_byteaddr = 0;
	m_page_offset = 0;
	m_page_written_size = 0;
}


void i2c_24c02_device::write_sda(int state)
{
	state &= 1;
	if (m_sdaw != state)
	{
		m_sdaw = state;

		// Ignore transitions on SDA while device is driving it low
		if (m_scl && m_sdar)
		{
			if (m_sdaw)
			{
				if (m_page_written_size > 0)
				{
					int base = data_offset();
					int root = base & ~(WRITE_PAGE_SIZE - 1);
					for (int i = 0; i < m_page_written_size; i++)
						m_data[root | ((base + i) & (WRITE_PAGE_SIZE - 1))] = m_page[i];

					m_page_written_size = 0;
				}
				// I2C stop
				m_state = STATE_IDLE;
			}
			else
			{
				// I2C start
				m_state = STATE_DEVSEL;
				m_bits = 0;
			}
		}
	}
}

void i2c_24c02_device::write_scl(int state)
{
	if (m_scl != state)
	{
		m_scl = state;

		switch (m_state)
		{
		case STATE_DEVSEL:
		case STATE_ADDRESSLOW:
		case STATE_DATAIN:
			if (m_bits < 8)
			{
				if (m_scl)
				{
					m_shift = ((m_shift << 1) | m_sdaw) & 0xff;
					m_bits++;
				}
			}
			else
			{
				if (m_scl)
				{
					m_bits++;
				}
				else
				{
					if (m_bits == 8)
					{
						switch (m_state)
						{
						case STATE_DEVSEL:
							m_devsel = m_shift;

							if (m_devsel == 0)
							{
								// 2-wire software reset
								m_state = STATE_RESET;
							}
							else if (!select_device())
							{
								// not this device
								m_state = STATE_IDLE;
							}
							else if ((m_devsel & DEVSEL_RW) == 0)
							{
								m_state = STATE_ADDRESSLOW;
							}
							else
							{
								m_state = STATE_READSELACK;
							}
							break;

						case STATE_ADDRESSLOW:
							// a 256-byte part takes its whole address from this packet
							m_byteaddr = m_shift | (((m_devsel & DEVSEL_ADDRESS) << 7) & address_mask());
							m_page_offset = 0;
							m_page_written_size = 0;

							m_state = STATE_DATAIN;
							break;

						case STATE_DATAIN:
							// writes land in the page buffer and are committed on stop
							m_page[m_page_offset] = m_shift;

							m_page_offset++;
							if (m_page_offset == WRITE_PAGE_SIZE)
								m_page_offset = 0;
							m_page_written_size++;
							if (m_page_written_size > WRITE_PAGE_SIZE)
								m_page_written_size = WRITE_PAGE_SIZE;
							break;
						}

						if (m_state != STATE_IDLE)
						{
							// I2C acknowledge
							m_sdar = 0;
						}
					}
					else
					{
						m_bits = 0;
						m_sdar = 1;
					}
				}
			}
			break;

		case STATE_READSELACK:
			m_bits = 0;
			m_state = STATE_DATAOUT;
			break;

		case STATE_DATAOUT:
			if (m_bits < 8)
			{
				if (m_scl)
				{
					m_bits++;
				}
				else
				{
					if (m_bits == 0)
					{
						m_shift = m_data[data_offset()];

						// no read page wrap on this part: plain sequential read
						m_byteaddr++;
					}

					m_sdar = (m_shift >> 7) & 1;

					m_shift = (m_shift << 1) & 0xff;
				}
			}
			else
			{
				if (m_scl)
				{
					if (m_sdaw)
					{
						// I2C nack
						m_state = STATE_IDLE;
					}

					m_bits = 0;
				}
				else
				{
					m_sdar = 1;
				}
			}
			break;

		case STATE_RESET:
			if (m_scl)
			{
				if (m_bits > 8)
				{
					// I2C software reset ack
					m_state = STATE_IDLE;
					m_sdar = 1;
				}
				m_bits++;
			}
			break;
		}
	}
}


int i2c_24c02_device::read_sda() const
{
	int res = m_sdar & 1;

	// pull up the written value if the read doesn't belong to this device
	if (m_state == STATE_IDLE)
		res = m_sdaw & 1;

	return res;
}
