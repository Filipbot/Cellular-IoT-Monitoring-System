#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"
#include "hardware/uart.h"
#include <mutex>

#include "eg800_mqtt.h"
#include "Ring-Buffer/ringbuffer.hpp"
#include "pcf8563.hpp"
#include "depth_sensor.hpp"

#define RING_BUFFER_ELEMENTS 1024 // needs to be power of 2


//-------------------------------------------------------------
// #include "pico/stdio/driver.h"
// #include <string.h>

// #define DEBUG_BUFFER_SIZE 4096

// static char debug_buffer[DEBUG_BUFFER_SIZE];
// static volatile uint32_t debug_pos = 0;

// static void debug_out_chars(const char *buf, int len)
// {
//     for (int i = 0; i < len; i++)
//     {
//         debug_buffer[debug_pos++] = buf[i];

//         if (debug_pos >= DEBUG_BUFFER_SIZE)
//             debug_pos = 0;   // buffer circular
//     }
// }

// static stdio_driver_t debug_driver = {
//     .out_chars = debug_out_chars,
// };
bool debug = false;
//-------------------------------------------------------------

PIO pio_tx, pio_rx, pio_rs485;
uint sm_tx, sm_rx, sm_tx_485, sm_rx_485;
uint offset_tx, offset_rx, offset_tx_485, offset_rx_485;

struct Measurement {
    float depth;
    float flow;
    uint32_t timestamp;
};

//ring buffer init
jnk0le::Ringbuffer<Measurement, RING_BUFFER_ELEMENTS, true, 4> ring_buffer;
unsigned int ring_buffer_size = 0;

DepthSensor *depth_sensor;

mutex_t ring_buffer_mutex;

unsigned int interval_send = 10000;
unsigned int interval_measurment = 10000;

char send_topic[32];
char config_topic[32];
mqtt_config config;

pcf::PCF8563 rtc(i2c0);
pcf::DateTime dt;

void core1_entry() {
    while (true) {
        char response;

        if (eg800_read(&response)) {
            putchar(response);
        }
    }
}

void core0_input() {
    char command[128];
    size_t i = 0;

    while (true) {
        int c = getchar_timeout_us(0);

        if (c == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (i > 0) {
                command[i] = '\0';
                eg800_send_command(command);
                i = 0;
            }
        } else if (i < sizeof(command) - 1) {
            command[i++] = (char)c;
        }
    }
}

void core1_mqtt() {
    Measurement measurement;

    absolute_time_t send_timeout = make_timeout_time_ms(interval_send);
    while (true)
    {
        if (time_reached(send_timeout))
        {
            send_timeout = make_timeout_time_ms(interval_send);
            while (true)
            {
                if (ring_buffer_size == 0)
                {
                    break;
                }

                if (ring_buffer_size > 0 && ring_buffer.remove(&measurement)) {
                    mutex_enter_blocking(&ring_buffer_mutex);
                    ring_buffer_size--;
                    mutex_exit(&ring_buffer_mutex);

                    // printf("Sending measurement: depth=%.2f, flow=%.2f, timestamp=%u\n", measurement.depth, measurement.flow, measurement.timestamp);

                    char message[32];
                    snprintf(message, sizeof(message), "%.2f,%.2f,%u", measurement.depth,
                        measurement.flow, measurement.timestamp);
                    unsigned int i = 0;
                    while (!send_mqtt_message("devices/FAD001/data", message) && i < 200)
                    {
                        printf("Failed to send message, retrying... %d\n", i);
                        eg800_mqtt_login(&config);
                        i++;
                        sleep_ms(10000);
                        if (i == 10) {
                            printf("Failed to send message after 200 attempts, rebooting...\n");
                            watchdog_reboot(0, 0, 0);
                        }
                    }
                }
            }
            //------------------------
            // char mqtt_msg[DEBUG_BUFFER_SIZE];

            // memcpy(mqtt_msg, debug_buffer, DEBUG_BUFFER_SIZE);
            // mqtt_msg[DEBUG_BUFFER_SIZE - 1] = '\0';
            // send_mqtt_message("devices/FAD001/debug", mqtt_msg);
            //-------------------------
            bool reboot = false;
            eg800_check_new_config(config_topic, &interval_measurment, &interval_send, &reboot);

            if (reboot == true) {
                printf("Rebooting...\n");
                watchdog_reboot(0, 0, 0);
            }

        }
    }
    
}

