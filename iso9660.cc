#include <cstddef>
#include "iso9660.hh"



namespace iso9660
{

int ascii_to_decimal(const uint8_t *data, std::size_t size)
{
    int decimal = 0;

    for(std::size_t i = 0; i < size; ++i)
    {
        uint8_t digit = (char)data[i] - '0';
        if(digit > 9)
            digit = 0;
        decimal = decimal * 10 + digit;
    }

    return decimal;
}


time_t convert_time(const DateTime &date_time)
{
    tm time_info;

    int year = ascii_to_decimal(date_time.year, sizeof(date_time.year));
    // PSX specifics
    if(year < 1970)
    {
        if(year >= 1900)
            year -= 1900;
        year += 2000;
    }
    time_info.tm_year = year - 1900;
    time_info.tm_mon = ascii_to_decimal(date_time.month, sizeof(date_time.month)) - 1;
    time_info.tm_mday = ascii_to_decimal(date_time.day, sizeof(date_time.day));
    time_info.tm_hour = ascii_to_decimal(date_time.hour, sizeof(date_time.hour));
    time_info.tm_min = ascii_to_decimal(date_time.minute, sizeof(date_time.minute));
    time_info.tm_sec = ascii_to_decimal(date_time.second, sizeof(date_time.second));
    time_info.tm_isdst = -1;

    //FIXME: GMT offset

    return mktime(&time_info);
}


time_t convert_time(const RecordingDateTime &date_time)
{
    tm time_info;

    // PSX specifics
    uint32_t year = date_time.year;
    if(year < 70)
        year += 100;

    time_info.tm_year = year;
    time_info.tm_mon = date_time.month - 1;
    time_info.tm_mday = date_time.day;
    time_info.tm_hour = date_time.hour;
    time_info.tm_min = date_time.minute;
    time_info.tm_sec = date_time.second;
    time_info.tm_isdst = -1;

    //FIXME: GMT offset

    return mktime(&time_info);
}

}
