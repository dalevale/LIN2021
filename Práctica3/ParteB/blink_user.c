
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define NR_LEDS	8
#define NR_COLORS 3
#define BUFF_LEN 8

int main(int argc, char* argv[]) {
	int fd;
	char buffer[BUFF_LEN];
	int counter = 0;
	int counter2 = 0;
	char colorBuffer[3];
	char colorChar = '1';
	char seperator = ':';
	colorBuffer[0] = colorChar;
	colorBuffer[1] = '0';
	colorBuffer[2] = '0';
	
	while(1){
		if(fd < 0)
			printf("Ensure blink stick is connected.");
		else {
			
			fd = open("/dev/usb/blinkstick0", O_RDWR);
			buffer[0] = (counter + '0');
			buffer[1] = seperator;
			buffer[2] = colorBuffer[0];
			buffer[3] = colorBuffer[0];
			buffer[4] = colorBuffer[1];
			buffer[5] = colorBuffer[1];
			buffer[6] = colorBuffer[2];
			buffer[7] = colorBuffer[2];
			write(fd, buffer, BUFF_LEN);
			counter++;

			if(counter == NR_LEDS){
				colorBuffer[0] = '0';
				colorBuffer[1] = '0';
				colorBuffer[2] = '0';
				counter2++;
				counter2 = counter2 % NR_COLORS;
				colorBuffer[counter2] = colorChar;
			}

			counter = counter % NR_LEDS;
			usleep(200000);
			close(fd);
		}
	}
    

	return 0;
}