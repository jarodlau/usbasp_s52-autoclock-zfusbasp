/*
 * USBasp - USB in-circuit programmer for Atmel AVR controllers
 *
 * Thomas Fischl <tfischl@gmx.de>
 *
 * License........: GNU GPL v2 (see Readme.txt)
 * Target.........: ATMega8 at 12 MHz
 * Creation Date..: 2005-02-20
 * Last change....: 2009-02-28
 *
 * PC2 SCK speed option.
 * GND  -> slow (8khz SCK),
 * open -> software set speed (default is 375kHz SCK)
 *
 * Changed........; 2015-11-18
 * mearged changes to program 89s52 ,also auto set clock ,no need SLOW SCK(J2)
 *
 * This model is similar to USBasp except that is uses:
 *      PD2 and PD3 for USB D- and D+
 *      PD5 and PD6 for green and red LEDs,back-to-back,no external resistor
 * To program it via the 10-pin ISP connector, a jumper is need between MEGA8
 * pin 29 (RESET) and ISP pin 5 (RESET).
 * Because I was afriaid to put 5V across the LEDS I enabled the internal
 * pullup, but it turned out this made the LED rather dim. Maybe they are
 * actually 5V-rated LEDs and the pullup should not be used.
 *
 * This source also include a personal serial number to identify my device, and
 * minor changes to get usbdrv to build with the currend GCC.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "usbasp.h"
#include "usbdrv.h"
#include "isp.h"
#include "clock.h"
#include "tpi.h"
#include "tpi_defs.h"

static uchar replyBuffer[8];

static uchar prog_state = PROG_STATE_IDLE;
static uchar prog_sck = USBASP_ISP_SCK_AUTO;

static uchar prog_address_newmode = 0;
static unsigned long prog_address;
static unsigned int prog_nbytes = 0;
static unsigned int prog_pagesize;
static uchar prog_blockflags;
static uchar prog_pagecounter;

uchar ispEnterProgrammingMode_jarodlau()
{
   uchar b;
   // return ispEnterProgrammingMode();//原来的代码
   //根据跳线恢复设置
   //跳线设置速度的部分
   ispDisconnect();
   ledRedOff();

   /* set SCK speed */
   if((PINC & (1 << PC2)) == 0)
      {
         ispSetSCKOption(USBASP_ISP_SCK_8);//低速
      }

   else
      {
         ispSetSCKOption(prog_sck);//自动
         //默认   prog_sck=USBASP_ISP_SCK_375:硬件SPI， enable SPI, master, 375kHz, XTAL/32 (default)
      }

   prog_address_newmode = 0;
   ledRedOn();
   ispConnect();

   //加入自动降速的代码
   b = ispEnterProgrammingMode();
   if(b == 1)
      {
         //如果 ispEnterProgrammingMode(); 检测了32次都失败，就降速
         ispDisconnect();
         ledRedOff();
         ispSetSCKOption(USBASP_ISP_SCK_8);//低速
         prog_address_newmode = 0;
         ledRedOn();
         ispConnect();

         b = ispEnterProgrammingMode();
      }

   return b;
}

