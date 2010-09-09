/*
  xmega_io.h - "file" and serial functions for xmega family
  Modeled after unix fileio 

  Copyright (c) 2010 Alden S. Hart, Jr.

  Annoying avr20100110 bug: 
  	Browse to this dir for Libs: C:\WinAVR-20100110\avr\lib\avrxmega6

*/

#ifndef xmega_serial_h
#define xmega_serial_h

/* 
 * Function prototypes and macros
 */

void xio_init(void);
int8_t xio_open(uint8_t dev, uint32_t control);
int8_t xio_close(uint8_t fd);
int8_t xio_control(uint8_t fd, uint32_t control);
int16_t xio_read(uint8_t fd, char *buf, int size);
int16_t xio_write(uint8_t fd, const char *buf, int size);

#define open(d,c) xio_open(d,c)		// these macros redefine names to std UNIX IO
#define close(fd) xio_close(fd)
#define control(fd,c) xio_control(fd,c)
#define read(fd,b,s) xio_read(fd,b,s)
#define write(fd,b,s) xio_write(fd,b,s)

/*
 * Major IO subsystem configs, constants, and device structures 
 */

#define RX_BUFSIZE 32				// rx buffer - written by ISRs
#define TX_BUFSIZE 1				// tx buffer - (not used)
#define SSIZE_MAX RX_BUFSIZE		// maximum bytes for read or write (progmem)

/* USART IO structure
   Note: as defined this struct won't do buffers larger than 255 chars 
*/

struct fdUSART {					// file descriptor struct for serial IO
	uint_fast8_t fd;				// the assigned FD number
	uint_fast8_t baud;				// baud rate index
	uint_fast8_t flags;				// control flags

	uint_fast8_t rx_buf_tail;		// RX buffer read index (location from which to read)
	volatile uint8_t rx_buf_head;	// RX buffer write index (changes via ISR)

	volatile uint8_t tx_buf_tail;	// TX buffer read index (changes via ISR)
	uint_fast8_t tx_buf_head;		// TX buffer write index

	volatile char rx_buf[RX_BUFSIZE];
//	volatile char tx_buf[TX_BUFSIZE];

	struct USART_struct *usart;		// USART structure
	struct PORT_struct *port;		// corresponding port

	// somehow I'd like to get this to work
//	int16_t (*xio_read)(struct fdUSART *fd_ptr, char *buf, int size);
	int16_t (*read)();
	int16_t (*write)();
	int8_t (*close)();
	int8_t (*control)();
};

/*************************************
 *
 * IO Subsystem General Assignments
 *
 *************************************/

#define FD_USB 1					// file descriptor for USB port
#define FD_RS485 2					// file descriptor for RS485 port
#define FD_MAX 3					// size of FD pointer array

#define SIZE_MODE 0					// read / write by size
#define LINE_MODE -1				// read / write to delimiter
#define NUL_MODE -2					// read / write to NUL (0, \0)
#define NUL 0						// the ASCII NUL char itself

/* Devices recognized by IO system functions 
	(note: by leaving these contiguous you have a better chance the compiler
	 will implement an efficient switch statement - like a computed goto)
*/

// Native Xmega devices
#define DEV_NULL	0				// NULL device

#define DEV_PORTA	1				// Define ports as IO devices
#define DEV_PORTB	2
#define DEV_PORTC	3
#define DEV_PORTD	4
#define DEV_PORTE	5
#define DEV_PORTF	6
#define DEV_PORTG	7				// not implemented on xmega A3s
#define DEV_PORTH	8				// not implemented on xmega A3s
#define DEV_PORTJ	9				// not implemented on xmega A3s
#define DEV_PORTK	10				// not implemented on xmega A3s
#define DEV_PORTL	11				// not implemented on xmega A3s
#define DEV_PORTM	12				// not implemented on xmega A3s
#define DEV_PORTN	13				// not implemented on xmega A3s
#define DEV_PORTP	14				// not implemented on xmega A3s
#define DEV_PORTQ	15				// not implemented on xmega A3s

#define DEV_PORTR	16				// special purpose port - programming bits only 

#define DEV_USARTC0	17				// USARTS C0 - F1
#define DEV_USARTC1	18
#define DEV_USARTD0	19
#define DEV_USARTD1	20
#define DEV_USARTE0	21
#define DEV_USARTE1	22
#define DEV_USARTF0	23
#define DEV_USARTF1	24

#define DEV_SPIC	25				// SPI interfaces C - F
#define DEV_SPID	26
#define DEV_SPIE	27
#define DEV_SPIF	28

#define DEV_TWIC	29				// Two Wire interfaces C and E
#define DEV_TWIE	30

#define DEV_IRCOM	31				// IR communications module
#define DEV_AES		32				// AES crypto accelerator

#define DEV_ADCA	33				// ADCs
#define DEV_ADCB	34

#define DEV_DACA	35				// DACs
#define DEV_DACB	36

#define	DEV_SRAM	37				// string in static RAM
#define DEV_EEPROM	38				// string in EEPROM
#define DEV_PROGMEM 39				// string in application program memory (FLASH)
#define DEV_TABLEMEM 40				// string in app table program memory (FLASH)
#define DEV_BOOTMEM 41				// string in boot program memory (FLASH)


// Synthetic devices
#define DEV_CONSOLE	42				// mapped to USB, here for convenience
#define DEV_USB		43				// USB comm and controls packaged
#define DEV_RS485	44				// RS485 comm and controls packaged
#define DEV_ENCODERS 45				// Encoder comm and controls packaged
#define DEV_BRIDGE	46				// USB to RS485 bridge

