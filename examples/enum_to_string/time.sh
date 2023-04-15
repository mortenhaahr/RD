#!/bin/bash
time for i in {1..10}; do cp input_file.orig.cpp input_file.cpp && ./build/bin/enum_to_string input_file.cpp --; done
