#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "Hello, World!" << std::endl;
    for (int i = 1; i < argc; ++i) { // Start from 1 to skip the program name
        std::cout << "Argument " << i << ": " << argv[i] << std::endl;
    }
    return 0;
}
