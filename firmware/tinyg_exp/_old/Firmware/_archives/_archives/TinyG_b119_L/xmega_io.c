/*
  xmega_io.c - IO functions for xmega family
  Modeled after UNIX io: open(), close(), read(), write(), ioctl()

  Copyright (c) 2010 Alden S. Hart, Jr.

  IO subsystem features
  	- Looks and works like Unix IO .
	- Syntax, semantics and operation of UNIX IO largely copied
		- open() returns integer (unint8_t) file descriptors
		- read() and write() obey fd, buffer and size conventions (in SIZE mode)
	- Macros are defined to expose routines using UNIX names (open(), close()...)
		as opposed to the module names (xio_open(), xio_close()...)
	- Framework to organize IO drivers for the 37 (by my count) Xmega IO devices
	- Extensible to support synthetic devices such as USB ports, RS-485, etc.
	- Can be used to provide the putc / getc needed by AVR-GCC stdio

  Notable differences from UNIX IO
  	- It's Kabuki Theater: everything is pre-allocated (no malloc calls)
	- read() and write() extended to handle lines and strings (in addition to SIZE)
		- LINE_MODE: read/write to defined line delimiter (e.g. \r, \n, ';')
		- NUL_MODE:  read/write to end-of-string (aka: zero, ASCII nul, 0, \0)
	- xio_control() is NOT ioctl(). Very different interfaces


---- Read/Write Modes ----

  There are four modes for read and write:

	SIZE_MODE	Reads or writes exactly SIZE characters before returning. 
				NULs are not special - i.e. nul chars in strings are passed through
				In non-blocking mode it is possible that the read or write may 
				  complete less than SIZE characters and return with -1, EAGAIN.
				This emulates standard UNIX IO.

	LINE_MODE	Reads until a delimiter is read from the device (ex. \n, \r, ;)
				1st delimiter is written to the rcv string (ex. \r of a \r\n pair)
				The receive buffer string is nul terminated after the first delimiter
				A read that exceeds rx_size_max is an EMSGSIZE error (returns -1)
				The buffer will be full up to that point and terminated at the max.

				Writes until a delimiter is found in the source string
				The first delimiter is written to the device, 
				Terminating nul is not written to the device.
				A write that exceeds tx_size_max is an EMSGSIZE error (returns -1).
				  but will have written all bytes up to that point to the device.

	STR_MODE	Reads until a nul is read from the device.
				The nul is written to the receiving string
				A read that exceeds rx_size_max is an EMSGSIZE error (returns -1)
				The buffer will be full up to that point and terminated at the max.

				Writes until a nul is found in the source string.
				Terminating nul is not written to the device.
				A write that exceeds tx_size_max is an EMSGSIZE error (returns -1).
				  but will have written all bytes up to that point to the device.

	PSTR_MODE	(This mode is not valid for read)
	
				Writes characters from a program memory string to the device
				Writes until a nul is found in the source string (PSTR)
				Terminating nul is not written to the device.
				A write that exceeds tx_size_max is an EMSGSIZE error (returns -1).
				  but will have written all bytes up to that point to the device.

				Typically used to embed PGM string literals in a "print" statement.
				Reading from a PGM memory file is different, and is accomplished by 
				  opening a DEV_PROGMEM device and reading from program memory.

  (Not all devices implement all modes.)

  The following alaises are provided (see xmega_io.h)

	SIZE_MODE	read(f,b,s)		Specify file descriptor, receive buffer and size	
				write(f,b,s)	Specify file descriptor, source buffer and size

	LINE_MODE	readln(f,b)		Specify file descriptor and receive buffer
				writeln(f,b)	Specify file descriptor and source buffer

	STR_MODE	readstr(f,b)	Specify file descriptor and receive buffer
				writestr(f,b)	Specify file descriptor and source buffer

	PSTR_MODE	writepstr(f,b)	Specify file descriptor and source buffer

  Character level functions are also provided:

				char getc(f)	read single character from device
				void putc(f,c)	write single character to device

---- Notes on the circular buffers ----

  An attempt has beeen made to make the circular buffers used by low-level 
  character read / write as efficient as possible. This opens up higher-speed 
  IO between 100K and 1Mbaud and better supports high-speed parallel operations.

  The circular buffers are unsigned char arrays that count down from the top 
  element and wrap back to the top when index zero is reached. This allows 
  pre-decrement operations, zero tests, and eliminates modulus, mask, substraction 
  and other less efficient array bounds checking. Buffer indexes are all 
  unint_fast8_t which limits these buffers to 254 usable locations. (one is lost 
  to head/tail collision detection and one is lost to the zero position) All this 
  enables the compiler to do better optimization.

  Chars are written to the *head* and read from the *tail*. 

  The head is left "pointing to" the character that was previously written - 
  meaning that on write the head is pre-decremented (and wrapped, if necessary), 
  then the new character is written.

  The tail is left "pointing to" the character that was previouly read - 
  meaning that on read the tail is pre-decremented (and wrapped, if necessary),
  then the new character is read.

  The head is only allowed to equal the tail if there are no characters to read.

  On read: If the head = the tail there is nothing to read, so it exits or blocks.

  On write: If the head pre-increment causes the head to equal the tail the buffer
  is full. The head is reset to its previous value and the device should go into 
  flow control (and the byte in the device is not read). Reading a character from 
  a buffer that is in flow control should clear flow control

  (Note: More sophisticated flow control would detect the full condition earlier, 
   say at a high water mark of 95% full, and may go out of flow control at some low
   water mark like 33% full).

---- Other Stuff ----

  In this code:
  	"NULL" refers to a null (uninitialized) pointer 
   	"NUL" refers to the ASCII string termination character - zero
			See http://home.netcom.com/~tjensen/ptr/  (chapter 3)

---- To Do ----

  Flow control for USB low-level read and write

*/

