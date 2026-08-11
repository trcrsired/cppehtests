#include <iostream>
#include <exception>

int main() {
    try {
        throw std::runtime_error("test seh");
    } catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
    }
    return 0;
}
