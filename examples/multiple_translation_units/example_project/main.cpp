#include "Printer.h"
#include <array>
#include <iostream>

int main(){
    int arr[5] = {5, 4, 3, 2, 1};
    auto print1 = Printer{};
    auto print2 = Printer{arr};
    std::cout << "Printer 1: " << std::endl;
    print1.print();

    std::cout << "Printer 2: " << std::endl;
    print2.print();
}