#include <stdio.h>
#include <stdarg.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <math.h>				// only required for wiring_serial compatibility
								// ...printFloat()

#include "xmega_support.h"		// put this early as it has the F_CPU value
#include <util/delay.h>
#include "xmega_io.h"
#include "xmega_errno.h"


/* Helper routines and device-specific routines */
void _echo_to_console(char c);

// USART device handlers
static int8_t _open_USART(uint8_t dev, uint32_t control);
static int8_t _close_USART(struct fdUSART *f);
static int8_t _control_USART(struct fdUSART *f, uint32_t control, int16_t arg);
char _read_char_USART(struct fdUSART *fd_ptr);
char _write_char_USART(struct fdUSART *fd_ptr, const char c);

// USB device handlers
static int8_t _open_usb(uint8_t dev, uint32_t control);
static int8_t _close_usb(struct fdUSART *f);
static int8_t _control_usb(struct fdUSART *f, uint32_t control, int16_t arg);
int16_t _read_usb(struct fdUSART *fd_ptr, char *buf, int16_t size);
static int16_t _write_usb(struct fdUSART *fd_ptr, const char *buf, int16_t size);


/* Variables and functions with scope to this module only 
	If you want to move some functions out into other files or extend
	with new devices you need to look at this section carefully.
*/

int	errno=0;								// global error number
static struct fdUSART *fd_ptrs[FD_MAX];		// array of pointers to IO structs
static struct fdUSART fd_usb, fd_rs485;		// pre-allocate 2 USART structs
// put other types of pre-allocated structs here. See xio_init().


// File descriptor assignments. Device numbers look up FDs via this table
static const uint8_t fdes[] PROGMEM = 		// device IDs assigned to file descriptor
				{ 0,						// NULL device (position 0) assigned to 0 
				  0, 0, 0, 0, 0, 0, 0, 0, 	// Ports A, B, C, D, E, F, G, H   (no I)
				  0, 0, 0, 0, 0, 0, 0, 0,	// Ports J, K, L, M, N, P, Q, R   (no O)
				  1, 2, 0, 0, 0, 0, 0, 0, 	// USARTS C0, C1, D0, D1, E0, E1, F0, F1
				  0, 0, 0, 0,				// SPI interfaces C, D, E, F
				  0, 0,						// Two Wire interfaces C, E
				  0, 	 					// IR communications module
				  0, 						// AES accelerator
				  0, 0,						// ADCA, ADCB
				  0, 0,						// DACA, DACB
				  0, 0, 0, 0, 0, 			// SRAM, EEPROM, PGM, TABLE, BOOT
				  0, 						// CONSOLE, 
				  1, 2, 0, 0,				// USB, RS485, ENCODERS, BRIDGE
				  0, 						// a couple of not-yet-defined devices...
				  0 };						// ...for illustration purposes

// Note: USARTC0 and USB share the same file descriptor (as do USARTC1 and RS485).
// This is because USB first configures the USART, then takes it over.
// Calls to FD1 call the USB routines, not the generic USART routines.


