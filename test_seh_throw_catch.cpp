// Minimal test: throw and catch in same function, no unwinding
#include <iostream>
#include <exception>
int main() {
    std::cout << "before throw\n";
    try {
        std::cout << "in try\n";
        throw 42;
    } catch (int i) {
        std::cout << "caught int: " << i << "\n";
    }
    std::cout << "after catch\n";
    return 0;
}
