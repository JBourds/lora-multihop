#include <stdint.h>

#include <iostream>
#include <queue>
#include <sstream>
#include <string>

class SerialClass {
   public:
    void begin(unsigned long baud);
    void end();

    // Print
    void print(const std::string& s);
    void print(const char* s);
    void print(char c);
    void print(int value);
    void print(unsigned int value);
    void print(long value);
    void print(unsigned long value);
    void print(float value);
    void print(double value);

    // Println
    void println();
    void println(const std::string& s);
    void println(const char* s);
    void println(char c);
    void println(int value);
    void println(unsigned int value);
    void println(long value);
    void println(unsigned long value);
    void println(float value);
    void println(double value);

    // Write (Arduino-style)
    size_t write(uint8_t byte);
    size_t write(const char* buffer);
    size_t write(const uint8_t* buffer, size_t size);

    // Input
    int available();
    int read();
    int peek();
    void flush();

    explicit operator bool() const { return started; }

   private:
    void pollInput();

    bool started = false;
#ifdef SIMULATE
    std::queue<char> inputBuffer;
#endif
};

// Global instance like Arduino
extern SerialClass Serial;