// USART lookups
static const struct USART_struct *usel[] PROGMEM = 		// USART base addresses
		{ &USARTC0,&USARTC1,&USARTD0,&USARTD1,&USARTE0,&USARTE1,&USARTF0,&USARTF1 };

static const struct PORT_struct *psel[] PROGMEM = 		// PORT base addresses
		{ &PORTC, &PORTC, &PORTD, &PORTD, &PORTE, &PORTE, &PORTF, &PORTF };

static const uint8_t bsel[] PROGMEM = 		// baud rates. See xmega_io.h
		{ 0, 207, 103, 51, 34, 33, 31, 27, 19, 1, 1 };

static const uint8_t bscale[] PROGMEM =  	// more baud rate data
		{ 0, 0, 0, 0, 0, (-1<<4), (-2<<4), (-3<<4), (-4<<4), (1<<4), 1 };

/**************************************************************
 *
 *          XIO_MAIN ROUTINES (NOT DEVICE SPECIFIC)
 *
 * These are the dispatchers to the device specific routines
 *
 **************************************************************/

/* xio_init() - init serial and "file" io sub-system 

	All the structs are pre-assigned to the FD array. 
	These must line up with the FD values in the fdes table
*/

void xio_init(void)
{ 
	fd_ptrs[0] = NULL;						// /dev/null
	fd_ptrs[1] = &fd_usb;					// this gets assigned to serial port C0
	fd_ptrs[2] = &fd_rs485;					// this gets assigned to serial port C1
	errno = 0;
	return;
}

/* xio_open() - open a device such as a serial port or program memory "file" handle 

	dev		Device specifier (takes the place of Unix path variable)
				Device number 0 - N to specify a device (see #defines DEV_XXXXX)

	control	Valid parameters for io_open() and io_control()
				IO_RDONLY		enable read only - attempt to write will cause error
				IO_WRONLY		enable write only - attempt to read will cause error
				IO_RDWR			enable read & write operations 
				IO_RDNONBLOCK	reads return immediately if char(s) not avail
				IO_WRNONBLOCK	writes do not wait for char(s) to be written
				IO_RDWRNONBLOCK	enable nonblocking for both read and write
				IO_ECHO			echo reads from device to the console (line level)
				IO_BAUD_XXXXX	baud rate for RX and TX (not independently settable)
				[ADDR]			address of program memory to read. See Address mode.

			Defaults are:
				IO_RDWR			enable both read and write
				IO_RDBLOCK	 	enable blocking on read
				IO_WRECHO		enable echo characters read from device to console
				IO_BAUD_DEFAULT	set baud rate to default (which is 115200)

			Address mode:
				Address mode is enabled if device expects an addr (eg.DEV_PROGMEM)
				In address mode device parameters must be set using io_control(). 
				Default settings are IO_RDONLY, IO_ECHO

	returns	File descriptor for device (fd)

			Error returns -1 and sets errno

				ENODEV	requested dev is not supported or illegal

				EINVAL	requesting IO_RDONLY and IO_WRONLY is an error. Use IO_RDWR

---- Enough of the theoretical stuff. Notes about this implementation ----

	Only recognizes the synthetic devices DEV_USB and DEV_RS485. All else will fail.
	Uses a very inefficient case statement because I don't care about optimization...
	...I care more about code clarity.
*/

int8_t xio_open(uint8_t dev, uint32_t control)
{
	switch (dev) {
		case DEV_USARTC0: errno = ENODEV; return (-1); 	// can't open C0 - use USB
		case DEV_USARTC1: errno = ENODEV; return (-1); 	// can't open C1 - use RS485
		case DEV_USB: return _open_usb(dev, control);
//		case DEV_RS485: return _open_rs485(dev, control);
		default: errno = ENODEV; return (-1);
	}
	errno = EWTF;
	return (-1);		// should never execute this return
}

/* xio_control() - set device parameters  

	This isn't to ioctl. It's works differently. 
	Provides a rehash of the io_open() parameter settings only with an fd

	fd		valid device handle (file descriptor) returned from io_open()
	control	device parameters
	data	data required by some parameters
	
	return	Success: File descriptor for device (fd) 
			Error:	 -1 and sets errno. See io_open()

*/

