#include "PIOuart.hpp"

#define DEPTH_TX_PIN 2
#define DEPTH_RX_PIN 3
#define DEPTH_BAUD_RATE 9600

class DepthSensor
{
    private:
        PIOUart *uart;

        bool sendRequest();

        bool receiveResponse(uint8_t *buffer, size_t length);

        float decodeFloat(uint8_t *bytes);

        static uint16_t calculateCRC16(const uint8_t *data, size_t length);

        bool verifyCRC16(const uint8_t *data, size_t length);

    public:
        DepthSensor(PIOUart *uart_instance);
        bool readDepth(float &depth);
};

    inline DepthSensor::DepthSensor(PIOUart *uart_inst)
        : uart(uart_inst) {}

    inline bool DepthSensor::readDepth(float &depth)
    {
        uint8_t response[9];

        if (!sendRequest())
        {
            return false;
        }

        if (!receiveResponse(response, sizeof(response)))
        {
            return false;
        }

        if (!verifyCRC16(response, sizeof(response)))
        {
            return false;
        }

        if (response[0] != 0x01 || response[1] != 0x03 || response[2] != 0x04)
        {
            return false;
        }

        // printf("Depth response: %x %x %x %x %x %x %x %x %x\n", response[0], response[1], response[2], response[3], response[4], response[5], response[6], response[7], response[8]);

        depth = decodeFloat(&response[3]);

        return true;
    }

    inline float DepthSensor::decodeFloat(uint8_t *bytes)
    {
        uint32_t raw =
            (bytes[0] << 24) |
            (bytes[1] << 16) |
            (bytes[2] << 8) |
            (bytes[3]);

        float value;
        memcpy(&value, &raw, sizeof(value));
        return value;
    }

    inline bool DepthSensor::sendRequest()
    {
        uint8_t request[8] = {0x01, 0x03, 0x00, 0x16, 0x00, 0x02};
        uint16_t crc16 = calculateCRC16(request, 6);

        request[6] = crc16 & 0xFF;
        request[7] = (crc16 >> 8) & 0xFF;

        uart->write(request, sizeof(request));

        // printf("REQ: %x %x %x %x %x %x %x %x\n", request[0], request[1], request[2], request[3], request[4], request[5], request[6], request[7]);

        return true;
    }

    inline uint16_t DepthSensor::calculateCRC16(const uint8_t *data, size_t length)
    {
        uint16_t crc = 0xFFFF;

        for (unsigned int i = 0; i < length; i++)
        {
            crc ^= data[i];

            for (unsigned int j = 0; j < 8; j++)
            {
                if (crc & 0x0001)
                {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }

    inline bool DepthSensor::receiveResponse(uint8_t *buffer, size_t length)
    {
        absolute_time_t timeout = make_timeout_time_ms(1000);

        size_t received = 0;

        while (received < length && !time_reached(timeout))
        {
            if (uart->readable())
            {
                buffer[received++] = uart->getc();
            }

            if (time_reached(timeout))
            {
                return false;
            }
        }
        return true;
    }

    inline bool DepthSensor::verifyCRC16(const uint8_t *data, size_t length)
    {
        if (length < 3)
        {
            return false;
        }

        uint16_t received_crc = data[length - 2] | (data[length - 1] << 8);
        uint16_t calculated_crc = calculateCRC16(data, length - 2);

        return received_crc == calculated_crc;
    }

