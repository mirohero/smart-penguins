////////////////////////////////////////////////////////////////////////////////
// /****************************************************************************
// **
// ** Copyright (C) 2015-2019 M-Way Solutions GmbH
// ** Contact: https://www.blureange.io/licensing
// **
// ** This file is part of the Bluerange/FruityMesh implementation
// **
// ** $BR_BEGIN_LICENSE:GPL-EXCEPT$
// ** Commercial License Usage
// ** Licensees holding valid commercial Bluerange licenses may use this file in
// ** accordance with the commercial license agreement provided with the
// ** Software or, alternatively, in accordance with the terms contained in
// ** a written agreement between them and M-Way Solutions GmbH. 
// ** For licensing terms and conditions see https://www.bluerange.io/terms-conditions. For further
// ** information use the contact form at https://www.bluerange.io/contact.
// **
// ** GNU General Public License Usage
// ** Alternatively, this file may be used under the terms of the GNU
// ** General Public License version 3 as published by the Free Software
// ** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
// ** included in the packaging of this file. Please review the following
// ** information to ensure the GNU General Public License requirements will
// ** be met: https://www.gnu.org/licenses/gpl-3.0.html.
// **
// ** $BR_END_LICENSE$
// **
// ****************************************************************************/
////////////////////////////////////////////////////////////////////////////////

#include <Logger.h>
#include <Terminal.h>
#include <Config.h>
#include <Utility.h>
#include <mini-printf.h>
#include <GlobalState.h>

#ifdef SIM_ENABLED
#include <CherrySim.h>
#include <mutex>
#include <condition_variable>
#include <chrono>
static std::mutex terminalMutex;
static std::condition_variable bufferFree;
#endif

#if defined(__unix)
#include <stdio.h>
#include <unistd.h>
#endif


extern "C"
{
#if IS_ACTIVE(UART)
#include "app_util_platform.h"
#include "nrf_uart.h"
#endif
#if IS_ACTIVE(SEGGER_RTT)
#include "SEGGER_RTT.h"
#endif
#if IS_ACTIVE(STDIO)
#ifdef _WIN32
#include <conio.h>
#else
#undef trace
#include <ncurses.h>
#define trace(message, ...) 
#endif
#endif
}

// ######################### GENERAL
#define ________________GENERAL___________________

Terminal::Terminal(){
	registeredCallbacksNum = 0;
	terminalIsInitialized = false;
}

//Initialize the mhTerminal
void Terminal::Init()
{
#ifdef TERMINAL_ENABLED
#if defined(__unix) && !defined(SIM_ENABLED)
    initscr();
    cbreak();
    noecho();
    scrollok(stdscr, TRUE);
    nodelay(stdscr, TRUE);
#endif //defined(__unix) && !defined(SIM_ENABLED)
	//UART
	uartActive = false;
	lineToReadAvailable = false;
	readBufferOffset = 0;

	registeredCallbacksNum = 0;
	CheckedMemset(&registeredCallbacks, 0x00, sizeof(registeredCallbacks));

#if IS_ACTIVE(UART)
	if(Conf::getInstance().terminalMode != TerminalMode::DISABLED){
		UartEnable(Conf::getInstance().terminalMode == TerminalMode::PROMPT);
	}
	GS->SetUartHandler([]()->void {
		Terminal::getInstance().UartInterruptHandler();
	});
#endif
#if IS_ACTIVE(SEGGER_RTT)
	SeggerRttInit();
#endif
#if IS_ACTIVE(STDIO)
	StdioInit();
#endif

	terminalIsInitialized = true;

#if IS_INACTIVE(GW_SAVE_SPACE)
	char versionString[15];
	Utility::GetVersionStringFromInt(GS->config.getFruityMeshVersion(), versionString);

	if (Conf::getInstance().terminalMode == TerminalMode::PROMPT)
	{
		//Send Escape sequence
		log_transport_put(27); //ESC
		log_transport_putstring("[2J"); //Clear Screen
		log_transport_put(27); //ESC
		log_transport_putstring("[H"); //Cursor to Home

		//Send App start header
		log_transport_putstring("--------------------------------------------------" EOL);
		log_transport_putstring("Terminal started, compile date: ");
		log_transport_putstring(__DATE__);
		log_transport_putstring("  ");
		log_transport_putstring(__TIME__);
		log_transport_putstring(", version: ");
		log_transport_putstring(versionString);

#ifdef NRF52
		log_transport_putstring(", nRF52");
#else
		log_transport_putstring(", nRF51");
#endif

		if(RamConfig->deviceConfigOrigin == DeviceConfigOrigins::RANDOM_CONFIG){
			log_transport_putstring(", RANDOM Config");
		} else if(RamConfig->deviceConfigOrigin == DeviceConfigOrigins::UICR_CONFIG){
			log_transport_putstring(", UICR Config");
		} else if(RamConfig->deviceConfigOrigin == DeviceConfigOrigins::TESTDEVICE_CONFIG){
			log_transport_putstring(", TESTDEVICE Config");
		}


		log_transport_putstring(EOL "--------------------------------------------------" EOL);
	} else {
		
	}
#endif
#endif //IS_ACTIVE(UART)
}