int8_t xio_control(uint8_t fd, uint32_t control, int16_t arg)
{
	switch (fd) {
		case FD_USB: return _control_usb(fd_ptrs[fd], control, arg);
//		case FD_RS485: return _control_rs485(fd_ptrs[fd], control, arg);
		default: errno = ENODEV; return (-1);
	}
	errno = EWTF;
	return (-1);		// should never execute this return
}


/* xio_close() - close device 
	
	Close FD device. Stops all operations. Frees resources.
	In theory. In fact it's a lot like Hotel California.

	returns	0 if successful. 
	
			Error returns -1 and sets errno
				EBADF	fd isn't a valid open file descriptor.
				EINTR	The close() call was interrupted by a signal.
				EIO		An I/O error occurred
*/

int8_t xio_close(uint8_t fd)
{
	return (0);
}


/* xio_read() - read one or more characters from device 

	fd		Valid device handle (file descriptor) returned from io_open()
	buf		Address of buffer to read into - this will be a RAM (string)
	size	Number of characters to read (See Read/Write Modes for more details)
			  0		Returns zero and no other results
			  1-N	SIZE_MODE: Read 1-N chars. Returns error if N > RX_SIZE_MAX
			 -1		LINE_MODE: Read until delimiter is read from device
			 -2		STR_MODE:  Read until nul is read from device

	returns	  1-N	Number of characters read
			 -1 	Error returns -1 and sets errno:

			 EBADF	fd is not a valid file descriptor or is not open for reading.
			 EAGAIN	Non-blocking I/O has been selected using RDNONBLOCK and
					 no data was immediately available for reading.
			 EINVAL	Invalid argument - some negative number other than -1 or -2
			 EFBIG	String requested is too big and was not read
			 EMSGSIZE String exceeded maximum size and was read up to the max
*/

int16_t xio_read(uint8_t fd, char *buf, int16_t size)
{
	struct fdUSART *fd_ptr;

	if (fd == FD_USB) {
		fd_ptr = &fd_usb;
		return (_read_usb(fd_ptr, buf, size));
	} else {
		errno = EBADF;
		return (-1);
	}
}

/* xio_write() - write one or more characters to device

	fd		valid device handle (file descriptor) returned from io_open()

	buf		address of buffer to write from. This will be a RAM (string) 
				address unless if DEV_EEPROM or DEV_PROGMEM selected

	size	Number of characters to write (See Read/Write Modes for more details)
			  0		Returns zero and no other results
			  1-N	SIZE_MODE: Write 1-N chars. Returns error if N > TX_SIZE_MAX
			 -1		LINE_MODE: Write until delimiter is found in source buffer
			 -2		STR_MODE:  Write until nul is found in source buffer
			 -3 	PSTR_MODE  Write string from program memory until NUL found

	returns   1-N	Number of characters written
			 -1 	Error returns -1 and sets errno:

			 EBADF	fd is not a valid file descriptor or is not open for reading.
			 EAGAIN	Non-blocking I/O has been selected using RDNONBLOCK and
					 no data was immediately available for reading.
			 EINVAL	Invalid argument - some negative number other than -1 or -2
			 EFBIG  String exceeded maximum size and was not written
			 EMSGSIZE String exceeded maximum size and was written up to the max
*/

int16_t xio_write(uint8_t fd, const char *buf, int16_t size)
{
//	struct fdUSART *f ;
//	f = fd_ptrs[fd];
	switch (fd) {
		case (FD_USB): return (_write_usb(fd_ptrs[fd], buf, size));
		default: { errno = EBADF; return -1; }
	}
}


/* xio_getc() - read one character from device
   xio_putc() - write one character to device

	fd		valid device handle (file descriptor) returned from io_open()
	c 		character to write

	Blocking and other behaviors set by xio_open() / xio_control()
 */

char xio_getc(uint8_t fd)
{
//	struct fdUSART *f;
//	f = fd_ptrs[fd];

	switch (fd) {
		case (FD_USB): return(_read_char_USART(fd_ptrs[fd]));
		default: { errno = EBADF; return ERR_EOF; }
	}
}

char xio_putc(uint8_t fd, const char c)
{
//	struct fdUSART *f;
//	f = fd_ptrs[fd];

	switch (fd) {
		case (FD_USB): return(_write_char_USART(fd_ptrs[fd],c));
		default: { errno = EBADF; return ERR_EOF; }
	}
}

/**********************************************************************************
 * DEVICE SPECIFIC ROUTINES
 **********************************************************************************/

