#include <fstream>
#include <stdexcept>
#include <iostream>

struct Test {
    int val;
    Test(int v) : val(v) {}
    ~Test() { std::cout << "dtor " << val << "\n"; }
};

int may_throw(int i) {
    if (i < 0)
        throw std::runtime_error("negative");
    if (i == 0)
        throw std::logic_error("zero");
    Test t(i);
    if (i > 100)
        throw std::overflow_error("too large");
    return i * 2;
}

void nesting() {
    Test t1(1);
    try {
        Test t2(2);
        try {
            Test t3(3);
            may_throw(42);
        } catch (const std::logic_error& e) {
            std::cout << "inner: " << e.what() << "\n";
        }
    } catch (const std::runtime_error& e) {
        std::cout << "outer: " << e.what() << "\n";
    }
}

int main() {
    try {
        nesting();
        return may_throw(0);
    } catch (const std::exception& e) {
        std::cout << "main: " << e.what() << "\n";
        return 1;
    }
}
