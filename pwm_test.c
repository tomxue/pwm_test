#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// 13 MHz clock
#define PWM_FREQUENCY_13MHZ 13000000
// 32 kHz clock
#define PWM_FREQUENCY_32KHZ 32000

// Timer register
#define GPT_REG_TCLR 0x024  // in byte space
#define GPT_REG_TCRR 0x028
#define GPT_REG_TLDR 0x02c
#define GPT_REG_TMAR 0x038

// clock register of GPTIMER10
#define CM_CLOCK_BASE 0x48004000
#define CM_FCLKEN1_CORE 0xa00
#define CM_ICLKEN1_CORE 0xa10
#define CM_CLKSEL_CORE 0xa40

#define INT *(volatile unsigned int*)

unsigned int pwm_calc_resolution(int pwm_frequency, int clock_frequency)
{
    float pwm_period = 1.0 / pwm_frequency;
    float clock_period = 1.0 / clock_frequency;
    return (unsigned int) (pwm_period / clock_period);
}

void pwm_config_timer(unsigned int *gpt, unsigned int resolution, float duty_cycle){
	duty_cycle = duty_cycle/100;
	unsigned long counter_start = 0xffffffff - resolution;
	unsigned long dc = 0xffffffff - ((unsigned long) (resolution * duty_cycle));

    // Edge condition: the duty cycle is set within two units of the overflow
    // value.  Loading the register with this value shouldn't be done (TRM 16.2.4.6).
    if (0xffffffff - dc <= 2) {
        dc = 0xffffffff - 2;
    }

    // Edge condition: TMAR will be set to within two units of the overflow
    // value.  This means that the resolution is extremely low, which doesn't
    // really make sense, but whatever.
    if (0xffffffff - counter_start <= 2) {
        counter_start = 0xffffffff - 2;
    }

    // GPT_REG_TCLR/4: in int space
    gpt[GPT_REG_TCLR/4] = 0; // Turn off
    gpt[GPT_REG_TCRR/4] = counter_start;
    gpt[GPT_REG_TLDR/4] = counter_start;
    gpt[GPT_REG_TMAR/4] = dc;
    gpt[GPT_REG_TCLR/4] = (
        (1 << 0)  | // ST -- enable counter
        (1 << 1)  | // AR -- autoreload on overflow
        (1 << 6)  | // CE -- compare enabled
        (1 << 7)  | // SCPWM -- invert pulse
        (2 << 10) | // TRG -- overflow and match trigger
        (1 << 12)   // PT -- toggle PWM mode
    );
}

int main(int argc, char *argv[]){
	int dev_fd;
	unsigned int *PinConfig;
	unsigned int CurValue;
	unsigned int *gpt10;
	unsigned long resolution;
	int i;

    printf("Run like: sudo ./pwm_test 50\n\n");
	// Configure PIN
	dev_fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (dev_fd == -1) {
		printf("Can not opem /dev/mem.");
		return -1;
	}
	else
		printf("dev_fd = %d...............\n", dev_fd);

	// set the clock source to 13MHz
    // comparing to GPIO/gpio.c, (unsigned int *) is added here
    // so it then goes into int space and the offset (@char) must be devided by 4 (@int)
	PinConfig = (unsigned int *) mmap(NULL, 0x300, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fd, CM_CLOCK_BASE);
	CurValue = INT(PinConfig+CM_CLKSEL_CORE/4);
	CurValue &= 0xffffffbf;
	CurValue |= 0x40;	// set CLKSEL_GPT10 1,
	INT(PinConfig+CM_CLKSEL_CORE/4) = CurValue;
	printf("13MHz clock source enabled........\n");

	// enable the clock: FCLK and ICLK
	PinConfig = (unsigned int *) mmap(NULL, 0x300, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fd, CM_CLOCK_BASE);
	CurValue = INT(PinConfig+CM_FCLKEN1_CORE/4);
	CurValue &= 0xfffff7ff;
	CurValue |= 0x800;	// set EN_GPT10 1, GPTIMER 10 functional clock is enabled
	INT(PinConfig+CM_FCLKEN1_CORE/4) = CurValue;
	printf("FCLOCK enabled........\n");

	CurValue = INT(PinConfig+CM_ICLKEN1_CORE/4);
	CurValue &= 0xfffff7ff;
	CurValue |= 0x800;	// set EN_GPT10 1, GPTIMER 10 interface clock is enabled
	INT(PinConfig+CM_ICLKEN1_CORE/4) = CurValue;
	printf("ICLOCK enabled.........\n");
	munmap(PinConfig, 0x1000);

	// System control module: 0x4800 2000, found via devmem2
	PinConfig=(unsigned int *) mmap(NULL, 0x200, PROT_READ | PROT_WRITE, MAP_SHARED,dev_fd, 0x48002000);

	// Set PWM function on pin: GPIO_56, EMA-product-board GPMC_nCS5, DM3730 spec page 2428
	// division by 4 is necessary because the size of one element of "unsigned int" is 4 bytes, which corresponds to the size of control registers
	CurValue=INT(PinConfig+0x0B8/4); 
	CurValue &= 0xffff0000;
	/* Timer 10: mode 2 - gpt_10_pwm_evt, PullUp selected, PullUp/Down enabled, Input enable value for pad_x */
	CurValue |= 0x0000011B; 
	INT(PinConfig+0x0B8/4) = CurValue; //PIN CONFIGURED AS BIDIRECTIONAL

	munmap(PinConfig, 0x200);

	// Configure timer 10 and 11 to 13 MHz
	// Config=(unsigned int *) mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED,dev_fd, 0x48004000);
	// CurValue=Config[0xA40/4];
	// CurValue=CurValue | (1<<6) | (1<<7);
	// CurValue |= (1<<6);//Configure 10 timer to 13 MHz
	// CurValue |= (1<<7);//Configure 11 timer to 13 MHz
	// Config[0xA40/4]=CurValue;

	// GPTIMER10 base address: 0x48086000(in byte space)
    // gpt10: in int space
	gpt10=(unsigned int *) mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED,dev_fd, 0x48086000);

	resolution = pwm_calc_resolution(10000, PWM_FREQUENCY_13MHZ);

	pwm_config_timer(gpt10, resolution,atoi(argv[1]));

	munmap(gpt10, 0x10000);
	close(dev_fd);
	return 0;
}