/**********************************************************************************
 * SPECIALTY ROUTINES
 **********************************************************************************/

/*
 *	_echo_to_console()
 */

void _echo_to_console(char c)
{
	_write_char_USART(&fd_usb, c);
}

/**********************************************************************************
 * NATIVE USART ROUTINES (GENERIC)
 **********************************************************************************/

/* 
 *	_open_USART() - initialize and set controls for USART
 */

static int8_t _open_USART(uint8_t dev, uint32_t control)
{
	struct fdUSART *f;						// ptr to our fd structure
	uint8_t u;								// index into usart settings arrays
	uint8_t fd;								// local temp for fd
	
	fd = (uint8_t)pgm_read_byte(&fdes[dev]); // lookup file descriptor
	f = fd_ptrs[fd];						// get fd struct pointer from ptr array
	f->fd = fd;
	f->rx_buf_head = 1;						// can't use location 0
	f->rx_buf_tail = 1;
	f->tx_buf_head = 1;
	f->tx_buf_tail = 1;

	// buffer overflow protection values
	f->rx_size_max = READ_BUFFER_SIZE-1;	// default read buffer size - the NUL
	f->tx_size_max = NO_LIMIT;

	// device flags
	if ((control & (IO_RDONLY | IO_WRONLY)) == (IO_RDONLY | IO_WRONLY)) {
		errno = EINVAL;						// can't have both RDONLY & WRONLY set
		return (-1);
	}
	f->flags = IO_FLAG_DEFAULT_gm;			// set flags to defaults
	if (control & IO_RDONLY) {
		f->flags &= ~IO_FLAG_WR_bm;			// clear write flag
	} else if (control && IO_RDONLY) {
		f->flags &= ~IO_FLAG_RD_bm;			// clear read flag
	}
	if (control & IO_NOECHO) {
		f->flags &= ~IO_FLAG_ECHO_CHAR_bm;	// clear line echo flag
	}
	if (control & IO_RDNONBLOCK) {
		f->flags &= ~IO_FLAG_RD_BLOCK_bm;	// clear read blocking flag
	}

	// device assignment
	u = dev - DEV_USARTC0;					// zero justify the USART #s for lookups
	f->usart = (struct USART_struct *)pgm_read_word(&usel[u]);	// bind USART to fd
	f->port = (struct PORT_struct *)pgm_read_word(&psel[u]);	// bind PORT to fd

	// baud rate and USART setup
	if ((f->baud = (uint8_t)(control & IO_BAUD_gm)) == IO_BAUD_UNSPECIFIED) {
		f->baud = IO_BAUD_DEFAULT;
	}
	f->usart->BAUDCTRLA = (uint8_t)pgm_read_byte(&bsel[f->baud]);
	f->usart->BAUDCTRLB = (uint8_t)pgm_read_byte(&bscale[f->baud]);
	f->usart->CTRLB = USART_TXEN_bm | USART_RXEN_bm; // enable tx and rx on USART
	f->usart->CTRLA = USART_RXCINTLVL_MED_gc;		 // receive interrupt medium level

	if (u & 1) {					// test if this is an odd USART (e.g. USARTC1)
		f->port->DIRCLR = USART_RX_odd_bm; 	// clr RX pin as input
		f->port->DIRSET = USART_TX_odd_bm; 	// set TX pin as output
		f->port->OUTSET = USART_TX_odd_bm;	// set TX HI as initial state
	} else {
		f->port->DIRCLR = USART_RX_even_bm;	// as above
		f->port->DIRSET = USART_TX_even_bm;
		f->port->OUTSET = USART_TX_even_bm;
	}

	// bind functions to structure
	f->read = (*_read_usb);
	f->write = (*_write_usb);
	f->close = (*_close_usb);
	f->control = (*_control_usb);

	_delay_us(10);							// give UART a chance to settle before use
	return (f->fd);
}


/* 
 *	_close_USART() - close USART port (disable) 
 */

static int8_t _close_USART(struct fdUSART *f)
{
	return (0);
}


