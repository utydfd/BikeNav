#ifndef TIMEZONE_H
#define TIMEZONE_H

#include <TinyGPS++.h>

// LocalTime structure for timezone-adjusted GPS time
struct LocalTime {
  int hour;
  int minute;
  int second;
  int day;
  int month;
  int year;
};

// External timezone helper function
extern LocalTime getLocalTime();

#endif // TIMEZONE_H
