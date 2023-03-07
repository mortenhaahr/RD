//
// Created by morten on 3/7/23.
//

#ifndef MULTIPLETRANSLATIONUNITS_PRINTER_H
#define MULTIPLETRANSLATIONUNITS_PRINTER_H


class Printer {
public:
    Printer();
    Printer(const int data[5]);

    void print();
private:
    int data_[5];
};


#endif //MULTIPLETRANSLATIONUNITS_PRINTER_H