/*	
 *	_control_USART() - set controls for USART device 
 *
 *	Control		   Data		Notes
 *	
 *	IO_BAUD_xxxxx	0		One of the supported baud rate enums
 *	IO_ECHO			0		Enable echo
 *	IO_NOECHO		0		Disable echo
 *	IO_RDBLOCK		0		Enable blocking reads
 *	IO_RDNONBLOCK	0		Disable blocking reads
 *	IO_WRBLOCK		0		Enable blocking writes (not implemented)
 *	IO_WRNONBLOCK	0		Disable blocking writes (not implemented)
 *
 *  IO_RD_SIZE_MAX	1-32767, NO_LIMIT
 *  IO_WR_SIZE_MAX	1-32767, NO_LIMIT
 */

static int8_t _control_USART(struct fdUSART *f, uint32_t control, int16_t arg)
{
	// group 1 commands (do not have argument)
	if ((control & IO_BAUD_gm) != IO_BAUD_UNSPECIFIED) {
		f->baud = (uint8_t)(control & IO_BAUD_gm);
		f->usart->BAUDCTRLA = (uint8_t)pgm_read_byte(&bsel[f->baud]);
		f->usart->BAUDCTRLB = (uint8_t)pgm_read_byte(&bscale[f->baud]);
//		_delay_us(100);							// let it settle before use
	}
	if (control & IO_ECHO) {
		f->flags |= IO_FLAG_ECHO_CHAR_bm;		// set echo flag
	}
	if (control & IO_NOECHO) {
		f->flags &= ~IO_FLAG_ECHO_CHAR_bm;		// clear echo flag
	}
	if (control & IO_RDBLOCK) {
		f->flags |= IO_FLAG_RD_BLOCK_bm;		// set read blocking flag
	}
	if (control & IO_RDNONBLOCK) {
		f->flags &= ~IO_FLAG_RD_BLOCK_bm;		// clear read blocking flag
	}
	if (control & IO_WRBLOCK) {
		f->flags |= IO_FLAG_WR_BLOCK_bm;		// set write blocking flag
	}
	if (control & IO_WRNONBLOCK) {
		f->flags &= ~IO_FLAG_WR_BLOCK_bm;		// clear write blocking flag
	}

	// group 2 commands (have argument)
	if (control & IO_RD_SIZE_MAX) {
		f->rx_size_max = arg;
		return (0);
	}
	if (control & IO_WR_SIZE_MAX) {
		f->tx_size_max = arg;
		return (0);
	}
	return (0);
}

/*
 *  _read_char_USART() - lowest level char reader for USARTS 
 *
 *	Execute blocking or non-blocking read depending on controls
 *	Return character or -1 if non-blocking
 *	Return character or sleep() if blocking
 */

char _read_char_USART(struct fdUSART *f)
{
	while (f->rx_buf_head == f->rx_buf_tail) {		// buffer empty
		if (!BLOCKING_ENABLED(f->flags)) {
			errno = EAGAIN;
			return (-1);
		}
		sleep_mode();								// sleep until next interrupt
	}
	if (--(f->rx_buf_tail) == 0) {					// decrement and wrap if needed
		fd_usb.rx_buf_tail = USART_RX_BUFSIZE-1;	// -1 avoids off-by-one error
	}
	char c = f->rx_buf[f->rx_buf_tail];				// get character from buffer
	if (ECHO_ENABLED(f->flags)) {
		_echo_to_console(c);
	}
	return c;
}


/* 
 * _write_char_USART() - lowest level char reader for USARTS 
 */

char _write_char_USART(struct fdUSART *f, const char c)
{
//	struct fdUSART *f = fd_ptr;

	while(!(f->usart->STATUS & USART_DREIF_bm)); // spin until TX data register is available
	f->usart->DATA = c;							 // write data register
	return c;
}

/**********************************************************************************
 * USB ROUTINES
 **********************************************************************************/

/* USB_RX_ISR - USB receiver interrupt (RX)
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
	if ((--fd_usb.rx_buf_head) == 0) { 				// wrap condition
		fd_usb.rx_buf_head = USART_RX_BUFSIZE-1;	// -1 avoids the off-by-one error
	}
	if (fd_usb.rx_buf_head != fd_usb.rx_buf_tail) {	// write char unless buffer full
		fd_usb.rx_buf[fd_usb.rx_buf_head] = fd_usb.usart->DATA; // (= USARTC0.DATA;)
		return;
	}
	// buffer-full handling
	if ((++fd_usb.rx_buf_head) > USART_RX_BUFSIZE -1) { // reset the head
		fd_usb.rx_buf_head = 1;
	}
	// activate flow control here or before it gets to this level
}

/* 
 *	_open_usb() - initialize and set controls for USB device 
 *
 *	This routine essentially subclasses the USARTC0 open to 
 *	extend it for use as a USB port. Mind, you it's all done at compile time.
 */

