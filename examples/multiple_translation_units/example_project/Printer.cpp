//
// Created by morten on 3/7/23.
//

#include "Printer.h"
#include <iostream>

static int global[5] = {};

Printer::Printer() {
    for (int i = 0; i < sizeof(data_)/sizeof(data_[0]); ++i) {
        data_[i] = i;
    }
}

Printer::Printer(const int data[5]) {
    for (int i = 0; i < sizeof(data_)/sizeof(data_[0]); ++i) {
        data_[i] = data[i];
    }
}

void Printer::print() {
    for (int i : data_) {
        std::cout << i << std::endl;;
    }
}
