// juce_serialport_Linux.cpp
//
// ElfKick: cpr2323/juce_serialport ships Windows/macOS/Android/iOS
// implementations only. This is a minimal Linux stub: every operation
// fails safe (no port opened, no data moved), which is all the simulator
// build needs — SIM_BUILD never opens the port, and on a desktop there is
// no /dev/ttymxc0 for HwSerialData::connect() to find anyway.
// This file is part of the ElfKick repo (see .gitignore exception);
// scripts/fetch-deps.sh clones the module around it and preserves it.

// Note: do NOT include the app JuceHeader.h here — the generated header
// references <signal/signal.h>/<magic_enum/magic_enum.h> (wrong extensions)
// and no other TU includes it. The serialport header is self-contained.
#include "juce_serialport.h"

#if JUCE_LINUX

bool SerialPort::open( const juce::String& path )
{
    portPath = path;
    return false;
}

void SerialPort::close()
{
}

bool SerialPort::setConfig( const SerialPortConfig& )
{
    return false;
}

bool SerialPort::getConfig( SerialPortConfig& )
{
    return false;
}

juce::StringPairArray SerialPort::getSerialPortPaths()
{
    return {};
}

bool SerialPort::exists()
{
    return false;
}

void SerialPort::cancel()
{
}

//==============================================================================
void SerialPortInputStream::run()
{
    while( !threadShouldExit() )
        juce::Thread::sleep( 50 );
}

int SerialPortInputStream::read( void*, int )
{
    return 0;
}

void SerialPortInputStream::cancel()
{
}

//==============================================================================
void SerialPortOutputStream::run()
{
    while( !threadShouldExit() )
        juce::Thread::sleep( 50 );
}

bool SerialPortOutputStream::write( const void*, size_t )
{
    return false;
}

void SerialPortOutputStream::cancel()
{
}

#endif // JUCE_LINUX
