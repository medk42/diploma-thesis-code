#include "logging.h"

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        LOG_ERROR("Requires three argument")
        return 0;
    }

    AERGO_LOG("Hello, world!")
}