/* Serial Configuration Settings
   Values for common baud rates at 32 Mhz clock
   Enum Baud		BSEL	BSCALE 
	0  	unspec'd	0		0			// use default value
	1	9600		207		0
	2	19200		103		0
	3	38400		51		0
	4	57600		34		0
	5	115200		33 		(-1<<4)
	6	230400		31		(-2<<4)
	7	460800		27		(-3<<4)
	8	921600		19		(-4<<4)
	9	500000		1 		(1<<4)		// 500K
	10	1000000		1		0			// 1 mbps
*/

#define	IO_BAUD_UNSPECIFIED 0
#define IO_BAUD_9600 1
#define IO_BAUD_19200 2
#define IO_BAUD_38400 3
#define IO_BAUD_57600 4
#define IO_BAUD_115200 5
#define IO_BAUD_230400 6
#define IO_BAUD_460800 7
#define IO_BAUD_921600 8
#define IO_BAUD_500000 9
#define IO_BAUD_1000000 10
#define	IO_BAUD_DEFAULT IO_BAUD_115200

/* io_open() io_control() parameters and fs.flags */

#define IO_BAUD_gm		0x0000000F	// baud rate enumeration mask (keep in LSbyte)

#define IO_RDONLY		(1<<8) 		// read enable bit
#define IO_WRONLY		(1<<9)		// write enable only
#define IO_RDWR			(0) 		// read & write

#define IO_ECHO			(1<<10)		// echo reads from device to console (line level)
#define IO_NOECHO		(1<<11)		// disable echo

#define IO_RDBLOCK		(1<<12)		// enable blocking reads
#define IO_WRBLOCK		(1<<13)		// enable blocking writes (not implemented)
#define IO_RDWRBLOCK 	(IO_RDBLOCK | IO_WRBLOCK)  // enable blocking on RD/WR
#define IO_RDNONBLOCK	(1<<14)		// disable blocking reads
#define IO_WRNONBLOCK	(1<<15)		// disable blocking writes (not implemented)
#define IO_RDWRNONBLOCK (IO_RDNONBLOCK | IO_WRNONBLOCK)

#define IO_FLAG_RD_bm		(1<<0)	// read flag in fs.flags
#define IO_FLAG_WR_bm		(1<<1)	// write flag
#define IO_FLAG_RD_BLOCK_bm	(1<<2)	// enable blocking read
#define IO_FLAG_WR_BLOCK_bm	(1<<3)	// enable blocking write
#define IO_FLAG_ECHO_CHAR_bm (1<<4)	// echo read chars to console 

#define IO_FLAG_DEFAULT_gm (IO_FLAG_RD_bm | IO_FLAG_WR_bm | IO_FLAG_RD_BLOCK_bm | IO_FLAG_ECHO_CHAR_bm)

#define READ_ENABLED(a) (a & IO_FLAG_RD_bm)			// TRUE if read enabled
#define WRITE_ENABLED(a) (a & IO_FLAG_WR_bm)		// TRUE if write enabled
#define BLOCKING_ENABLED(a) (a & IO_FLAG_RD_BLOCK_bm) // TRUE if read blocking enab
#define ECHO_ENABLED(a) (a & IO_FLAG_ECHO_CHAR_bm)	// TRUE if char echo mode enabled



/********************************
 *
 * Device Specific Assignments
 *
 ********************************/

/*
 * generic USART device assignments
 */

#define USART_TX_even_bm (1<<3)		// TX pin for even USARTs (e.g. USARTC0)
#define USART_RX_even_bm (1<<2)		// RX pin 
#define USART_RTS_even_bm (1<<1)	// RTS pin (or extra for other purposes)
#define USART_CTS_even_bm (1<<0)	// CTS pin (or extra for other purposes)

#define USART_TX_odd_bm (1<<7)		// TX pin for even USARTs (e.g. USARTC1)
#define USART_RX_odd_bm (1<<6)		// RX pin 
#define USART_RTS_odd_bm (1<<5)		// RTS pin (or extra for other purposes)
#define USART_CTS_odd_bm (1<<4)		// CTS pin (or extra for other purposes)

/*
 * USB port assignments
 */

#define USB_USART USARTC0			// USARTC0 is wired to USB chip on the board
#define USB_RX_ISR_vect USARTC0_RXC_vect	// RX ISR
#define USB_TX_ISR_vect USARTC0_TXC_vect	// TX ISR

#define USB_PORT PORTC				// port where the USART is located
#define USB_RX_bm (1<<2)			// RX pin	- these pins are wired on the board
#define USB_TX_bm (1<<3)			// TX pin
#define USB_RTS_bm (1<<1)			// RTS pin
#define USB_CTS_bm (1<<0)			// CTS pin

/*
 * RS485 port assignments
 */

#define RS485_USART USARTC1			// USARTC1 is wired to RS485 circuitry
#define RS485_RX_ISR_vect USARTC1_RXC_vect	// RX ISR
#define RS485_TX_ISR_vect USARTC1_TXC_vect	// TX ISR

#define RS485_PORT PORTC			// port where the USART is located
#define RS485_RX_bm (1<<6)			// RX pin	- these pins are wired on the board
#define RS485_TX_bm (1<<7)			// TX pin
#define RS485_DE_bm (1<<5)			// Data Enable pin (active HI)
#define RS485_RE_bm (1<<4)			//~Recv Enable pin (active LO)

#endif
