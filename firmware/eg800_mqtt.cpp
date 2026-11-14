#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"
#include "hardware/uart.h"

#include <string.h>

#include "eg800_mqtt.h"

#include "eg800.pio.h"

bool eg800_init() {
    
    //tx
    bool success_tx = pio_claim_free_sm_and_add_program_for_gpio_range(&uart_tx_program, &pio_tx, &sm_tx, &offset_tx,
    EG800_TX_PIN, 1, true);
    hard_assert(success_tx);
    uart_tx_program_init(pio_tx, sm_tx, offset_tx, EG800_TX_PIN, EG800_BAUD);

    //rx
    bool success_rx = pio_claim_free_sm_and_add_program_for_gpio_range(&uart_rx_program, &pio_rx, &sm_rx, &offset_rx,
    EG800_RX_PIN, 1, true);
    hard_assert(success_rx);
    uart_rx_program_init(pio_rx, sm_rx, offset_rx, EG800_RX_PIN, EG800_BAUD);

    return true;
}

void eg800_send_command(const char *command) {
    uart_tx_program_puts(pio_tx, sm_tx, command);
    uart_tx_program_puts(pio_tx, sm_tx, "\r\n");
}

void free_eg800() {
    pio_remove_program_and_unclaim_sm(&uart_tx_program, pio_tx, sm_tx, offset_tx);
    pio_remove_program_and_unclaim_sm(&uart_rx_program, pio_rx, sm_rx, offset_rx);
}

bool eg800_read(char *response) {
    if (pio_sm_is_rx_fifo_empty(pio_rx, sm_rx)) {
        return false;
    }

    io_rw_8 *rxfifo_shift = (io_rw_8*)&pio_rx->rxf[sm_rx] + 3;
    *response = (char)*rxfifo_shift;

    return true;
}

void eg800_flush_rx() {
    for (unsigned int i = 0; i < 10; i++) {
        char c;
        while (eg800_read(&c)) {
        }
        sleep_ms(10);
    }
}


bool eg800_read_ok(char *response, unsigned int response_size, unsigned int timeout_ms) {
    unsigned int i = 0;
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);

    while (!time_reached(timeout) && i < response_size - 1) {
        char c;

        if (eg800_read(&c)) {
            response[i++] = c;
            response[i] = '\0';
            //putchar(c);

            if (strstr(response, "OK") != NULL) {
                eg800_flush_rx();
                return true;
            }

            if (strstr(response, "ERROR") != NULL) {
                eg800_flush_rx();
                return false;
            }
        }
    }

    return false;
}

bool eg800_wait_for_response(const char *expected_response, unsigned int response_size,
                    unsigned int timeout_ms, bool flush_rx) {
    unsigned int i = 0;
    absolute_time_t timeout = make_timeout_time_ms(timeout_ms);
    char response[RESPONSE_BUFFER_SIZE] = {0};

    while (!time_reached(timeout) && i < response_size - 1) {
        char c;

        if (eg800_read(&c)) {
            response[i++] = c;
            response[i] = '\0';
            //putchar(c);

            if (strstr(response, expected_response) != NULL) {
                if (flush_rx) {
                    eg800_flush_rx();
                }
                printf("Response: %s\n", response);
                return true;
            }

            if (strstr(response, "ERROR") != NULL) {
                if (flush_rx) {
                    eg800_flush_rx();
                }
                eg800_flush_rx();
                printf("ERR_Response: %s\n", response);
                return false;
            }
        }
    }
    printf("FAL_Response%s: %s\n", expected_response, response);
    return false;
}

bool eg800_check_at() {

    eg800_send_command("AT");
    char response[RESPONSE_BUFFER_SIZE] = {0};
    eg800_read_ok(response);
    
    if(DEBUG) {
        printf("Response: %s\n", response);
    }
    return strstr(response, "OK") != NULL;
}

bool eg800_check_pin() {
    eg800_send_command("AT+CPIN?");
    char response[RESPONSE_BUFFER_SIZE] = {0};
    eg800_read_ok(response);

    if(DEBUG) {
        printf("Response: %s\n", response);
    }
    return (strstr(response, "READY") != NULL);
}

bool eg800_check_network() {
    eg800_send_command("AT+CEREG?");
    char response[RESPONSE_BUFFER_SIZE] = {0};
    eg800_read_ok(response);

    if(DEBUG) {
        printf("Response: %s\n", response);
    }
    return (strstr(response, "0,1") != NULL);
}

bool eg800_check_lte_data() {
    eg800_send_command("AT+CGATT?");
    char response[RESPONSE_BUFFER_SIZE] = {0};
    eg800_read_ok(response);

    if(DEBUG) {
        printf("Response: %s\n", response);
    }
    return (strstr(response, "1") != NULL);
}

bool eg800_ready() {
    return 
        eg800_check_at() && 
        eg800_check_pin() && 
        eg800_check_network() && 
        eg800_check_lte_data();
}

bool eg800_check_RDY() {
    unsigned int i = 0;
    absolute_time_t timeout = make_timeout_time_ms(20000);

    char response[RESPONSE_BUFFER_SIZE] = {0};
    while (!time_reached(timeout) && i < RESPONSE_BUFFER_SIZE - 1) {
        char c;

        if (eg800_read(&c)) {
            response[i++] = c;
            response[i] = '\0';

            if (strstr(response, "RDY") != NULL) {
                return true;
            }
        } else {
            tight_loop_contents();
        }
    }
    return false;
}