Terminal & Terminal::getInstance()
{
	return GS->terminal;
}

//Register a string that will call the callback function with the rest of the string
void Terminal::AddTerminalCommandListener(TerminalCommandListener* callback)
{
#ifdef TERMINAL_ENABLED
	if (registeredCallbacksNum >= MAX_TERMINAL_COMMAND_LISTENER_CALLBACKS) {
		SIMEXCEPTION(TooManyTerminalCommandListenersException); //LCOV_EXCL_LINE assertion
	}
	registeredCallbacks[registeredCallbacksNum] = callback;
	registeredCallbacksNum++;
#endif
}

void Terminal::PutString(const char* buffer)
{
	if(!terminalIsInitialized) return;

#if IS_ACTIVE(UART)
	UartPutStringBlockingWithTimeout(buffer);
#endif
#if IS_ACTIVE(SEGGER_RTT)
	Terminal::SeggerRttPutString(buffer);
#endif
#if IS_ACTIVE(STDIO)
	Terminal::StdioPutString(buffer);
#endif
}

void Terminal::PutChar(const char character)
{
	if(!terminalIsInitialized) return;

#if IS_ACTIVE(UART)
	UartPutCharBlockingWithTimeout(character);
#endif
#if IS_ACTIVE(SEGGER_RTT)
	SeggerRttPutChar(character);
#endif
}

char ** Terminal::getCommandArgsPtr()
{
	return commandArgsPtr;
}

u8 Terminal::getAmountOfRegisteredCommandListeners()
{
	return registeredCallbacksNum;
}

TerminalCommandListener ** Terminal::getRegisteredCommandListeners()
{
	return registeredCallbacks;
}

u8 Terminal::getReadBufferOffset()
{
	return readBufferOffset;
}

char * Terminal::getReadBuffer()
{
	return readBuffer;
}

// Checks all transports if a line is available (or retrieves a line)
// Then processes it
void Terminal::CheckAndProcessLine()
{
	if(!terminalIsInitialized) return;

#if IS_ACTIVE(UART)
	UartCheckAndProcessLine();
#endif
#if IS_ACTIVE(SEGGER_RTT)
	SeggerRttCheckAndProcessLine();
#endif
#if IS_ACTIVE(STDIO)
	StdioCheckAndProcessLine();
#endif
}

//Processes a line (give to all handlers and print response)
void Terminal::ProcessLine(char* line)
{
#ifdef TERMINAL_ENABLED
	//Log the input
	//logt("ERROR", "input:%s", line);


	//Tokenize input string into vector

	u16 size = (u16)strlen(line);
	i32 commandArgsSize = TokenizeLine(line, size);
	if (commandArgsSize < 0) {
		if (Conf::getInstance().terminalMode == TerminalMode::PROMPT) {
			log_transport_putstring("Too many arguments!" EOL);
		}
		else {
			logjson_error(Logger::UartErrorType::TOO_MANY_ARGUMENTS);
		}
		return;
	}

	//Call all callbacks
	int handled = 0;

	for(u32 i=0; i<MAX_TERMINAL_COMMAND_LISTENER_CALLBACKS; i++){
		if(registeredCallbacks[i]){
			handled += (registeredCallbacks[i])->TerminalCommandHandler(commandArgsPtr, (u8)commandArgsSize);
		}
	}

	//Output result
	if (handled == 0){
		if(Conf::getInstance().terminalMode == TerminalMode::PROMPT){
			log_transport_putstring("Command not found" EOL);
		} else {
			logjson_error(Logger::UartErrorType::COMMAND_NOT_FOUND);
		}
#ifdef CHERRYSIM_TESTER_ENABLED
		SIMEXCEPTION(CommandNotFoundException);
#endif
	} else if(Conf::getInstance().terminalMode == TerminalMode::JSON){
		logjson_error(Logger::UartErrorType::SUCCESS);
	}
#endif
}