uchar usbFunctionSetup(uchar data[8]) {

	uchar len = 0;

	if (data[1] == USBASP_FUNC_CONNECT) {

		/* set SCK speed */
		if ((PINC & (1 << PC2)) == 0) {
			ispSetSCKOption(USBASP_ISP_SCK_8);
		} else {
			ispSetSCKOption(prog_sck);
		}

		/* set compatibility mode of address delivering */
		prog_address_newmode = 0;

		ledRedOn();
		ispConnect();

	} else if (data[1] == USBASP_FUNC_DISCONNECT) {
		ispDisconnect();
		ledRedOff();

	} else if (data[1] == USBASP_FUNC_TRANSMIT) {
	  if(chip==ATM){
		replyBuffer[0] = ispTransmit(data[2]);
		replyBuffer[1] = ispTransmit(data[3]);
		replyBuffer[2] = ispTransmit(data[4]);
		replyBuffer[3] = ispTransmit(data[5]);
	  } else {
	    if(data[2]==0x24) {
	      // read lock bits
	      replyBuffer[0] = ispTransmit(data[2]);
	      replyBuffer[1] = ispTransmit(data[3]);
	      replyBuffer[2] = ispTransmit(data[4]);
	      switch(ispTransmit(data[5])&0x1C) {
	      case(0x00):replyBuffer[3]=0xE0;break;
	      case(0x04):replyBuffer[3]=0xE5;break;
	      case(0x0C):replyBuffer[3]=0xEE;break;
	      case(0x1C):replyBuffer[3]=0xFF;break;
	      }
	    } else if(data[2]==0x30) {
	      // read signature
	      replyBuffer[0] = ispTransmit(0x28);
	      replyBuffer[1] = ispTransmit(data[3]);
	      replyBuffer[2] = ispTransmit(data[4]);
	      replyBuffer[3] = ispTransmit(data[5]);
	    } else {
	      replyBuffer[0] = ispTransmit(data[2]);
	      replyBuffer[1] = ispTransmit(data[3]);
	      replyBuffer[2] = ispTransmit(data[4]);
	      replyBuffer[3] = ispTransmit(data[5]);
	    }
	  }
	  len = 4;

	} else if (data[1] == USBASP_FUNC_READFLASH) {

		if (!prog_address_newmode)
			prog_address = (data[3] << 8) | data[2];

		prog_nbytes = (data[7] << 8) | data[6];
		prog_state = PROG_STATE_READFLASH;
		len = 0xff; /* multiple in */

	} else if (data[1] == USBASP_FUNC_READEEPROM) {

		if (!prog_address_newmode)
			prog_address = (data[3] << 8) | data[2];

		prog_nbytes = (data[7] << 8) | data[6];
		prog_state = PROG_STATE_READEEPROM;
		len = 0xff; /* multiple in */

	} else if (data[1] == USBASP_FUNC_ENABLEPROG) {
		//    replyBuffer[0] = ispEnterProgrammingMode();
         	replyBuffer[0] = ispEnterProgrammingMode_jarodlau();//代码修改成自动降速,jarodlau
		len = 1;

	} else if (data[1] == USBASP_FUNC_WRITEFLASH) {

		if (!prog_address_newmode)
			prog_address = (data[3] << 8) | data[2];

		prog_pagesize = data[4];
		prog_blockflags = data[5] & 0x0F;
		prog_pagesize += (((unsigned int) data[5] & 0xF0) << 4);
		if (prog_blockflags & PROG_BLOCKFLAG_FIRST) {
			prog_pagecounter = prog_pagesize;
		}
		prog_nbytes = (data[7] << 8) | data[6];
		prog_state = PROG_STATE_WRITEFLASH;
		len = 0xff; /* multiple out */

	} else if (data[1] == USBASP_FUNC_WRITEEEPROM) {

		if (!prog_address_newmode)
			prog_address = (data[3] << 8) | data[2];

		prog_pagesize = 0;
		prog_blockflags = 0;
		prog_nbytes = (data[7] << 8) | data[6];
		prog_state = PROG_STATE_WRITEEEPROM;
		len = 0xff; /* multiple out */

	} else if (data[1] == USBASP_FUNC_SETLONGADDRESS) {

		/* set new mode of address delivering (ignore address delivered in commands) */
		prog_address_newmode = 1;
		/* set new address */
		prog_address = *((unsigned long*) &data[2]);

	} else if (data[1] == USBASP_FUNC_SETISPSCK) {

		/* set sck option */
		prog_sck = data[2];
		replyBuffer[0] = 0;
		len = 1;

	} else if (data[1] == USBASP_FUNC_TPI_CONNECT) {
		tpi_dly_cnt = data[2] | (data[3] << 8);

		/* RST high */
		ISP_OUT |= (1 << ISP_RST);
		ISP_DDR |= (1 << ISP_RST);

		clockWait(3);

		/* RST low */
		ISP_OUT &= ~(1 << ISP_RST);
		ledRedOn();

		clockWait(16);
		tpi_init();

	} else if (data[1] == USBASP_FUNC_TPI_DISCONNECT) {

		tpi_send_byte(TPI_OP_SSTCS(TPISR));
		tpi_send_byte(0);

		clockWait(10);

		/* pulse RST */
		ISP_OUT |= (1 << ISP_RST);
		clockWait(5);
		ISP_OUT &= ~(1 << ISP_RST);
		clockWait(5);

		/* set all ISP pins inputs */
		ISP_DDR &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));
		/* switch pullups off */
		ISP_OUT &= ~((1 << ISP_RST) | (1 << ISP_SCK) | (1 << ISP_MOSI));

		ledRedOff();

	} else if (data[1] == USBASP_FUNC_TPI_RAWREAD) {
		replyBuffer[0] = tpi_recv_byte();
		len = 1;

	} else if (data[1] == USBASP_FUNC_TPI_RAWWRITE) {
		tpi_send_byte(data[2]);

	} else if (data[1] == USBASP_FUNC_TPI_READBLOCK) {
		prog_address = (data[3] << 8) | data[2];
		prog_nbytes = (data[7] << 8) | data[6];
		prog_state = PROG_STATE_TPI_READ;
		len = 0xff; /* multiple in */

	} else if (data[1] == USBASP_FUNC_TPI_WRITEBLOCK) {
		prog_address = (data[3] << 8) | data[2];
		prog_nbytes = (data[7] << 8) | data[6];
		prog_state = PROG_STATE_TPI_WRITE;
		len = 0xff; /* multiple out */

	} else if (data[1] == USBASP_FUNC_GETCAPABILITIES) {
		replyBuffer[0] = USBASP_CAP_0_TPI;
		replyBuffer[1] = 0;
		replyBuffer[2] = 0;
		replyBuffer[3] = 0;
		len = 4;
	}

	usbMsgPtr = replyBuffer;

	return len;
}

