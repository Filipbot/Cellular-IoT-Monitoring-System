#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct mqtt_config {
    char host[64];
    char device_id[8];
    unsigned int port;
    char username[16];
    char password[16];
} mqtt_config;

#define EG800_RX_PIN   7
#define EG800_TX_PIN   8
#define EG800_DTR_PIN  9

#define EG800_BAUD     115200

#define RESPONSE_BUFFER_SIZE 100
#define DEFAULT_TIMEOUT_MS 10000

extern bool debug;

#define DEBUG debug

#define DEVICE_ID "FAD001"
#define MQTT_USER "user"
#define MQTT_PASSWORD "password"
#define MQTT_HOST "mqtt-broker"
#define MQTT_PORT 8883

extern PIO pio_tx, pio_rx;
extern uint sm_tx, sm_rx;
extern uint offset_tx, offset_rx;

// initialize the pio tx and rx
bool eg800_init();
// send a command to the eg800 module (adds \r\n at the end) EX: eg800_send_command("AT");
void eg800_send_command(const char *command);
// cleans pio rx and tx
void free_eg800();
// reads a char from the rx pin
bool eg800_read(char *response);
// flushes the rx pin buffer
void eg800_flush_rx();
// reads until ok or error is received, returns true if ok, false if error or timeout
bool eg800_read_ok(char *response,unsigned int response_size = RESPONSE_BUFFER_SIZE,
                    unsigned int timeout_ms = DEFAULT_TIMEOUT_MS);
// reads unil the expected response is received (same as read_ok but with custom response)
bool eg800_wait_for_response(const char *expected_response,unsigned int response_size = RESPONSE_BUFFER_SIZE,
                    unsigned int timeout_ms = DEFAULT_TIMEOUT_MS, bool flush_rx = true);
// checks if the eg800 module is ready (AT command)
bool eg800_check_at();
// checks if the eg800 module has a valid pin (AT+CPIN? command)
bool eg800_check_pin();
// checks if the eg800 module has registered to the network (AT+CEREG? command)
bool eg800_check_network();
// checks if the eg800 module has lte data connection (AT+CGATT? command)
bool eg800_check_lte_data();
// an & for the check commands above, returns true if all checks are ok
bool eg800_ready();
// checks if the eg800 module has started (RDY response)
bool eg800_check_RDY();
// prepares the module for mqtt (activates SSL)
bool eg800_prepare_mqtt();
// login to the mqtt broker
bool eg800_mqtt_login(mqtt_config *config);
// stops the echo of at commands (ATE0)
bool eg800_stop_echo();
// sends a mqtt message to the broker
bool send_mqtt_message(const char *topic, const char *message);
// checks if a new config message has been received from the broker
bool eg800_check_new_config(const char *topic,unsigned int *interval_measurment,unsigned int *interval_send, bool *reboot);
// checks the signal strength of the module (AT+CSQ command)
bool eg800_check_signal(int* response);