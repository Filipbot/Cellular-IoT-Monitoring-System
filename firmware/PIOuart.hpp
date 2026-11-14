#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "eg800.pio.h"

class PIOUart
{
private:
    PIO pio;
    uint tx_sm, rx_sm, tx_offset, rx_offset;

public:
PIOUart(
    uint tx_pin,
    uint rx_pin,
    uint baud)
{
    bool success_tx =
        pio_claim_free_sm_and_add_program_for_gpio_range(
            &uart_tx_program,
            &pio,
            &tx_sm,
            &tx_offset,
            tx_pin,
            1,
            true);

    hard_assert(success_tx);

    uart_tx_program_init(
        pio,
        tx_sm,
        tx_offset,
        tx_pin,
        baud);

    bool success_rx =
        pio_claim_free_sm_and_add_program_for_gpio_range(
            &uart_rx_program,
            &pio,
            &rx_sm,
            &rx_offset,
            rx_pin,
            1,
            true);

    hard_assert(success_rx);

    uart_rx_program_init(
        pio,
        rx_sm,
        rx_offset,
        rx_pin,
        baud);
}
    void putc(char c)
    {
        uart_tx_program_putc(
            pio,
            tx_sm,
            c);
    }

    void puts(const char *s)
    {
        uart_tx_program_puts(
            pio,
            tx_sm,
            s);
    }

    void write(const uint8_t *data, size_t len)
    {
        for (size_t i = 0; i < len; i++)
        {
            putc((char)data[i]);
        }
    }

    bool readable()
    {
        return !pio_sm_is_rx_fifo_empty(
            pio,
            rx_sm);
    }

    char getc()
    {
        return uart_rx_program_getc(
            pio,
            rx_sm);
    }

    uint8_t read()
    {
        return (uint8_t)getc();
    }

    void read(uint8_t *buffer, size_t len)
    {
        for (size_t i = 0; i < len; i++)
        {
            buffer[i] = read();
        }
    }
};