void core0_read_sensors() {
    Measurement measurement;
    absolute_time_t measurment_timeout = make_timeout_time_ms(interval_measurment);
    while (true)
    {
        if (time_reached(measurment_timeout))
        {
            measurment_timeout = make_timeout_time_ms(interval_measurment);

            mutex_enter_blocking(&ring_buffer_mutex);
            ring_buffer_size++;
            mutex_exit(&ring_buffer_mutex);

            // rtc.getDateTime(dt);
            uint32_t unix_time = 0;
            rtc.getUnixTime(unix_time);
            printf("%d\n", unix_time);

            measurement.timestamp = unix_time;
            measurement.flow = 0.0f;
            (*depth_sensor).readDepth(measurement.depth);
            if (measurement.depth - 0.1f < 0.0f)
            {
                measurement.depth = -1.0f;
            }

            ring_buffer.insert(measurement);
        }
    }
}

int main() {
    //pcf init
    i2c_init(i2c0, 100 * 1000);
    
    gpio_set_function(4, GPIO_FUNC_I2C);
    gpio_set_function(5, GPIO_FUNC_I2C);

    gpio_pull_up(4);
    gpio_pull_up(5);
    

    depth_sensor = new DepthSensor(new PIOUart(DEPTH_TX_PIN, DEPTH_RX_PIN, DEPTH_BAUD_RATE));


    bool eg800_ready_flag = false;

    mutex_init(&ring_buffer_mutex);

    stdio_init_all();

    //----------------------------
    // stdio_set_driver_enabled(&debug_driver, true);
    //----------------------------
    
    //initializam eg800_pio_uart
    eg800_init();

    // bool started = eg800_check_RDY();
    // if(DEBUG) {
    //     if (started) {
    //         printf("EG800 has started\n");
    //     } else {
    //         printf("EG800 has not started\n");
    //     }
    // }
    eg800_stop_echo();

    // while (true) {
    //     uint32_t unix_time = 0;
    //     rtc.getUnixTime(unix_time);
    //     printf("Current unix time: %u\n", unix_time);
    //     rtc.getDateTime(dt);
    //     printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    //     sleep_ms(2000);
    // }

    strcpy(config.host, MQTT_HOST);
    config.port = MQTT_PORT;
    strcpy(config.username, MQTT_USER);
    strcpy(config.password, MQTT_PASSWORD);
    strcpy(config.device_id, DEVICE_ID);

    snprintf(send_topic, sizeof(send_topic), "devices/%s/data", config.device_id);
    snprintf(config_topic, sizeof(config_topic), "devices/%s/config", config.device_id);

    sleep_ms(4000);

    while (!eg800_ready_flag) {
        eg800_ready_flag = eg800_ready();
        if(DEBUG) {
            if (eg800_ready_flag) {
                printf("EG800 is ready\n");
            } else {
                printf("EG800 is not ready\n");
            }
        }
        sleep_ms(5000);
    }

    eg800_prepare_mqtt();

    // int signal_strength = 0;
    // while (true) {

    //     eg800_check_signal(&signal_strength);
    //     printf("Signal strength: %d\n", signal_strength);
    //     sleep_ms(5000);
    // }

    bool login_res = eg800_mqtt_login(&config);

    if (login_res) {
        printf("Login successful\n");
    } else {
        printf("Login failed\n");
    }

    // send_mqtt_message(send_topic, "Hello, World!");

    // sleep_ms(1000);
    // printf("Check new config\n");
    // eg800_check_new_config(config_topic, &interval_measurment, &interval_send);

    printf("Starting PIO UART RX example\n");

    multicore_launch_core1(core1_mqtt);
    core0_read_sensors();

    // multicore_launch_core1(core1_entry);
    // core0_input();

    // This will free resources and unload our program
    free_eg800();
}
