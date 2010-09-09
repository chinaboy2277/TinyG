/*
  xio_usb.c - FTDI USB port driver for xmega family
  Copyright (c) 2010 Alden S. Hart, Jr.
*/

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>			// needed for blocking character reads

#include "xmega_support.h"		// put this early as it has the F_CPU value
#include "xio.h"
#include "xio_usb.h"

// local USART table lookups

//static const uint8_t bsel[] PROGMEM = 				// baud rates. See xmega_io.h
//		{ 0, 207, 103, 51, 34, 33, 31, 27, 19, 1, 1 };

//static const uint8_t bscale[] PROGMEM =  			// more baud rate data
//		{ 0, 0, 0, 0, 0, (-1<<4), (-2<<4), (-3<<4), (-4<<4), (1<<4), 1 };

extern uint8_t bsel[];					// shared baud rate selection values
extern uint8_t bscale[];					// shared baud rate scaling vaalues

struct xioUSART f;				// local USART control struct

/* 
 *	xio_usb_init() - initialize and set controls for USB device 
 *
 *	Control		   Arg	  Default		Notes
 *	
 *	XIO_RD		  <null>	Y	Enable device for reads
 *	XIO_WR		  <null>	Y	Enable device for write
 *	XIO_ECHO	  <null>	Y	Enable echo
 *	XIO_NOECHO	  <null>		Disable echo
 *	XIO_CRLF	  <null>		Enable echo
 *	XIO_NOCRLF	  <null>	Y	Disable echo
 *	XIO_BLOCK	  <null>	Y	Enable blocking reads
 *	XIO_NOBLOCK   <null>		Disable blocking reads
 *
 *	XIO_BAUD_xxxxx <null>		One of the supported baud rate enums
 */

void xio_usb_init(uint32_t control)
{
	// transfer control flags to internal flag bits
	f.flags = XIO_FLAG_DEFAULT_gm;			// set flags to defaults & initial state
	if (control & XIO_RD) {
		f.flags |= XIO_FLAG_RD_bm;
	}
	if (control & XIO_WR) {
		f.flags |= XIO_FLAG_WR_bm;
	}
	if (control & XIO_ECHO) {
		f.flags |= XIO_FLAG_ECHO_bm;
	}
	if (control & XIO_NOECHO) {
		f.flags &= ~XIO_FLAG_ECHO_bm;
	}
	if (control & XIO_CRLF) {
		f.flags |= XIO_FLAG_CRLF_bm;
	}
	if (control & XIO_NOCRLF) {
		f.flags &= ~XIO_FLAG_CRLF_bm;
	}
	if (control & XIO_BLOCK) {
		f.flags |= XIO_FLAG_BLOCK_bm;
	}
	if (control & XIO_NOBLOCK) {
		f.flags &= ~XIO_FLAG_BLOCK_bm;
	}

	// setup internal RX/TX buffers
	f.rx_buf_head = 1;						// can't use location 0
	f.rx_buf_tail = 1;
	f.tx_buf_head = 1;
	f.tx_buf_tail = 1;

	// device assignment
	f.usart = &USB_USART;					// bind USART
	f.port = &USB_PORT;						// bind PORT

	// baud rate and USART setup
	if ((f.baud = (uint8_t)(control & XIO_BAUD_gm)) == XIO_BAUD_UNSPECIFIED) {
		f.baud = XIO_BAUD_DEFAULT;
	}
	f.usart->BAUDCTRLA = (uint8_t)pgm_read_byte(&bsel[f.baud]);
	f.usart->BAUDCTRLB = (uint8_t)pgm_read_byte(&bscale[f.baud]);
	f.usart->CTRLB = USART_TXEN_bm | USART_RXEN_bm; // enable tx and rx on USART
	f.usart->CTRLA = USART_RXCINTLVL_MED_gc;		 // receive interrupt medium level

	f.port->DIRCLR = USB_RX_bm;	 			// clr RX pin as input
	f.port->DIRSET = USB_TX_bm; 			// set TX pin as output
	f.port->OUTSET = USB_TX_bm;				// set TX HI as initial state
	f.port->DIRCLR = USB_CTS_bm; 			// set CTS pin as input
	f.port->DIRSET = USB_RTS_bm; 			// set RTS pin as output
	f.port->OUTSET = USB_RTS_bm; 			// set RTS HI initially (RTS enabled)
//	f.port->OUTCLR = USB_RTS_bm; 			// set RTS HI initially (RTS enabled)
}

