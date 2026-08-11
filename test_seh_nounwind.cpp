// Test with no stack unwinding (catch in same frame as throw)
#include <iostream>
#include <cstdlib>
struct Cleanup {
    const char* msg;
    Cleanup(const char* m) : msg(m) {}
    ~Cleanup() { std::cout << "cleanup: " << msg << "\n"; }
};
void inner() {
    Cleanup c("inner");
    throw std::runtime_error("from inner");
}
void outer() {
    Cleanup c("outer");
    try {
        inner();
    } catch (const std::exception& e) {
        std::cout << "outer caught: " << e.what() << "\n";
        throw;  // rethrow
    }
}
int main() {
    try {
        outer();
    } catch (const std::exception& e) {
        std::cout << "main caught: " << e.what() << "\n";
    }
    std::cout << "done\n";
    return 0;
}
