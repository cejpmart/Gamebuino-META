/***************************************************
	This is a library for the Adafruit 1.8" SPI display.

This library works with the Adafruit 1.8" TFT Breakout w/SD card
	----> http://www.adafruit.com/products/358
The 1.8" TFT shield
	----> https://www.adafruit.com/product/802
The 1.44" TFT breakout
	----> https://www.adafruit.com/product/2088
as well as Adafruit raw 1.8" TFT display
	----> http://www.adafruit.com/products/618

	Check out the links above for our tutorials and wiring diagrams
	These displays use SPI to communicate, 4 or 5 pins are required to
	interface (RST is optional)
	Adafruit invests time and resources providing this open source code,
	please support Adafruit and open-source hardware by purchasing
	products from Adafruit!

	Written by Limor Fried/Ladyada for Adafruit Industries.
	MIT license, all text above must be included in any redistribution
 ****************************************************/

#include "Display-ST7735.h"
#include <limits.h>
#include "pins_arduino.h"
#include "wiring_private.h"
#include <SPI.h>
#include "../Image.h"


#include "../Adafruit_ASFcore.h"
#include "../Adafruit_ASFcore/status_codes.h"
#include "../Adafruit_ZeroDMA.h"
#include "../Adafruit_ZeroDMA/utility/dmac.h"
#include "../Adafruit_ZeroDMA/utility/dma.h"

namespace Gamebuino_Meta {


Adafruit_ZeroDMA myDMA;
status_code stat; // we'll use this to read and print out the DMA status codes
// are we done yet?
volatile bool transfer_is_done = false;
// If you like, a callback can be used
void dma_callback(struct dma_resource* const resource) {
	transfer_is_done = true;
}
void printStatus(status_code stat) {
	Serial.print("Status ");
	switch (stat) {
	case STATUS_OK:
		Serial.println("OK"); break;
	case STATUS_BUSY:
		Serial.println("BUSY"); break;
	case STATUS_ERR_INVALID_ARG:
		Serial.println("Invalid Arg."); break;
	default:
		Serial.print("Unknown 0x"); Serial.println(stat); break;
	}
}


inline uint16_t swapcolor(uint16_t x) { 
	return (x << 11) | (x & 0x07E0) | (x >> 11);
}

static SPISettings mySPISettings;

// Constructor when using hardware SPI.	Faster, but must use SPI pins
// specific to each board type (e.g. 11,13 for Uno, 51,52 for Mega, etc.)
Display_ST7735::Display_ST7735(int8_t cs, int8_t rs, int8_t rst)  : Graphics(ST7735_TFTWIDTH, ST7735_TFTHEIGHT_18) {
	_cs	 = cs;
	_rs	 = rs;
	_rst	= rst;
	_sid	= _sclk = 0;

}

#if defined(CORE_TEENSY) && !defined(__AVR__)
#define __AVR__
#endif

inline void Display_ST7735::spiwrite(uint8_t c) {
	SPI.transfer(c);
}


void Display_ST7735::writecommand(uint8_t c) {
	SPI.beginTransaction(mySPISettings);
	commandMode();

	spiwrite(c);

	idleMode();
	SPI.endTransaction();
}


void Display_ST7735::writedata(uint8_t c) {
	SPI.beginTransaction(mySPISettings);
	dataMode();
		
	//Serial.print("D ");
	spiwrite(c);

	idleMode();
	SPI.endTransaction();
}

// Rather than a bazillion writecommand() and writedata() calls, screen
// initialization commands and arguments are organized in these tables
// stored in PROGMEM.	The table may look bulky, but that's mostly the
// formatting -- storage-wise this is hundreds of bytes more compact
// than the equivalent code.	Companion function follows.
#define DELAY 0x80
static const uint8_t
	Rcmd1[] = {                     // Init for 7735R, part 1 (red or green tab)
		15,                         // 15 commands in list:
		ST7735_SWRESET, DELAY,      //  1: Software reset, 0 args, w/delay
			150,                    //     150 ms delay
		ST7735_SLPOUT, DELAY,       //  2: Out of sleep mode, 0 args, w/delay
			255,                    //     500 ms delay
		ST7735_FRMCTR1, 3,          //  3: Frame rate ctrl - normal mode, 3 args:
			0x01, 0x2C, 0x2D,       //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
		ST7735_FRMCTR2, 3,          //  4: Frame rate control - idle mode, 3 args:
			0x01, 0x2C, 0x2D,       //     Rate = fosc/(1x2+40) * (LINE+2C+2D)
		ST7735_FRMCTR3, 6,          //  5: Frame rate ctrl - partial mode, 6 args:
			0x01, 0x2C, 0x2D,       //     Dot inversion mode
			0x01, 0x2C, 0x2D,       //     Line inversion mode
		ST7735_INVCTR, 1,           //  6: Display inversion ctrl, 1 arg, no delay:
			0x07,                   //     No inversion
		ST7735_PWCTR1, 3,           //  7: Power control, 3 args, no delay:
			0xA2,
			0x02,                   //     -4.6V
			0x84,                   //     AUTO mode
		ST7735_PWCTR2, 1,           //  8: Power control, 1 arg, no delay:
			0xC5,                   //     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD
		ST7735_PWCTR3, 2,           //  9: Power control, 2 args, no delay:
			0x0A,                   //     Opamp current small
			0x00,                   //     Boost frequency
		ST7735_PWCTR4, 2,           // 10: Power control, 2 args, no delay:
			0x8A,                   //     BCLK/2, Opamp current small & Medium low
			0x2A,
		ST7735_PWCTR5, 2,           // 11: Power control, 2 args, no delay:
			0x8A, 0xEE,
		ST7735_VMCTR1, 1,           // 12: Power control, 1 arg, no delay:
			0x0E,
		ST7735_INVOFF, 0,           // 13: Don't invert display, no args, no delay
		ST7735_MADCTL, 1,           // 14: Memory access control (directions), 1 arg:
			0xC8,                   //     row addr/col addr, bottom to top refresh
		ST7735_COLMOD, 1,           // 15: set color mode, 1 arg, no delay:
			0x05 },                 //     16-bit color

