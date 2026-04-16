#ifdef SIMULATE

#define UNUSED(v) (void)(v)

#include "serial_stub.h"

#include <string.h>

#include <iostream>

SerialClass Serial;

void SerialClass::begin(unsigned long baud) {
    UNUSED(baud);
    started = true;
}

void SerialClass::end() { started = false; }

// printing functions
void SerialClass::print(const std::string& s) { std::cout << s; }
void SerialClass::print(const char* s) {
    if (s) {
        std::cout << s;
    }
}
void SerialClass::print(char c) { std::cout << c; }
void SerialClass::print(int value) { std::cout << value; }
void SerialClass::print(unsigned int value) { std::cout << value; }
void SerialClass::print(long value) { std::cout << value; }
void SerialClass::print(unsigned long value) { std::cout << value; }
void SerialClass::print(float value) { std::cout << value; }
void SerialClass::print(double value) { std::cout << value; }
void SerialClass::println() { std::cout << '\n'; }
void SerialClass::println(const std::string& s) { std::cout << s << '\n'; }
void SerialClass::println(const char* s) {
    if (s) {
        std::cout << s << '\n';
    }
}
void SerialClass::println(char c) { std::cout << c << '\n'; }
void SerialClass::println(int value) { std::cout << value << '\n'; }
void SerialClass::println(unsigned int value) { std::cout << value << '\n'; }
void SerialClass::println(long value) { std::cout << value << '\n'; }
void SerialClass::println(unsigned long value) { std::cout << value << '\n'; }
void SerialClass::println(float value) { std::cout << value << '\n'; }
void SerialClass::println(double value) { std::cout << value << '\n'; }

size_t SerialClass::write(uint8_t byte) {
    std::cout << static_cast<char>(byte);
    return 1;
}

size_t SerialClass::write(const char* buffer) {
    if (!buffer) {
        return 0;
    }
    size_t len = strlen(buffer);
    std::cout << buffer;
    return len;
}

size_t SerialClass::write(const uint8_t* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        std::cout << static_cast<char>(buffer[i]);
    }
    return size;
}

void SerialClass::pollInput() {
    while (std::cin.rdbuf()->in_avail() > 0) {
        char c;
        std::cin.get(c);
        inputBuffer.push(c);
    }
}

int SerialClass::available() {
    pollInput();
    return static_cast<int>(inputBuffer.size());
}

int SerialClass::read() {
    pollInput();
    if (inputBuffer.empty()) {
        return -1;
    }
    char c = inputBuffer.front();
    inputBuffer.pop();
    return static_cast<unsigned char>(c);
}

int SerialClass::peek() {
    pollInput();
    if (inputBuffer.empty()) {
        return -1;
    }
    return static_cast<unsigned char>(inputBuffer.front());
}

void SerialClass::flush() { std::cout << std::flush; }
#endif