uchar usbFunctionRead(uchar *data, uchar len) {

	uchar i;

	/* check if programmer is in correct read state */
	if ((prog_state != PROG_STATE_READFLASH) && (prog_state
			!= PROG_STATE_READEEPROM) && (prog_state != PROG_STATE_TPI_READ)) {
		return 0xff;
	}

	/* fill packet TPI mode */
	if(prog_state == PROG_STATE_TPI_READ)
	{
		tpi_read_block(prog_address, data, len);
		prog_address += len;
		return len;
	}

	/* fill packet ISP mode */
	for (i = 0; i < len; i++) {
		if (prog_state == PROG_STATE_READFLASH) {
			data[i] = ispReadFlash(prog_address);
		} else {
			data[i] = ispReadEEPROM(prog_address);
		}
		prog_address++;
	}

	/* last packet? */
	if (len < 8) {
		prog_state = PROG_STATE_IDLE;
	}

	return len;
}

uchar usbFunctionWrite(uchar *data, uchar len) {

	uchar retVal = 0;
	uchar i;

	/* check if programmer is in correct write state */
	if ((prog_state != PROG_STATE_WRITEFLASH) && (prog_state
			!= PROG_STATE_WRITEEEPROM) && (prog_state != PROG_STATE_TPI_WRITE)) {
		return 0xff;
	}

	if (prog_state == PROG_STATE_TPI_WRITE)
	{
		tpi_write_block(prog_address, data, len);
		prog_address += len;
		prog_nbytes -= len;
		if(prog_nbytes <= 0)
		{
			prog_state = PROG_STATE_IDLE;
			return 1;
		}
		return 0;
	}

	for (i = 0; i < len; i++) {

		if (prog_state == PROG_STATE_WRITEFLASH) {
			/* Flash */

			if (prog_pagesize == 0) {
				/* not paged */
				ispWriteFlash(prog_address, data[i], 1);
			} else {
				/* paged */
				ispWriteFlash(prog_address, data[i], 0);
				prog_pagecounter--;
				if (prog_pagecounter == 0) {
					ispFlushPage(prog_address, data[i]);
					prog_pagecounter = prog_pagesize;
				}
			}

		} else {
			/* EEPROM */
			ispWriteEEPROM(prog_address, data[i]);
		}

		prog_nbytes--;

		if (prog_nbytes == 0) {
			prog_state = PROG_STATE_IDLE;
			if ((prog_blockflags & PROG_BLOCKFLAG_LAST) && (prog_pagecounter
					!= prog_pagesize)) {

				/* last block and page flush pending, so flush it now */
				ispFlushPage(prog_address, data[i]);
			}

			retVal = 1; // Need to return 1 when no more data is to be received
		}

		prog_address++;
	}

	return retVal;
}

int main(void) {
	uchar i, j;

	/* no pullups on USB and ISP pins */
	/* pullups on LED pins */
	//PORTD = (1 << PD5) | (1 << PD6);
	PORTB = 0;
	/* all inputs except PD5(green)and PD6(red) */
	//all input
	//DDRD = 0;
	/* 初值的问题,折腾了半天,红灯始终不亮,终于搞定*/
	DDRD  |= 0x60;
	/* 默认开启绿灯, 不影响其他位*/
	PORTD |= 0x40;

	/* output SE0 for USB reset */
	DDRB = ~0;
	j = 0;
	/* USB Reset by device only required on Watchdog Reset */
	while (--j) {
		i = 0;
		/* delay >10ms for USB reset */
		while (--i)
			;
	}
	/* all USB and ISP pins inputs */
	DDRB = 0;

	/* PORT C all inputs with pull-ups */
	DDRC = 0;
	PORTC = 0xff;

	chip=ATM;

	/* init timer */
	clockInit();

	/* main event loop */
	usbInit();
	sei();
	for (;;) {
		usbPoll();
	}
	return 0;
}

