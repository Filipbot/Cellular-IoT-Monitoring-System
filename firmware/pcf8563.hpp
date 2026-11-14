#include <stdint.h>
#include <time.h>

#include "hardware/i2c.h"

#define PCF8563_ADDRESS 0x51

namespace pcf {
    struct DateTime
    {
        uint16_t year;
        uint8_t month;
        uint8_t day;

        uint8_t hour;
        uint8_t minute;
        uint8_t second;
    };

    class PCF8563
    {
        private:
            i2c_inst_t *i2c;

            bool readRegister(uint8_t reg, uint8_t *data, size_t length);
            bool writeRegister(uint8_t reg, const uint8_t *data, size_t length);
            static uint8_t bcdToDec(uint8_t value);
            static uint8_t decToBcd(uint8_t value);

        public:
            PCF8563(i2c_inst_t *i2c_instance);

            bool setDateTime(const DateTime &dt);

            bool getDateTime(DateTime &dt);

            bool getUnixTime(uint32_t &unix_time);
    };

    inline PCF8563::PCF8563(i2c_inst_t *i2c_instance)
        : i2c(i2c_instance) {}

    inline uint8_t PCF8563::bcdToDec(uint8_t value)
    {
        return ((value >> 4) * 10) + (value & 0x0F);
    }

    inline uint8_t PCF8563::decToBcd(uint8_t value)
    {
        return ((value / 10) << 4) | (value % 10);
    }

    inline bool PCF8563::getDateTime(DateTime &dt)
    {
        uint8_t reg = 0x02;

        if (i2c_write_blocking(i2c, PCF8563_ADDRESS, &reg, 1, true) < 0)
        {
            return false;
        }

        uint8_t data[7];
        if (i2c_read_blocking(i2c, PCF8563_ADDRESS, data, sizeof(data), false) < 0)
        {
            return false;
        }

        dt.second = bcdToDec(data[0] & 0x7F);
        dt.minute = bcdToDec(data[1] & 0x7F);
        dt.hour = bcdToDec(data[2] & 0x3F);
        dt.day = bcdToDec(data[3] & 0x3F);
        dt.month = bcdToDec(data[5] & 0x1F);
        dt.year = 2000 + bcdToDec(data[6]);

        return true;
    }

    inline bool PCF8563::setDateTime(const DateTime &dt)
    {
        uint8_t data[8];
        data[0] = 0x02;
        data[1] = decToBcd(dt.second);
        data[2] = decToBcd(dt.minute);
        data[3] = decToBcd(dt.hour);
        data[4] = decToBcd(dt.day);
        data[5] = 0; // Weekday (not used)
        data[6] = decToBcd(dt.month);
        data[7] = decToBcd(dt.year - 2000);

        if (i2c_write_blocking(i2c, PCF8563_ADDRESS, data, sizeof(data), false) != sizeof(data))
        {
            return false;
        }

        return true;
    }

    inline bool PCF8563::getUnixTime(uint32_t &unix_time)
    {
        struct tm timeinfo;
        DateTime dt;

        if (!getDateTime(dt))
        {
            return false;
        }

        timeinfo.tm_year = dt.year - 1900;
        timeinfo.tm_mon = dt.month - 1;
        timeinfo.tm_mday = dt.day;
        timeinfo.tm_hour = dt.hour;
        timeinfo.tm_min = dt.minute;
        timeinfo.tm_sec = dt.second;
        timeinfo.tm_isdst = -1;

        unix_time = mktime(&timeinfo);
        return true;
    }

}