static int8_t _open_usb(uint8_t dev, uint32_t control)
{
	struct fdUSART *f;						// USART struct used by USB port
	uint8_t fd;								// temp for file descriptor

	if ((fd = _open_USART(DEV_USARTC0, control)) == -1) {
		return -1;
	}
	f = fd_ptrs[fd];						// get struct pointer from fd pointer array

	// setup USB RTS/CTS 
	f->port->DIRCLR = USB_CTS_bm; 			// set CTS pin as input
	f->port->DIRSET = USB_RTS_bm; 			// set RTS pin as output
	f->port->OUTSET = USB_RTS_bm; 			// set RTS HI initially (RTS enabled)
//	f->port->OUTCLR = USB_RTS_bm; 			// set RTS HI initially (RTS enabled)

	return (f->fd);
}

/*
 *	_close_usb() - close usb port (disable)
 */

static int8_t _close_usb(struct fdUSART *f)
{
	return (0);
}

/*
 *	_control_usb() - set controls for USB device
 */

static int8_t _control_usb(struct fdUSART *f, uint32_t control, int16_t arg)
{
	return (_control_USART(f, control, arg));
}

/* 
 *	_read_usb() - USB line reader (see io_read() for semantics) 
 *
 *	Note: LINE_MODE (-1) and STR_MODE (-2) are valid modes. PSTR_MODE (-3) is not.
 */

int16_t _read_usb(struct fdUSART *f, char *buf, int16_t size)
{
	char c;										// character temp
	int	i = 0;									// output buffer index (buf[i])
	int8_t mode;

	// get the size and mode variables right
	if (size == 0) { return (0); }							// special case of 0
	if (size > f->rx_size_max) { errno = EFBIG; return (-1); } // too big a request made
	if (size < STR_MODE) { errno = EINVAL; return (-1); }	// invalid (negative) # 
	if (size > 0) {
		mode = SIZE_MODE; 
	} else {
		mode = (int8_t)size;
		size = f->rx_size_max;					// sets max size or NO_LIMIT
	}

	// dispatch to smaller, tighter, more maintainable read loops depending on mode
	if (mode == SIZE_MODE) {
		while (TRUE) {
			if ((buf[i++] = _read_char_USART(f)) == -1) {
				return (-1);					// passes errno through			
			}
			if (--size == 0) {					// test if size is complete
				return (i);
			}
		}
	} else if (mode == LINE_MODE) {
		while (TRUE) {
			if ((c = _read_char_USART(f)) == -1) {
				return (-1);					// passes errno through			
			}
			buf[i++] = c;
			if (size != NO_LIMIT) {				// using 2 lines forces eval order
				if (--size == 0) {				// test if size is complete
					buf[i] = NUL;
					errno = EMSGSIZE;			// means was read until buffer full
					return (-1);
				}
			}
			if ((c == '\r') || (c == '\n') || (c == ';')) { 
				buf[i] = NUL;
				return (i);
			}
			if (c == NUL) {						// read a NUL
				return (i);
			}
		}
	} else if (mode == STR_MODE) {
		while (TRUE) {
			if ((c = _read_char_USART(f)) == -1) {
				return (-1);					// passes errno through			
			}
			buf[i++] = c;
			if (size != NO_LIMIT) {
				if (--size == 0) {				// test if size is complete
					buf[i] = NUL;
					errno = EFBIG;
					return (-1);
				}
			}
			if (c == NUL) {						// read a NUL
				return (i);
			}
		}		
	}
	errno = EWTF;		// shouldn't ever get here.
	return (-1);
}

/* 
 *	_write_usb() - USB line writer 
 *
 *	NOTE: LINE_MODE (-1), STR_MODE (-2), and PSTR_MODE (-3) are all valid modes.
 */