	Rcmd2red[] = {                  // Init for 7735R, part 2 (red tab only)
		2,                          //  2 commands in list:
		ST7735_CASET, 4,            //  1: Column addr set, 4 args, no delay:
			0x00, 0x00,             //     XSTART = 0
			0x00, 0x7F,             //     XEND = 127
		ST7735_RASET, 4,            //  2: Row addr set, 4 args, no delay:
			0x00, 0x00,             //     XSTART = 0
			0x00, 0x9F },           //     XEND = 159

	Rcmd3[] = {                     // Init for 7735R, part 3 (red or green tab)
		4,                          //  4 commands in list:
		ST7735_GMCTRP1, 16,         //  1: Magical unicorn dust, 16 args, no delay:
			0x02, 0x1c, 0x07, 0x12,
			0x37, 0x32, 0x29, 0x2d,
			0x29, 0x25, 0x2B, 0x39,
			0x00, 0x01, 0x03, 0x10,
		ST7735_GMCTRN1, 16,         //  2: Sparkles and rainbows, 16 args, no delay:
			0x03, 0x1d, 0x07, 0x06,
			0x2E, 0x2C, 0x29, 0x2D,
			0x2E, 0x2E, 0x37, 0x3F,
			0x00, 0x00, 0x02, 0x10,
		ST7735_NORON, DELAY,        //  3: Normal display on, no args, w/delay
			10,                     //     10 ms delay
		ST7735_DISPON, DELAY,       //  4: Main screen turn on, no args w/delay
			100 };                  //     100 ms delay


// Companion code to the above tables.	Reads and issues
// a series of LCD commands stored in PROGMEM byte array.
void Display_ST7735::commandList(const uint8_t *addr) {

	uint8_t	numCommands, numArgs;
	uint16_t ms;

	numCommands = pgm_read_byte(addr++);	 // Number of commands to follow
	while(numCommands--) {								 // For each command...
		writecommand(pgm_read_byte(addr++)); //	 Read, issue command
		numArgs	= pgm_read_byte(addr++);		//	 Number of args to follow
		ms			 = numArgs & DELAY;					//	 If hibit set, delay follows args
		numArgs &= ~DELAY;									 //	 Mask out delay bit
		while(numArgs--) {									 //	 For each argument...
			writedata(pgm_read_byte(addr++));	//		 Read, issue argument
		}

		if(ms) {
			ms = pgm_read_byte(addr++); // Read post-command delay time (ms)
			if(ms == 255) ms = 500;		 // If 255, delay for 500 ms
			delay(ms);
		}
	}
}


// Initialization code common to both 'B' and 'R' type displays
void Display_ST7735::commonInit(const uint8_t *cmdList) {
	colstart	= rowstart = 0; // May be overridden in init func

	pinMode(_rs, OUTPUT);
	pinMode(_cs, OUTPUT);
	csport = portOutputRegister(digitalPinToPort(_cs));
	rsport = portOutputRegister(digitalPinToPort(_rs));
	cspinmask = digitalPinToBitMask(_cs);
	rspinmask = digitalPinToBitMask(_rs);

	SPI.begin();
	mySPISettings = SPISettings(24000000, MSBFIRST, SPI_MODE0);

	// toggle RST low to reset; CS low so it'll listen to us
	*csport &= ~cspinmask;
	if (_rst) {
		pinMode(_rst, OUTPUT);
		digitalWrite(_rst, HIGH);
		delay(500);
		digitalWrite(_rst, LOW);
		delay(500);
		digitalWrite(_rst, HIGH);
		delay(500);
	}

	if(cmdList) commandList(cmdList);
}


// Initialization for ST7735R screens (green or red tabs)
void Display_ST7735::init() {
	commonInit(Rcmd1);
	// colstart, rowstart left at default '0' values
	commandList(Rcmd2red);
	
	commandList(Rcmd3);

	writecommand(ST7735_MADCTL);
	writedata(0xC0);
}


void Display_ST7735::setAddrWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {

	writecommand(ST7735_CASET); // Column addr set
	writedata(0x00);
	writedata(x0+colstart);		 // XSTART 
	writedata(0x00);
	writedata(x1+colstart);		 // XEND

	writecommand(ST7735_RASET); // Row addr set
	writedata(0x00);
	writedata(y0+rowstart);		 // YSTART
	writedata(0x00);
	writedata(y1+rowstart);		 // YEND

	writecommand(ST7735_RAMWR); // write to RAM
}

//fast method to quickly push a buffered line of pixels
//boundary check must be made prior to this function
//the color must be formated as the destination
void Display_ST7735::drawBufferedLine(int16_t x, int16_t y, uint16_t *buffer, uint16_t w, Image& img) {

	PORT->Group[0].OUTSET.reg = (1 << 17);	// set PORTA.17 high	"digitalWrite(13, HIGH)"

	//create a local buffer line not to mess up the source
	uint16_t bufferedLine[w];
	for (uint16_t i = 0; i < w; i++) {
		uint16_t color = buffer[i];
		color = (color << 8) | (color >> 8); //change endianness
		bufferedLine[i] = color;
	}

	setAddrWindow(x, y, x + w - 1, y + 1);

	//configure DMA
	myDMA.configure_peripheraltrigger(SERCOM4_DMAC_ID_TX);	// SERMCOM4 == SPI native SERCOM
	myDMA.configure_triggeraction(DMA_TRIGGER_ACTON_BEAT);

	//allocate DMA
	myDMA.allocate();
	//printStatus(stat);

	//set up transfer 
	myDMA.setup_transfer_descriptor(bufferedLine,// move data from here
		(void *)(&SERCOM4->SPI.DATA.reg),		// to here
		w * 2,								// this many...
		DMA_BEAT_SIZE_BYTE,						// 8 bits bytes
		true,									// increment source addr?
		false);									// increment dest addr?

	//add descriptor
	//Serial.print("Adding descriptor...");
	myDMA.add_descriptor();
	//printStatus(stat);

	//register and enable call back
	transfer_is_done = false;
	myDMA.register_callback(dma_callback); // by default, called when xfer done
	myDMA.enable_callback(); // by default, for xfer done registers

	//start transfer
	// once started, we dont need to trigger it because it will autorun
	//Serial.println("Starting transfer job");
	SPI.beginTransaction(mySPISettings);
	dataMode();

	myDMA.start_transfer_job();

	while (!transfer_is_done); //chill

	idleMode();
	SPI.endTransaction();
	myDMA.free(); //free the DMA channel

	PORT->Group[0].OUTCLR.reg = (1 << 17); // clear PORTA.17 high "digitalWrite(13, LOW)"
}

//fast method to quickly push a buffered line of pixels
//boundary check must be made prior to this function
//the color must be formated as the destination
void Display_ST7735::drawBuffer(int16_t x, int16_t y, uint16_t *buffer, uint16_t w, uint16_t h) {

	PORT->Group[0].OUTSET.reg = (1 << 17); // set PORTA.17 high	"digitalWrite(13, HIGH)"

	setAddrWindow(x, y, x + w - 1, y + h - 1);

	myDMA.configure_peripheraltrigger(SERCOM4_DMAC_ID_TX); // SERMCOM4 == SPI native SERCOM
	myDMA.configure_triggeraction(DMA_TRIGGER_ACTON_BEAT);

	//allocate DMA
	myDMA.allocate();
	//printStatus(stat);

	//set up transfer 
	myDMA.setup_transfer_descriptor(buffer,// move data from here
		(void *)(&SERCOM4->SPI.DATA.reg),		// to here
		w * h * 2,								// this many...
		DMA_BEAT_SIZE_BYTE,						// 8 bits bytes
		true,									// increment source addr?
		false);									// increment dest addr?

	//add descriptor
	//Serial.print("Adding descriptor...");
	myDMA.add_descriptor();
	//printStatus(stat);

	//register and enable call back
	transfer_is_done = false;
	myDMA.register_callback(dma_callback); // by default, called when xfer done
	myDMA.enable_callback(); // by default, for xfer done registers

							 //start transfer
							 // once started, we dont need to trigger it because it will autorun
							 //Serial.println("Starting transfer job");
	SPI.beginTransaction(mySPISettings);
	dataMode();

	myDMA.start_transfer_job();

	while (!transfer_is_done); //chill

	idleMode();
	SPI.endTransaction();
	myDMA.free(); //free the DMA channel
}

//fast method to quickly push a buffered line of pixels
//boundary check must be made prior to this function
//the color must be formated as the destination
void Display_ST7735::sendBuffer(uint16_t *buffer, uint16_t n) {

	//configure DMA
	myDMA.configure_peripheraltrigger(SERCOM4_DMAC_ID_TX); // SERMCOM4 == SPI native SERCOM
	myDMA.configure_triggeraction(DMA_TRIGGER_ACTON_BEAT);

	//allocate DMA
	myDMA.allocate();
	//printStatus(stat);

	//set up transfer 
	myDMA.setup_transfer_descriptor(buffer,// move data from here
		(void *)(&SERCOM4->SPI.DATA.reg),		// to here
		n * 2,								// this many...
		DMA_BEAT_SIZE_BYTE,						// 8 bits bytes
		true,									// increment source addr?
		false);									// increment dest addr?

	//add descriptor
	//Serial.print("Adding descriptor...");
	myDMA.add_descriptor();
	//printStatus(stat);

	//register and enable call back
	transfer_is_done = false;

	myDMA.register_callback(dma_callback); // by default, called when xfer done
	myDMA.enable_callback();// by default, for xfer done registers

	//start transfer
	// once started, we dont need to trigger it because it will autorun
	//Serial.println("Starting transfer job");
	myDMA.start_transfer_job();
}

uint16_t swap_endians_16(uint16_t b) {
	return (b << 8) | (b >> 8);
}

void Display_ST7735::dataMode() {
	*rsport |= rspinmask;
	*csport &= ~cspinmask;
}

void Display_ST7735::commandMode() {
	*rsport &= ~rspinmask;
	*csport &= ~cspinmask;
}

void Display_ST7735::idleMode() {
	*csport |= cspinmask;
}

void Display_ST7735::drawImage(int16_t x, int16_t y, Image& img){
	img.nextFrame();
	int16_t w = img._width;
	int16_t h = img._height;
	if (w == 0 || h == 0) {
		return;
	}

	if ((img.colorMode == ColorMode::index) && (w == _width) && (h == _height)) {

		uint16_t preBufferLineArray[w];
		uint16_t sendBufferLineArray[w];
		uint16_t *preBufferLine = preBufferLineArray;
		uint16_t *sendBufferLine = sendBufferLineArray;

		//set the window to the whole screen
		setAddrWindow(0, 0, _width - 1, _height - 1);

		//initiate SPI
		SPI.beginTransaction(mySPISettings);
		dataMode();

		//prepare the first line
		indexTo565(preBufferLine, (uint8_t*)img._buffer, Graphics::colorIndex, w, false);
		for (uint16_t i = 0; i < w; i++) { //horizontal coordinate in source image
			uint16_t color = preBufferLine[i];
			preBufferLine[i] = swap_endians_16(color);
		}

		//start sending lines and processing them in parallel using DMA
		for (uint16_t j = 1; j < h; j++) { //vertical coordinate in source image, start from the second line

			//swap buffers pointers
			uint16_t *temp = preBufferLine;
			preBufferLine = sendBufferLine;
			sendBufferLine = temp;
			
			PORT->Group[0].OUTSET.reg = (1 << 17); // set PORTA.17 high	"digitalWrite(13, HIGH)"
			sendBuffer(sendBufferLine, w); //start DMA send


			//prepare the next line while the current one is being transferred

			//length is the number of destination pixels
			uint16_t *dest = preBufferLine;
			uint16_t *src = img._buffer + ((j * w) / 4);
			Color *index = Graphics::colorIndex;
			uint16_t length = w;
			for (uint16_t i = 0; i < length / 4; i++) {
				uint16_t index1 = (src[i] >> 4) & 0x000F;
				uint16_t index2 = (src[i] >> 0) & 0x000F;
				uint16_t index3 = (src[i] >> 12) & 0x000F;
				uint16_t index4 = (src[i] >> 8) & 0x000F;
				//change pixel order (because of words endianness) at the same time
				dest[i * 4] = swap_endians_16((uint16_t)index[index1]);
				dest[(i * 4) + 1] = swap_endians_16((uint16_t)index[index2]);
				dest[(i * 4) + 2] = swap_endians_16((uint16_t)index[index3]);
				dest[(i * 4) + 3] = swap_endians_16((uint16_t)index[index4]);
			}

			PORT->Group[0].OUTCLR.reg = (1 << 17); // clear PORTA.17 high "digitalWrite(13, LOW)"

			while (!transfer_is_done); //chill

			myDMA.free(); //free the DMA channel
		}

		//send the last line
		sendBuffer(preBufferLine, w); //start DMA send
		while (!transfer_is_done); //chill
		myDMA.free(); //free the DMA channel

		//finish SPI
		idleMode();
		SPI.endTransaction();

		return;
	}
	
	Graphics::drawImage(x, y, img); //fallback to the usual
}

void bufferIndexLineDouble(uint16_t* preBufferLine, uint16_t* img_buffer, int16_t w, uint16_t j) {
	int16_t w2 = w*2;
	uint16_t *dest = preBufferLine;
	uint16_t *src = img_buffer + ((j * w) / 4);
	Color *index = Graphics::colorIndex;
	uint16_t length = w;
	for (uint16_t i = 0; i < length / 4; i++) {
		uint16_t index1 = (src[i] >> 4) & 0x000F;
		uint16_t index2 = (src[i] >> 0) & 0x000F;
		uint16_t index3 = (src[i] >> 12) & 0x000F;
		uint16_t index4 = (src[i] >> 8) & 0x000F;
		//change pixel order (because of words endianness) at the same time
		// endians are swapped because SPI sends byte-by-byte instead of word-by-word
		*(dest++) = swap_endians_16((uint16_t)index[index1]);
		*(dest++) = swap_endians_16((uint16_t)index[index1]);
		*(dest++) = swap_endians_16((uint16_t)index[index2]);
		*(dest++) = swap_endians_16((uint16_t)index[index2]);
		*(dest++) = swap_endians_16((uint16_t)index[index3]);
		*(dest++) = swap_endians_16((uint16_t)index[index3]);
		*(dest++) = swap_endians_16((uint16_t)index[index4]);
		*(dest++) = swap_endians_16((uint16_t)index[index4]);
	}
	
	memcpy(&preBufferLine[w2], preBufferLine, w2 * 2); //double the line on the second half of the buffer
}

void Display_ST7735::drawImage(int16_t x, int16_t y, Image& img, int16_t w2, int16_t h2) {
	img.nextFrame();
	//out of screen
	if ((x > _width) || ((x + abs(w2)) < 0) || (y > _height) || ((y + abs(h2)) < 0) || (w2 == 0) || (h2 == 0)) return;

	int16_t w = img._width;
	int16_t h = img._height;
	if (w == 0 || h == 0) {
		return;
	}

	//no scaling
	if ((w == w2) && (h == h2)) { 
		drawImage(x, y, img);
		return;
	}

	//x2 upscaling to full screen
	if ((w2 == (w * 2)) && (h2 == (h * 2)) && (_width == w2) && (_height == h2)) {
		uint16_t preBufferLineArray[w2 * 2];
		uint16_t sendBufferLineArray[w2 * 2];
		uint16_t *preBufferLine = preBufferLineArray;
		uint16_t *sendBufferLine = sendBufferLineArray;
		
		//set the window to the whole screen
		setAddrWindow(0, 0, _width - 1, _height - 1);
		
		//initiate SPI
		SPI.beginTransaction(mySPISettings);
		dataMode();
		if (img.colorMode == ColorMode::rgb565) {

			//prepare the first line
			for (uint16_t i = 0; i < w; i++) { //horizontal coordinate in source image
				uint16_t color = img._buffer[i];
				preBufferLine[i * 2] = preBufferLine[(i * 2) + 1] = swap_endians_16(color);
			}
			memcpy(&preBufferLine[w2], preBufferLine, w2 * 2); //double the line on the second half of the buffer

			//start sending lines and processing them in parallel using DMA
			for (uint16_t j = 1; j < h; j ++) { //vertical coordinate in source image, start from the second line

				//swap buffers pointers
				uint16_t *temp = preBufferLine;
				preBufferLine = sendBufferLine;
				sendBufferLine = temp;

				PORT->Group[0].OUTSET.reg = (1 << 17); // set PORTA.17 high	"digitalWrite(13, HIGH)"

				sendBuffer(sendBufferLine, _width * 2); //start DMA send

				//prepare the next line while the current one is being transferred
				for (uint16_t i = 0; i < w; i ++) { //horizontal coordinate in source image
					uint16_t color = img._buffer[(j * w) + i];
					preBufferLine[i * 2] = preBufferLine[(i * 2) + 1] = swap_endians_16(color);
				}
				memcpy(&preBufferLine[w2], preBufferLine, w2 * 2); //double the line on the second half of the buffer

				PORT->Group[0].OUTCLR.reg = (1 << 17); // clear PORTA.17 high "digitalWrite(13, LOW)"

				while (!transfer_is_done); //chill

				myDMA.free(); //free the DMA channel
			}

			//send the last line
			sendBuffer(preBufferLine, _width * 2); //start DMA send
			while (!transfer_is_done); //chill
			myDMA.free(); //free the DMA channel

			//finish SPI
			idleMode();
			SPI.endTransaction();

			return;
		}
		if (img.colorMode == ColorMode::index) {
			bufferIndexLineDouble(preBufferLine, img._buffer, w, 0);

			//start sending lines and processing them in parallel using DMA
			for (uint16_t j = 1; j < h; j++) { //vertical coordinate in source image, start from the second line

				//swap buffers pointers
				uint16_t *temp = preBufferLine;
				preBufferLine = sendBufferLine;
				sendBufferLine = temp;
				
				PORT->Group[0].OUTSET.reg = (1 << 17); // set PORTA.17 high	"digitalWrite(13, HIGH)"

				sendBuffer(sendBufferLine, _width * 2); //start DMA send

				// prepare the next line while we'r at it
				bufferIndexLineDouble(preBufferLine, img._buffer, w, j);

				PORT->Group[0].OUTCLR.reg = (1 << 17); // clear PORTA.17 high "digitalWrite(13, LOW)"

				while (!transfer_is_done); //chill

				myDMA.free(); //free the DMA channel
			}

			//send the last line
			sendBuffer(preBufferLine, _width * 2); //start DMA send
			while (!transfer_is_done); //chill
			myDMA.free(); //free the DMA channel

			//finish SPI
			idleMode();
			SPI.endTransaction();
			return;
		}
	}

	// fall back to most generic but slow resizing
	Graphics::drawImage(x, y, img, w2, h2);
}

void Display_ST7735::drawImage(int16_t x, int16_t y, Image& img, int16_t x2, int16_t y2, int16_t w2, int16_t h2) {
	Graphics::drawImage(x, y, img, x2, y2, w2, h2);
}



void Display_ST7735::pushColor(uint16_t c) {
	SPI.beginTransaction(mySPISettings);
	dataMode();
	
	spiwrite(c >> 8);
	spiwrite(c);

	idleMode();
	SPI.endTransaction();
}

void Display_ST7735::_drawPixel(int16_t x, int16_t y) {

	if((x < 0) ||(x >= _width) || (y < 0) || (y >= _height)) return;

	setAddrWindow(x,y,x+1,y+1);

	SPI.beginTransaction(mySPISettings);
	dataMode();
	
	spiwrite((uint16_t)color.c >> 8);
	spiwrite((uint16_t)color.c);

	idleMode();
	SPI.endTransaction();
}


void Display_ST7735::drawFastVLine(int16_t x, int16_t y, int16_t h) {
	// Rudimentary clipping
	if((x >= _width) || (y >= _height)) return;
	if((y+h-1) >= _height) h = _height-y;
	setAddrWindow(x, y, x, y+h-1);

	uint8_t hi = (uint16_t)Graphics::color.c >> 8, lo = (uint16_t)Graphics::color.c;
	
	SPI.beginTransaction(mySPISettings);
	dataMode();
	while (h--) {
		spiwrite(hi);
		spiwrite(lo);
	}
	idleMode();
	SPI.endTransaction();
}


void Display_ST7735::drawFastHLine(int16_t x, int16_t y, int16_t w) {
	// Rudimentary clipping
	if((x >= _width) || (y >= _height)) return;
	if((x+w-1) >= _width)	w = _width-x;
	setAddrWindow(x, y, x+w-1, y);

	uint8_t hi = (uint16_t)Graphics::color.c >> 8, lo = (uint16_t)Graphics::color.c;

	SPI.beginTransaction(mySPISettings);
	dataMode();
	while (w--) {
		spiwrite(hi);
		spiwrite(lo);
	}
	idleMode();
	SPI.endTransaction();
}

// fill a rectangle
void Display_ST7735::fillRect(int16_t x, int16_t y, int16_t w, int16_t h) {
	// rudimentary clipping (drawChar w/big text requires this)
	if((x >= _width) || (y >= _height)) return;
	if((x + w - 1) >= _width)	w = _width	- x;
	if((y + h - 1) >= _height) h = _height - y;

	setAddrWindow(x, y, x+w-1, y+h-1);

	uint8_t hi = (uint16_t)Graphics::color.c >> 8, lo = (uint16_t)Graphics::color.c;
	
	SPI.beginTransaction(mySPISettings);
	dataMode();
	for(y=h; y>0; y--) {
		for(x=w; x>0; x--) {
			spiwrite(hi);
			spiwrite(lo);
		}
	}

	idleMode();
	SPI.endTransaction();
}


#define MADCTL_MY	0x80
#define MADCTL_MX	0x40
#define MADCTL_MV	0x20
#define MADCTL_ML	0x10
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08
#define MADCTL_MH	0x04

void Display_ST7735::setRotation(Rotation m) {
	writecommand(ST7735_MADCTL);
	rotation = m;
	switch (rotation) {
		case Rotation::left:
			writedata(MADCTL_MX | MADCTL_MY | MADCTL_RGB);
			_width = ST7735_TFTWIDTH;
			_height = ST7735_TFTHEIGHT_18;
			break;
		case Rotation::up:
			writedata(MADCTL_MY | MADCTL_MV | MADCTL_RGB);
			_width = ST7735_TFTHEIGHT_18;
			_height = ST7735_TFTWIDTH;
			break;
		case Rotation::right:
			writedata(MADCTL_RGB);
			_width	= ST7735_TFTWIDTH;
			_height = ST7735_TFTHEIGHT_18;
			break;
		case Rotation::down:
			writedata(MADCTL_MX | MADCTL_MV | MADCTL_RGB);
			_width = ST7735_TFTHEIGHT_18;
			_height = ST7735_TFTWIDTH;
			break;
	}
}

Rotation Display_ST7735::getRotation() {
	return rotation;
}


void Display_ST7735::invertDisplay(boolean i) {
	writecommand(i ? ST7735_INVON : ST7735_INVOFF);
}

} // namespace Gamebuino_Meta
