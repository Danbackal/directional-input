#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/adc.h"

/* Example code to talk to a Max7219 driving an 8 digit 7 segment display via SPI

   NOTE: The device is driven at 5v, but SPI communications are at 3v3

   Connections on Raspberry Pi Pico board and a generic Max7219 board, other
   boards may vary.

   * GPIO 17 (pin 22) Chip select -> CS on Max7219 board
   * GPIO 18 (pin 24) SCK/spi0_sclk -> CLK on Max7219 board
   * GPIO 19 (pin 25) MOSI/spi0_tx -> DIN on Max7219 board
   * 5v (pin 40) -> VCC on Max7219 board
   * GND (pin 38)  -> GND on Max7219 board

   Note: SPI devices can have a number of different naming schemes for pins. See
   the Wikipedia page at https://en.wikipedia.org/wiki/Serial_Peripheral_Interface
   for variations.

*/

#define NUM_MODULES 1

// Arrows 

uint8_t arrows[4][8] = {
    {0x08, 0x1c, 0x3e, 0x7f, 0x1c, 0x1c, 0x1c, 0x1c}, // Up
    {0x1c, 0x1c, 0x1c, 0x1c, 0x7f, 0x3e, 0x1c, 0x08}, // Down
    {0x10, 0x30, 0x7f, 0xff, 0x7f, 0x30, 0x10, 0x00}, // Left
    {0x08, 0x0c, 0xfe, 0xff, 0xfe, 0x0c, 0x08, 0x00} // Right
};

static constexpr int UP = 0; // constexpr still typed but compile time
static constexpr int DOWN = 1;
static constexpr int LEFT = 2;
static constexpr int RIGHT = 3;

// SPI fields

const uint8_t CMD_NOOP = 0;
const uint8_t CMD_DIGIT0 = 1; // Goes up to 8, for each line
const uint8_t CMD_DECODEMODE = 9;
const uint8_t CMD_BRIGHTNESS = 10;
const uint8_t CMD_SCANLIMIT = 11;
const uint8_t CMD_SHUTDOWN = 12;
const uint8_t CMD_DISPLAYTEST = 15;

// Joystick Fields

constexpr uint buttonPin = 28;
constexpr uint xDeltaPin = 27;
constexpr uint yDeltaPin = 26;
absolute_time_t debouncer;
int64_t wait_time = 100;

#ifdef PICO_DEFAULT_SPI_CSN_PIN
static inline void cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);
    asm volatile("nop \n nop \n nop");
}
#endif

#if defined(spi_default) && defined(PICO_DEFAULT_SPI_CSN_PIN)
static void write_register(uint8_t reg, uint8_t data) {
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = data;
    cs_select();
    spi_write_blocking(spi_default, buf, 2);
    cs_deselect();
    sleep_ms(1);
}

static void write_register_all(uint8_t reg, uint8_t data) {
    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = data;
    cs_select();
    for (int i = 0; i< NUM_MODULES;i++) {
        spi_write_blocking(spi_default, buf, 2);
    }
    cs_deselect();
}
#endif


void display_arrow(int arrow)
{
    for (int i = 0; i<8; i++) {
        write_register_all(CMD_DIGIT0 + i, arrows[arrow][i]);
    }     
}

void clear()
{
    for (int i=0;i<8;i++) {
        write_register_all(CMD_DIGIT0 + i, 0);
    }
}
int cur_arrow = 0; // just temp until I learn varx/vary
void gpio_callback(uint gpio, uint32_t events)
{
    clear();
}

int main() {
    stdio_init_all();

#if !defined(spi_default) || !defined(PICO_DEFAULT_SPI_SCK_PIN) || !defined(PICO_DEFAULT_SPI_TX_PIN) || !defined(PICO_DEFAULT_SPI_CSN_PIN)
#warning spi/max7219_8x7seg_spi example requires a board with SPI pins
    puts("Default SPI pins were not defined");
#else

    printf("Hello, max7219! Drawing things on a 8 x 7 segment display since 2022...\n");

    // This example will use SPI0 at 10MHz.
    spi_init(spi_default, 10 * 1000 * 1000);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);

    // Make the SPI pins available to picotool
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_SPI_TX_PIN, PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI));

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(PICO_DEFAULT_SPI_CSN_PIN);
    gpio_set_dir(PICO_DEFAULT_SPI_CSN_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_SPI_CSN_PIN, 1);

    // Make the CS pin available to picotool
    bi_decl(bi_1pin_with_name(PICO_DEFAULT_SPI_CSN_PIN, "SPI CS"));

    // Send init sequence to device

    write_register_all(CMD_SHUTDOWN, 0);
    write_register_all(CMD_DISPLAYTEST, 0);
    write_register_all(CMD_SCANLIMIT, 7);
    write_register_all(CMD_DECODEMODE, 0);
    write_register_all(CMD_SHUTDOWN, 1);
    write_register_all(CMD_BRIGHTNESS, 2);

    clear();

    display_arrow(UP);

    // DEBOUNCER
    debouncer = get_absolute_time();
    // SWITCH INIT
    gpio_init(buttonPin);
    gpio_pull_up(buttonPin);
    gpio_set_dir(buttonPin, GPIO_IN);
    gpio_set_irq_enabled_with_callback(buttonPin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    // JOYSTICK INIT
    adc_init();
    adc_gpio_init(yDeltaPin); // adc_select_input 0
    adc_gpio_init(xDeltaPin); // adc_select_input 1 (28 & 29 are 2 & 3)
    int adc_x_drift;
    int adc_y_drift;
    uint mid = 2000;
    int active_drift = 200;
    while (true) {
        adc_select_input(0);
        adc_x_drift = (int) (adc_read() - mid);
        adc_select_input(1);
        adc_y_drift = (int) (adc_read() - mid);
        if ((abs(adc_x_drift) > abs(adc_y_drift)) && (abs(adc_x_drift) > active_drift)) {
            display_arrow((adc_x_drift > 0) ? RIGHT : LEFT);
        }
        if ((abs(adc_y_drift) > abs(adc_x_drift)) && (abs(adc_y_drift) > active_drift)) {
            display_arrow((adc_y_drift > 0) ? UP : DOWN);
        }
        // General Loop
        sleep_ms(100);
    }
#endif
}