i32 Terminal::TokenizeLine(char* line, u16 lineLength)
{
	CheckedMemset(commandArgsPtr, 0, MAX_NUM_TERM_ARGS * sizeof(char*));

	commandArgsPtr[0] = &(line[0]);
	i32 commandArgsSize = 1;

	for(u32 i=0; i<lineLength; i++){
		if (line[i] == ' ' && line[i+1] > '!' && line[i+1] < '~') {
			if (commandArgsSize >= MAX_NUM_TERM_ARGS) {
				SIMEXCEPTION(TooManyArgumentsException); //LCOV_EXCL_LINE assertion
				return -1;								 //LCOV_EXCL_LINE assertion
			}
			commandArgsPtr[commandArgsSize] = &line[i+1];
			line[i] = '\0';
			commandArgsSize++;
		}
	}

	return commandArgsSize;
}

// ############################### UART
// Uart communication expects a \r delimiter after a line to process the command
// Results such as JSON objects are delimtied by \r\n

#define ________________UART___________________
#if IS_ACTIVE(UART)


void Terminal::UartDisable()
{
	//Disable UART interrupt
	sd_nvic_DisableIRQ(UART0_IRQn);

	//Disable all UART Events
	nrf_uart_int_disable(NRF_UART0, NRF_UART_INT_MASK_RXDRDY |
									NRF_UART_INT_MASK_TXDRDY |
									NRF_UART_INT_MASK_ERROR  |
									NRF_UART_INT_MASK_RXTO);
	//Clear all pending events
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_CTS);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_NCTS);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXDRDY);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_TXDRDY);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_ERROR);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXTO);

	//Disable UART
	NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Disabled;

	//Reset all Pinx to default state
	nrf_uart_txrx_pins_disconnect(NRF_UART0);
	nrf_uart_hwfc_pins_disconnect(NRF_UART0);

	nrf_gpio_cfg_default(Boardconfig->uartTXPin);
	nrf_gpio_cfg_default(Boardconfig->uartRXPin);

	if(Boardconfig->uartRTSPin != -1){
		if (NRF_UART0->PSELRTS != NRF_UART_PSEL_DISCONNECTED) nrf_gpio_cfg_default(Boardconfig->uartRTSPin);
		if (NRF_UART0->PSELCTS != NRF_UART_PSEL_DISCONNECTED) nrf_gpio_cfg_default(Boardconfig->uartCTSPin);
	}
}

void Terminal::UartEnable(bool promptAndEchoMode)
{
	u32 err = 0;

	if(Boardconfig->uartRXPin == -1) return;

	//Disable UART if it was active before
	UartDisable();

	//Delay to fix successive stop or startterm commands
	FruityHal::DelayMs(10);

	readBufferOffset = 0;
	lineToReadAvailable = false;

	//Configure pins
	nrf_gpio_pin_set(Boardconfig->uartTXPin);
	nrf_gpio_cfg_output(Boardconfig->uartTXPin);
	nrf_gpio_cfg_input(Boardconfig->uartRXPin, NRF_GPIO_PIN_NOPULL);

	nrf_uart_baudrate_set(NRF_UART0, (nrf_uart_baudrate_t) Boardconfig->uartBaudRate);
	nrf_uart_configure(NRF_UART0, NRF_UART_PARITY_EXCLUDED, Boardconfig->uartRTSPin != -1 ? NRF_UART_HWFC_ENABLED : NRF_UART_HWFC_DISABLED);
	nrf_uart_txrx_pins_set(NRF_UART0, Boardconfig->uartTXPin, Boardconfig->uartRXPin);

	//Configure RTS/CTS (if RTS is -1, disable flow control)
	if(Boardconfig->uartRTSPin != -1){
		nrf_gpio_cfg_input(Boardconfig->uartCTSPin, NRF_GPIO_PIN_NOPULL);
		nrf_gpio_pin_set(Boardconfig->uartRTSPin);
		nrf_gpio_cfg_output(Boardconfig->uartRTSPin);
		nrf_uart_hwfc_pins_set(NRF_UART0, Boardconfig->uartRTSPin, Boardconfig->uartCTSPin);
	}

	//Enable Interrupts + timeout events
	if(!promptAndEchoMode){
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXTO);
		nrf_uart_int_enable(NRF_UART0, NRF_UART_INT_MASK_RXTO);

		sd_nvic_SetPriority(UART0_IRQn, APP_IRQ_PRIORITY_LOW);
		sd_nvic_ClearPendingIRQ(UART0_IRQn);
		sd_nvic_EnableIRQ(UART0_IRQn);
	}

	//Enable UART
	nrf_uart_enable(NRF_UART0);

	//Enable Receiver
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_ERROR);
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXDRDY);
	nrf_uart_task_trigger(NRF_UART0, NRF_UART_TASK_STARTRX);

	//Enable Transmitter
	nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_TXDRDY);
	nrf_uart_task_trigger(NRF_UART0, NRF_UART_TASK_STARTTX);

	uartActive = true;

	//Start receiving RX events
	if(!promptAndEchoMode){
		UartEnableReadInterrupt();
	}
}

