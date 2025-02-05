#ifndef AERGO_LOGGER_H
#define AERGO_LOGGER_H

#include <iostream>

#define LOG(__x) std::cout << __x << std::endl;
#define LOG_ERROR(__x) LOG("[ERROR] " << __x)



#endif // AERGO_LOGGER_H