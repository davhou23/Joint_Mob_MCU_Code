/*
 * Created: 4/6/2022 10:55:16 AM
 * Author : David
 */ 

#define F_CPU 20000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <string.h>
#include <avr/eeprom.h>
#include <stdlib.h>
#include <stdio.h>

//Functions
void send_data(char *data);
unsigned char reverse(unsigned char b);

//ACCELEROMETER COMMANDS
char ping[] = "[1\r";
char request_number[] = "[N?\r";
char baud_rate[] = "[1B9\r";
char baud_save[] = "[1BFW\r";
char data_rate[] = "[1D100\r";
char range[] = "[1AR2\r";
char zero[] = "[1ZA\r";
char output[] = "[1MAC\r";
char serial[] = "[1S\r";
char inc_off[] = "[1MIX\r";

unsigned char acc_data[50];
uint8_t TX0_counter = 0;

//Data Variables
double X_DATA[8];
double Y_DATA[8];
double Z_DATA[8];
uint16_t PRESS_DATA[8];
double x_average = 0;
double y_average = 0;
double z_average = 0;
uint16_t pressure_avg = 0;
uint16_t pressure = 0; 

//Flags
volatile bool calculate = false;
volatile bool last_byte_sent = false;

int main(void)
{	
	//Disable Prescaler Clock
	_PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0<<CLKCTRL_PEN_bp);
	
	/***********USART0**********/
	USART0.CTRLB |= (USART_TXEN_bm);
	
	USART0.CTRLC |= (USART_CHSIZE0_bm | USART_CHSIZE1_bm);
	
	USART0.BAUD = 694; //115200 bits per second
	
	/***********USART1**********/
	USART1.CTRLA |= (USART_RXCIE_bm);
	
	USART1.CTRLB |= (USART_TXEN_bm | USART_RXEN_bm);
	
	USART1.CTRLC |= (USART_CHSIZE0_bm | USART_CHSIZE1_bm);
	
	USART1.BAUD = 694; //115200 bits per second
	
	/*************ADC*************/
	ADC0.CTRLA |= (ADC_ENABLE_bm | ADC_FREERUN_bm);
	
	ADC0.CTRLB |= (ADC_SAMPNUM_ACC8_gc);
	
	ADC0.CTRLC |= (ADC_SAMPCAP_bm | ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV256_gc);
	
	ADC0.CTRLD |= (ADC_INITDLY_DLY256_gc);
	
	ADC0.SAMPCTRL |= (ADC_SAMPLEN2_bm);
	
	ADC0.INTCTRL |= (ADC_RESRDY_bm);
	
	ADC0.COMMAND |= (ADC_STCONV_bm);
	
	/**********PORT STUFF**********/
	
	//UART0 Transmitter
	PORTA.DIR |= 1<<PORT0; //TX
	
	//USART1 Transmitter and Receiver
	PORTC.DIR |= 1<<PORT0; //TX
	PORTC.DIR &= ~(1<<PIN1); //RX
	
	_delay_ms(500);
	
	send_data(request_number);
	send_data(serial);
	send_data(output);
	send_data(inc_off);
	send_data(data_rate); //Change data rate to 40Hz
	send_data(range); //Set the range to 2g
	_delay_ms(1000);
	send_data(zero); //Zero out the accelerometer
	
	_delay_ms(1000); //Wait for all of the output responses to go through
	
	//Enable Global Interrupt
	SREG |= (1<<SREG_I);
	
    while (1)
    {
		if (calculate)
		{
			//If the acc_data does not start with a $,
			//then get out of calculate
			if (strtok(acc_data, '$') == NULL) break;
			
			char temp[10];
			temp[0] = '\0';
			char data[3][10];
			double double_data[3];
			
			//Clear Data
			for (int i = 0; i < sizeof(data)/sizeof(char); i++){
				//memcpy(data[i], " ", strlen(data[i]));
				strcpy(data[i], "          ");
			}
			
			/**********Separate the data************/
			strcpy(acc_data, strstr(acc_data, ",")); //Get rid of header
			memmove(acc_data,acc_data+1,strlen(acc_data)-1); //Shorten length by 1
			
			for(int i = 0; i < 2; i++){ //Keep running until string is gone
				//memccpy, memchr, 
				strcpy(data[i], strtok(acc_data, ",")); //Separate and save acceleration data
				memcpy(strchr(acc_data, '\0'),",",1);
				
				//Delete Data in String
				strcpy(acc_data, strstr(acc_data, ",")); //Get rid of header
				memmove(acc_data,acc_data+1,strlen(acc_data)-1); //Shorten length by 1
			}
			
			//Z Data
			//sprintf?
			strcpy(data[2], strtok(acc_data, "*")); //Separate and save acceleration data
			
			//Convert into doubles
			for (int i = 0; i < sizeof(double_data)/sizeof(double); i++){
				double_data[i] = atof(data[i]); //Convert to double
			}
			
			/**************Shift Data*************/
			//X
			for (int i = 0; i < (sizeof(X_DATA)/sizeof(X_DATA[0])-1); i++){
				X_DATA[i] = X_DATA[i+1];
			}
			X_DATA[(sizeof(X_DATA)/sizeof(X_DATA[0]))-1] = double_data[0];
			
			//Y
			for (int i = 0; i < (sizeof(Y_DATA)/sizeof(Y_DATA[0])-1); i++){
				Y_DATA[i] = Y_DATA[i+1];
			}
			Y_DATA[(sizeof(Y_DATA)/sizeof(Y_DATA[0]))-1] = double_data[1];
			
			//Z
			for (int i = 0; i < (sizeof(Z_DATA)/sizeof(Z_DATA[0])-1); i++){
				Z_DATA[i] = Z_DATA[i+1];
			}
			Z_DATA[(sizeof(Z_DATA)/sizeof(Z_DATA[0]))-1] = double_data[2];
			
			//Pressure
			for (int i = 0; i < (sizeof(PRESS_DATA)/sizeof(PRESS_DATA[0])-1); i++){
				PRESS_DATA[i] = PRESS_DATA[i+1];
			}
			PRESS_DATA[(sizeof(PRESS_DATA)/sizeof(PRESS_DATA[0]))-1] = pressure;
			
			/***********Average out*************/
			double total = 0;
			
			//X
			for (int i = 0; i < sizeof(X_DATA)/sizeof(X_DATA[0]); i++){
				total += X_DATA[i];
			}
			
			x_average = total/8;
			total = 0;
			
			//Y
			for (int i = 0; i < sizeof(Y_DATA)/sizeof(Y_DATA[0]); i++){
				total += Y_DATA[i];
			}
			
			y_average = total/8;
			total = 0;
			
			//Z
			for (int i = 0; i < sizeof(Z_DATA)/sizeof(Z_DATA[0]); i++){
				total += Z_DATA[i];
			}
			
			z_average = total/8;
			total = 0;
			
			//Pressure
			for (int i = 0; i < sizeof(PRESS_DATA)/sizeof(PRESS_DATA[0]); i++){
				total += PRESS_DATA[i];
			}
			
			pressure_avg = total/8;
			total = 0;
			
			/***********Turn back into String***********/
			//temp = "";			
			char average_out[50];
			average_out[0] = '\0';
			
			sprintf(temp, "%lf", x_average);
			strcat(average_out, temp); //Add x data
			strcat(average_out, ","); //Add Comma
			
			sprintf(temp, "%lf", y_average);
			strcat(average_out, temp); //Add y data 
			strcat(average_out, ","); //Add Comma
			
			sprintf(temp, "%lf", z_average);
			strcat(average_out, temp); // Add z data
			strcat(average_out, ","); //Add Comma
			
			sprintf(temp, "%d", pressure_avg);
			strcat(average_out, temp); // Add pressure data
			
			//Add newline
			strcat(average_out, "\n");
			
			//Transfer Data to output: Do I even need this?, Just use "average_out" as output?
			sprintf(acc_data,average_out);
			
			/************Start sending data*************/
			for (uint8_t i = 0; i < strlen(acc_data); i++){
				while(!(USART0.STATUS & USART_DREIF_bm));
				USART0.TXDATAL = acc_data[i];
			}
			
			calculate = false;
			
		}
    }
}

ISR(USART1_RXC_vect){
	unsigned char temp = USART1.RXDATAL;
	
	//Is the receiving information a '\r'?
	if (temp == '\r'){
		acc_data[TX0_counter] = '\n'; //Replace with '\n'
		TX0_counter = 0;
		calculate = true;
		return;
	}
	
	//Append to String
	acc_data[TX0_counter++] = temp;
}

ISR(ADC0_RESRDY_vect){
	pressure = ADC0.RES;
}

void send_data(char *data){
	for (uint8_t i = 0; i < strlen(data); i++){
		while(!(USART1.STATUS & USART_DREIF_bm));
		USART1.TXDATAL = data[i];
	}
	_delay_ms(50);
}

unsigned char reverse(unsigned char b) {
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	return b;
}