//Checks whether a character is waiting on the input line
void Terminal::UartCheckAndProcessLine(){
	//Check if a line is available
	if(Conf::getInstance().terminalMode == TerminalMode::PROMPT && UartCheckInputAvailable()){
		UartReadLineBlocking();
	}

	//Check if a line is available either through blocking or interrupt mode
	if(!lineToReadAvailable) return;

	//Set uart active if input was received
	uartActive = true;

	//Some special stuff
	if (strcmp(readBuffer, "cls") == 0)
	{
		//Send Escape sequence
		UartPutCharBlockingWithTimeout(27); //ESC
		UartPutStringBlockingWithTimeout("[2J"); //Clear Screen
		UartPutCharBlockingWithTimeout(27); //ESC
		UartPutStringBlockingWithTimeout("[H"); //Cursor to Home
	}
#if IS_INACTIVE(GW_SAVE_SPACE)
	else if(strcmp(readBuffer, "startterm") == 0){
		Conf::getInstance().terminalMode = TerminalMode::PROMPT;
		UartEnable(true);
		return;
	}
#endif
	else if(strcmp(readBuffer, "stopterm") == 0){
		Conf::getInstance().terminalMode = TerminalMode::JSON;
		UartEnable(false);
		return;
	}
	else
	{
		ProcessLine(readBuffer);
	}

	//Reset buffer
	readBufferOffset = 0;
	lineToReadAvailable = false;

	//Re-enable Read interrupt after line was processed
	if(Conf::getInstance().terminalMode != TerminalMode::PROMPT){
		UartEnableReadInterrupt();
	}
}

void Terminal::UartHandleError(u32 error)
{
	//Errorsource is given, but has to be cleared to be handled
	NRF_UART0->ERRORSRC = error;

	//SeggerRttPrintf("ERROR %d, ", error);

	readBufferOffset = 0;

	//FIXME: maybe we need some better error handling here
}


//############################ UART_BLOCKING_READ
#define ___________UART_BLOCKING_READ______________


bool Terminal::UartCheckInputAvailable()
{
	if(NRF_UART0->EVENTS_RXDRDY == 1) uartActive = true;
	//SeggerRttPrintf("[%d]", uartActive);
	return NRF_UART0->EVENTS_RXDRDY == 1;
}

// Reads a String from UART (until the user has pressed ENTER)
// and provides a nice terminal emulation
// ATTENTION: If no system events are fired, this function will never execute as
// a non-interrupt driven UART will not generate an event
void Terminal::UartReadLineBlocking()
{
#if IS_INACTIVE(GW_SAVE_SPACE)
	if (!uartActive)
		return;

	UartPutStringBlockingWithTimeout("mhTerm: ");

	u8 byteBuffer = 0;

	//Read in an infinite loop until \r is recognized
	while (true)
	{
		//Read a byte from UART
		byteBuffer = UartReadCharBlocking();

		//BACKSPACE
		if (byteBuffer == 127)
		{
			if (readBufferOffset > 0)
			{
				//Output Backspace
				UartPutCharBlockingWithTimeout(byteBuffer);

				readBuffer[readBufferOffset - 1] = 0;
				readBufferOffset--;
			}
		}
		//ALL OTHER CHARACTERS
		else
		{
			//Display entered character in terminal
			UartPutCharBlockingWithTimeout(byteBuffer);

			if (byteBuffer == '\r' || readBufferOffset >= READ_BUFFER_LENGTH - 1)
			{
				readBuffer[readBufferOffset] = '\0';
				UartPutStringBlockingWithTimeout(EOL);
				if(readBufferOffset > 0) lineToReadAvailable = true;
				break;
			}
			else
			{
				memcpy(readBuffer + readBufferOffset, &byteBuffer, sizeof(u8));
			}

			readBufferOffset++;
		}
	}
#endif
}

