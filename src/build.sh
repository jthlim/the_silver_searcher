#!/bin/bash
clang++ --std=c++14 *.cpp ../../Javelin/Stream/StringWriter.cpp ../../Javelin/Stream/CountWriter.cpp -I ../.. -I . -O3 -DJBUILDCONFIG_FINAL -o jag -lJavelinPattern -L../../Javelin/build -pthread -lz -ldl
strip jag