/*	
 *	xio_usb_control() - set controls for USB device 
 *
 *	Control		   Arg	  Default		Notes
 *	
 *	XIO_ECHO	  <null>	Y	Enable echo
 *	XIO_NOECHO	  <null>		Disable echo
 *	XIO_CRLF	  <null>		Enable echo
 *	XIO_NOCRLF	  <null>	Y	Disable echo
 *	XIO_BLOCK	  <null>	Y	Enable blocking reads
 *	XIO_NOBLOCK   <null>		Disable blocking reads
 *
 *	XIO_BAUD_xxxxx	<null>		One of the supported baud rate enums
 */

int8_t xio_usb_control(uint32_t control, int16_t arg)
{
	// group 1 commands (do not have argument)
	if ((control & XIO_BAUD_gm) != XIO_BAUD_UNSPECIFIED) {
		f.baud = (uint8_t)(control & XIO_BAUD_gm);
		f.usart->BAUDCTRLA = (uint8_t)pgm_read_byte(&bsel[f.baud]);
		f.usart->BAUDCTRLB = (uint8_t)pgm_read_byte(&bscale[f.baud]);
	}

//	if (control & XIO_RD) {				// don't support chaning modes after create
//		f.flags |= XIO_FLAG_RD_bm;
//	}
//	if (control & XIO_WR) {
//		f.flags |= XIO_FLAG_WR_bm;
//	}
	if (control & XIO_ECHO) {
		f.flags |= XIO_FLAG_ECHO_bm;
	}
	if (control & XIO_NOECHO) {
		f.flags &= ~XIO_FLAG_ECHO_bm;
	}
	if (control & XIO_CRLF) {
		f.flags |= XIO_FLAG_CRLF_bm;
	}
	if (control & XIO_NOCRLF) {
		f.flags &= ~XIO_FLAG_CRLF_bm;
	}
	if (control & XIO_BLOCK) {
		f.flags |= XIO_FLAG_BLOCK_bm;
	}
	if (control & XIO_NOBLOCK) {
		f.flags &= ~XIO_FLAG_BLOCK_bm;
	}

	// group 2 commands (have argument)	// no argument commands for this device
	return (0);
}

/* 
 * USB_RX_ISR - USB receiver interrupt (RX)
 *
 *	RX buffer states can be one of:
 *	- buffer has space	(CTS should be asserted)
 *	- buffer is full 	(CTS should be not_asserted)
 *	- buffer becomes full with this character (write char and assert CTS)
 *
 *	We use expressions like fd_usb.rx_buf_head instead of fd_ptrs[FD_USB]->rx_buf_head
 *	because it's more efficient and this is an interrupt and it's hard-wired anyway
 *
 *	Flow control is not implemented. Need to work RTS line.
 *	Flow control should cut off at high water mark, re-enable at low water mark
 *	High water mark should have about 4 - 8 bytes left in buffer (~95% full) 
 *	Low water mark about 50% full
 */

ISR(USB_RX_ISR_vect)		//ISR(USARTC0_RXC_vect)	// serial port C0 RX interrupt 
{
	// normal path
	if ((--f.rx_buf_head) == 0) { 				// wrap condition
		f.rx_buf_head = RX_BUFFER_SIZE-1;		// -1 avoids the off-by-one error
	}
	if (f.rx_buf_head != f.rx_buf_tail) {		// write char unless buffer full
		f.rx_buf[f.rx_buf_head] = f.usart->DATA;// (= USARTC0.DATA;)
		return;
	}
	// buffer-full handling
	if ((++f.rx_buf_head) > RX_BUFFER_SIZE-1) { // reset the head
		f.rx_buf_head = 1;
	}
	// activate flow control here or before it gets to this level
}

/* 
 *	xio_usb_putc() - char writer for USB device 
 */

int xio_usb_putc(char c, FILE *stream)
{
	while(!(f.usart->STATUS & USART_DREIF_bm)); // spin until TX data register is available
	f.usart->DATA = c;							 // write data register
	return 0;
}

/*
 *  xio_usb_getc() - char reader for USB device
 *
 *	Execute blocking or non-blocking read depending on controls
 *	Return character or -1 if non-blocking
 *	Return character or sleep() if blocking
 */

int xio_usb_getc(FILE *stream)
{
	while (f.rx_buf_head == f.rx_buf_tail) {	// buffer empty
		if (!BLOCKING_ENABLED(f.flags)) {
			return (-1);
		}
		sleep_mode();							// sleep until next interrupt
	}
	if (--(f.rx_buf_tail) == 0) {				// decrement and wrap if needed
		f.rx_buf_tail = RX_BUFFER_SIZE-1;		// -1 avoids off-by-one error
	}
	char c = f.rx_buf[f.rx_buf_tail];			// get character from buffer
//	if (ECHO_ENABLED(f.flags)) {
//		_echo_to_console(c);
//	}
	return c;
}