char Terminal::UartReadCharBlocking()
{
#if IS_INACTIVE(GW_SAVE_SPACE)
	int i=0;
	while (NRF_UART0->EVENTS_RXDRDY != 1){
		if(NRF_UART0->EVENTS_ERROR){
			UartHandleError(NRF_UART0->ERRORSRC);
		}
		// Info: No timeout neede here, as we are waiting for user input
	}
	NRF_UART0->EVENTS_RXDRDY = 0;
	return NRF_UART0->RXD;

#else
	return 0;
#endif
}

//############################ UART_BLOCKING_WRITE
#define ___________UART_BLOCKING_WRITE______________

void Terminal::UartPutStringBlockingWithTimeout(const char* message)
{
	//SeggerRttPrintf("TX <");
	if(!uartActive) return;

	uint_fast8_t i  = 0;
	uint8_t byte = message[i++];

	while (byte != '\0')
	{
		NRF_UART0->TXD = byte;
		byte = message[i++];

		int i=0;
		while (NRF_UART0->EVENTS_TXDRDY != 1){
			//Timeout if it was not possible to put the character
			if(i > 10000){
				return;
			}
			i++;
			//FIXME: Do we need error handling here? Will cause lost characters
		}
		NRF_UART0->EVENTS_TXDRDY = 0;
	}

	//SeggerRttPrintf("> TX, ");
}

void Terminal::UartPutCharBlockingWithTimeout(const char character)
{
	char tmp[2] = {character, '\0'};
	UartPutStringBlockingWithTimeout(tmp);
}

//############################ UART_NON_BLOCKING_READ
#define _________UART_NON_BLOCKING_READ____________


void Terminal::UartInterruptHandler()
{
	if(!uartActive) return;

	//SeggerRttPrintf("Intrpt <");
	//Checks if an error occured
	if (nrf_uart_int_enable_check(NRF_UART0, NRF_UART_INT_MASK_ERROR) &&
		nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_ERROR))
	{
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_ERROR);

		UartHandleError(NRF_UART0->ERRORSRC);
	}

	//Checks if the receiver received a new byte
	if (nrf_uart_int_enable_check(NRF_UART0, NRF_UART_INT_MASK_RXDRDY) &&
			 nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_RXDRDY))
	{
		//Reads the byte
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXDRDY);
		char byte = NRF_UART0->RXD;

		//Disable the interrupt to stop receiving until instructed further
		nrf_uart_int_disable(NRF_UART0, NRF_UART_INT_MASK_RXDRDY | NRF_UART_INT_MASK_ERROR);

		//Tell somebody that we received something
		UartHandleInterruptRX(byte);
	}

	//Checks if a timeout occured
	if (nrf_uart_event_check(NRF_UART0, NRF_UART_EVENT_RXTO))
	{
		nrf_uart_event_clear(NRF_UART0, NRF_UART_EVENT_RXTO);

		readBufferOffset = 0;

		//Restart transmission and clear previous buffer
		nrf_uart_task_trigger(NRF_UART0, NRF_UART_TASK_STARTRX);

		//TODO: can we check if this works???
	}

	//SeggerRttPrintf("> Intrpt, ");
}
void Terminal::UartHandleInterruptRX(char byte)
{
	//Set uart active if input was received
	uartActive = true;

	//Read the received byte
	readBuffer[readBufferOffset] = byte;
	readBufferOffset++;

	//If the line is finished, it should be processed before additional data is read
	if(byte == '\r' || readBufferOffset >= READ_BUFFER_LENGTH - 1)
	{
		readBuffer[readBufferOffset-1] = '\0';
		lineToReadAvailable = true; //Should be the last statement
		// => next, the main event loop will process the line from the main context
	}
	//Otherwise, we keep reading more bytes
	else
	{
		UartEnableReadInterrupt();
	}
}

void Terminal::UartEnableReadInterrupt()
{
	//SeggerRttPrintf("RX Inerrupt enabled, ");
	nrf_uart_int_enable(NRF_UART0, NRF_UART_INT_MASK_RXDRDY | NRF_UART_INT_MASK_ERROR);
}
#endif
//############################ SEGGER RTT
#define ________________SEGGER_RTT___________________