static int16_t _write_usb(struct fdUSART *f, const char *buf, int16_t size)
{
	char c;										// character temp
	int i = 0; 									// input buffer index
	int8_t mode;

	// get the size and mode variables right
	if (size == 0) { return (0); }							// special case of 0
	if (size > f->rx_size_max) { errno = EFBIG; return (-1); } // too big a request made
	if (size < PSTR_MODE) { errno = EINVAL; return (-1); }	// invalid (negative) # 
	if (size > 0) {
		mode = SIZE_MODE; 
	} else {
		mode = (int8_t)size;
		size = f->rx_size_max;					// sets max size or NO_LIMIT
	}

	// dispatch to smaller, tighter, more maintainable write loops depending on mode
	if (mode == SIZE_MODE) {
		while (TRUE) {
			if (--size == 0) {					// size is complete. return
				return (i);
			}
			if (_write_char_USART(f,buf[i++]) == -1) {	// write char to output
				return (-1);					// passes errno through
			}
		}
	} else if (mode == LINE_MODE) {
		while (TRUE) {
			if (size != NO_LIMIT) {				// using 2 lines forces eval order
				if (--size == 0) {				// test if size is complete
					errno = EMSGSIZE;			// means a truncated write occurred
					return (-1);
				}
			}
			c = buf[i++];
			if (c == NUL) {
				return (i);						// don't write nul, just return
			}
			if (_write_char_USART(f,c) == -1) {	// write char to output channel
				return (-1);					// passes errno through
			}
			if ((c == '\r') || (c == '\n') || (c == ';')) { 
				return (i);						// time to go
			}
		}
	} else if (mode == STR_MODE) {
		while (TRUE) {
			if (size != NO_LIMIT) {				// using 2 lines forces eval order
				if (--size == 0) {				// test if size is complete
					errno = EMSGSIZE;			// means a truncated write occurred
					return (-1);
				}
			}
			c = buf[i++];
			if (c == NUL) {
				return (i);						// don't write nul, just return
			}
			if (_write_char_USART(f,c) == -1) {	// write char to output channel
				return (-1);					// passes errno through
			}
		}
	} else if (mode == PSTR_MODE) {
		while (TRUE) {
			if (size != NO_LIMIT) {				// using 2 lines forces eval order
				if (--size == 0) {				// test if size is complete
					errno = EMSGSIZE;			// means a truncated write occurred
					return (-1);
				}
			}
			c = pgm_read_byte(&buf[i++]);
			if (c == NUL) {
				return (i);						// don't write nul, just return
			}
			if (_write_char_USART(f,c) == -1) {	// write char to output channel
				return (-1);					// passes errno through
			}
		}
	}
	errno = EWTF;		// shouldn't ever get here.
	return (-1);
}

/**********************************************************************************
 * RS485 ROUTINES
 **********************************************************************************/

/*
 *	ISR(USARTC1_RXC_vect) - RS485 receive character interrupt handler
 */

ISR(USARTC1_RXC_vect)	// serial port C1 RX interrupt 
{
	// normal path
	if ((--fd_rs485.rx_buf_head) == 0) { 			// wrap condition
		fd_rs485.rx_buf_head = USART_RX_BUFSIZE-1;	// -1 avoids the off-by-one error
	}
	if (fd_rs485.rx_buf_head != fd_rs485.rx_buf_tail) {	// write char unless full
		fd_rs485.rx_buf[fd_rs485.rx_buf_head] = fd_rs485.usart->DATA;
		return;
	}
	// buffer-full handling
	if ((++fd_rs485.rx_buf_head) > USART_RX_BUFSIZE -1) { // reset the head
		fd_rs485.rx_buf_head = 1;
	}
	// activate flow control here or before it gets to this level
}

/**********************************************************************************
 * Compatibility with wiring_serial.c
 **********************************************************************************/

/* printIntegerInBase() */

void printIntegerInBase(unsigned long n, unsigned long base)
{ 
	unsigned char buf[8 * sizeof(long)]; // Assumes 8-bit chars. 
	unsigned long i = 0;

	if (n == 0) {
		printByte('0');
		return;
	} 

	while (n > 0) {
		buf[i++] = n % base;
		n /= base;
	}

	for (; i > 0; i--)
		printByte(buf[i - 1] < 10 ?
			'0' + buf[i - 1] :
			'A' + buf[i - 1] - 10);
}

/* printInteger() */

void printInteger(long n)
{
	if (n < 0) {
		printByte('-');
		n = -n;
	}
	printIntegerInBase(n, 10);
}

/* printFloat() */

void printFloat(double n)
{
	double integer_part, fractional_part;
	fractional_part = modf(n, &integer_part);
	printInteger(integer_part);
	printByte('.');
	printInteger(round(fractional_part*1000));
}

/* printHex() */

void printHex(unsigned long n)
{
	printIntegerInBase(n, 16);
}