bool eg800_prepare_mqtt() {
    eg800_send_command("AT+QSSLCFG=\"sslversion\",1,4");
    char response[RESPONSE_BUFFER_SIZE] = {0};
    if (eg800_read_ok(response) == false) {
        return false;
    }
    if (DEBUG) {
        printf("Response: %s\n", response);
    }
    
    eg800_send_command("AT+QSSLCFG=\"seclevel\",1,0");
    strcpy(response, "");
    if (eg800_read_ok(response) == false) {
        return false;
    }
    if (DEBUG) {
        printf("Response: %s\n", response);
    }
    
    eg800_send_command("AT+QSSLCFG=\"sni\",1,1");
    strcpy(response, "");
    if (eg800_read_ok(response) == false) {
        return false;
    }
    if (DEBUG) {
        printf("Response: %s\n", response);
    }
    
    eg800_send_command("AT+QMTCFG=\"SSL\",0,1,1");
    strcpy(response, "");
    if (eg800_read_ok(response) == false) {
        return false;
    }
    if (DEBUG) {
        printf("Response: %s\n", response);
    }
    return true;
}

bool eg800_mqtt_login(mqtt_config *config)
{
    char cmd[256];

    snprintf(cmd, sizeof(cmd),
             "AT+QMTOPEN=0,\"%s\",%d",
             config->host,
             config->port);
    eg800_send_command(cmd);

    if (eg800_wait_for_response("+QMTOPEN: 0,0", 100, 10000) == false) {
        return false;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"",
             config->device_id,
             config->username,
             config->password);
    eg800_send_command(cmd);


    if (eg800_wait_for_response("+QMTCONN: 0,0,0", 100, 10000) == false) {
        return false;
    }

    return true;
}

bool eg800_stop_echo() {
    eg800_send_command("ATE0");
    char response[100];
    if (eg800_read_ok(response) == false) {
        return false;
    }
    if (DEBUG) {
        printf("Response: %s\n", response);
    }
    return true;
}

bool send_mqtt_message(const char *topic, const char *message) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QMTPUB=0,0,0,0,\"%s\",\"%s\"", topic, message);
    eg800_send_command(cmd);

    sleep_ms(50);
    
    char compiled_message[256];
    snprintf(compiled_message, sizeof(compiled_message),
        "%s\x1A", message);
    eg800_send_command(compiled_message);

    if (DEBUG) {
        printf("Sent message: %s\n", compiled_message);
    }

    if (eg800_wait_for_response("+QMTPUB: 0,0,0", 100, 5000) == false) {
        return false;
    }

    return true;
}

bool eg800_check_new_config(const char *topic,unsigned int *interval_measurment, unsigned int *interval_send, bool *reboot) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+QMTSUB=0,1,\"%s\",0", topic);
    eg800_send_command(cmd);

    if (eg800_wait_for_response("+QMTSUB: 0,1,0,0", 100, 5000, false) == false) {
        return false;
    }

    absolute_time_t timeout_big = make_timeout_time_ms(5000);

    while (!time_reached(timeout_big)) {
        unsigned int i = 0;
        absolute_time_t timeout = make_timeout_time_ms(1000);
        char response[RESPONSE_BUFFER_SIZE] = {0};

        while (!time_reached(timeout) && i < RESPONSE_BUFFER_SIZE - 1) {
            char c;

            if (eg800_read(&c)) {
                response[i++] = c;
                response[i] = '\0';
                //putchar(c);

                if (strstr(response, "+QMTRECV: 0,0") != NULL) {
                    while (response[i - 1] != '\n' && i < RESPONSE_BUFFER_SIZE - 1) {
                        if (eg800_read(&c)) {
                            response[i++] = c;
                            response[i] = '\0';
                        }
                    }
                    if (DEBUG) {
                        printf("Response: %s %d\n", response, i);
                    }
                    char *config_start = strstr(response, "xd");
                    if (config_start != NULL) {
                        config_start += 2;
                        char *p = strtok(config_start, ",");
                        if (p != NULL) {
                            *interval_measurment = atoi(p) * 1000;
                        }
                        p = strtok(NULL, ",");
                        if (p != NULL) {
                            *interval_send = atoi(p) * 1000;
                        }
                        if (DEBUG) {
                            printf("New config: interval_measurment=%d, interval_send=%d\n",
                            *interval_measurment, *interval_send);
                        }
                        break;
                    }
                    config_start = strstr(response, "rghi");
                    if (config_start != NULL) {
                        *reboot = true;
                        break;
                    }
                    config_start = strstr(response, "dbg");
                    if (config_start != NULL && debug == false) {
                        debug = true;
                        break;
                    } else if (config_start != NULL && debug == true) {
                        debug = false;
                        break;
                    }
                    break;
                    
                }

                if (strstr(response, "ERROR") != NULL) {
                    eg800_flush_rx();
                    if (DEBUG) {
                        printf("ERR_Response: %s\n", response);
                    }
                    return false;
                }
            }
        }
    }

    snprintf(cmd, sizeof(cmd), "AT+QMTUNS=0,1,\"%s\"", topic);
    eg800_send_command(cmd);

    if (eg800_wait_for_response("+QMTUNS: 0,1,0", 100, 5000) == false) {
        return false;
    }

    return true;
}

bool eg800_check_signal(int* response) {
    eg800_send_command("AT+QCSQ");
    char response_str[RESPONSE_BUFFER_SIZE] = {0};
    if (eg800_read_ok(response_str) == false) {
        return false;
    }

    if (!DEBUG) {
        printf("Response: %s\n", response_str);
    }

    char *csq_start = strstr(response_str, "+CSQ: ");
    if (csq_start != NULL) {
        csq_start += 16;
        *response = atoi(csq_start);
        return true;
    }

    return false;
}