#if IS_ACTIVE(SEGGER_RTT)
void Terminal::SeggerRttInit()
{

}

void Terminal::SeggerRttCheckAndProcessLine()
{
	if(SEGGER_RTT_HasKey()){
		int seggerKey = 0;
		while(seggerKey != '\r' && seggerKey != '\n' && seggerKey != '#' && readBufferOffset < READ_BUFFER_LENGTH - 1){
			seggerKey = SEGGER_RTT_GetKey();
			if(seggerKey < 0) continue;
			readBuffer[readBufferOffset] = (char)seggerKey;
			readBufferOffset++;
		}
		readBuffer[readBufferOffset-1] = '\0';
		lineToReadAvailable = true;

		ProcessLine(readBuffer);

		//Reset buffer
		readBufferOffset = 0;
		lineToReadAvailable = false;
	}
}

void Terminal::SeggerRttPrintf(const char* message, ...)
{
	char tmp[250];
	CheckedMemset(tmp, 0, 250);

	//Variable argument list must be passed to vnsprintf
	va_list aptr;
	va_start(aptr, message);
	vsnprintf(tmp, 250, message, aptr);
	va_end(aptr);

	SeggerRttPutString(tmp);
}

void Terminal::SeggerRttPutString(const char*message)
{
	SEGGER_RTT_WriteString(0, (const char*) message);
}

void Terminal::SeggerRttPutChar(char character)
{
	u8 buffer[1];
	buffer[0] = character;
	SEGGER_RTT_Write(0, (const char*)buffer, 1);
}
#endif


//############################ STDIO
#define ________________STDIO___________________
#if IS_ACTIVE(STDIO)
#ifndef _WIN32
static int _kbhit(void)
{
    int ch = getch();

    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else {
        return 0;
    }
}
#endif

void Terminal::StdioInit()
{
	setbuf(stdout, nullptr);
}

void Terminal::WriteStdioLineToReadBuffer() {
	size_t i;
#ifdef __unix
	nodelay(stdscr, FALSE);
#endif
	for (i = 0; i < READ_BUFFER_LENGTH - 1; i++) {
		int c = fgetc(stdin);
		if(c == EOF) break;

		readBuffer[i] = (char)c;

		if(c == '\n') break;
	}
#ifdef __unix
	nodelay(stdscr, TRUE);
#endif
	readBuffer[i] = '\0';
}

extern bool meshGwCommunication;

//Used to inject a message into the readBuffer directly
bool Terminal::PutIntoReadBuffer(const char* message)
{
#ifdef SIM_ENABLED
	std::unique_lock<std::mutex> guard(terminalMutex);
#endif
	u16 len = (u16)(strlen(message) + 1);

	if (meshGwCommunication)
	{
		//Loop to catch spurious wakeups as well as timeouts.
		while (readBufferOffset != 0)
		{
			bufferFree.wait_for(guard, std::chrono::seconds(1));
		}
	}
	else if (readBufferOffset != 0)
	{
		// You need to simulate before sending another command!
		SIMEXCEPTION(CommandbufferAlreadyInUseException);
		return false;
	}
	if (len >= READ_BUFFER_LENGTH)
	{
		SIMEXCEPTION(CommandTooLongException);
		return false;
	}

	memcpy(readBuffer, message, len);
	readBufferOffset = (u8)len;

	return true;
}

void Terminal::StdioCheckAndProcessLine()
{
#ifdef SIM_ENABLED
	std::lock_guard<std::mutex> guard(terminalMutex);
#endif
	if (cherrySimInstance->simConfig.terminalId != cherrySimInstance->currentNode->id && cherrySimInstance->simConfig.terminalId != 0) return;

#if (defined(__unix) && !defined(CHERRYSIM_TESTER_ENABLED)) || defined(_WIN32)
	if(!meshGwCommunication && _kbhit() != 0){ //FIXME: Not supported by eclipse console
		printf("mhTerm: ");
		WriteStdioLineToReadBuffer();

		Terminal::ProcessLine(readBuffer);
	}
	//Also process data that was written in the readBuffer
	else 
#endif
	if (readBufferOffset != 0) {
		if(cherrySimInstance->simConfig.verboseCommands) printf("mhTerm: %s" EOL, readBuffer);
		Terminal::ProcessLine(readBuffer);
		readBufferOffset = 0;
		bufferFree.notify_one();
	}
}

void Terminal::StdioPutString(const char*message)
{
	cherrySimInstance->TerminalPrintHandler(message);
}

#endif
