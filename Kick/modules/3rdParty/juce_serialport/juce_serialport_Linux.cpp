// juce_serialport_Linux.cpp
//
// ElfKick: cpr2323/juce_serialport ships Windows/macOS/Android/iOS
// implementations only. This file previously held a fail-safe stub
// (every operation a no-op), which was all the SIM_BUILD simulator
// needed. It is now a REAL termios backend, mirroring the module's own
// OSX implementation (juce_serialport_OSX.cpp) function for function,
// so the hardware (non-SIM) input path works on Linux:
// KickDeviceControl can open /dev/ttymxc0 — a real UART on the device,
// or anything that presents as a tty (e.g. a pty whose other end is
// the web-panel bridge, panel/BRIDGE.md). Behavioural contract kept
// identical to OSX: buffered async input stream with change
// notification, buffered output stream drained by its own thread.
// This file is part of the ElfKick repo (see .gitignore exception);
// scripts/fetch-deps.sh clones the module around it and preserves it.

// Note: do NOT include the app JuceHeader.h here — the generated header
// references <signal/signal.h>/<magic_enum/magic_enum.h> (wrong extensions)
// and no other TU includes it. The serialport header is self-contained.
#include "juce_serialport.h"

#if JUCE_LINUX

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <cstring>

namespace
{
    speed_t baudToSpeed( uint32_t bps )
    {
        switch( bps )
        {
            case 1200:   return B1200;
            case 2400:   return B2400;
            case 4800:   return B4800;
            case 9600:   return B9600;
            case 19200:  return B19200;
            case 38400:  return B38400;
            case 57600:  return B57600;
            case 115200: return B115200;
            case 230400: return B230400;
            case 460800: return B460800;
            case 921600: return B921600;
            default:     return B0; // hang up / unsupported
        }
    }
}

//==============================================================================
juce::StringPairArray SerialPort::getSerialPortPaths()
{
    // Linux counterpart of the OSX IOKit enumeration: the device UART
    // (ttymxc*, as on the i.MX8M) plus the usual USB-serial names.
    juce::StringPairArray paths;
    juce::File            devDir( "/dev" );
    if( !devDir.isDirectory() )
        return paths;

    const juce::StringArray patterns{ "ttymxc*", "ttyUSB*", "ttyACM*" };
    for( const auto& pattern : patterns )
    {
        for( const auto& child : devDir.findChildFiles( juce::File::findFiles, false, pattern ) )
        {
            const juce::String path = child.getFullPathName();
            paths.set( path, path );
        }
    }
    return paths;
}

bool SerialPort::open( const juce::String& path )
{
    portPath = path;
    DebugLog( "SerialPort::open", "opening port:" + portPath );

    portDescriptor = ::open( path.toRawUTF8(), O_RDWR | O_NOCTTY | O_NONBLOCK );
    if( portDescriptor == -1 )
    {
        DebugLog( "SerialPort::open", "open() failed" );
        return false;
    }

    // Don't allow multiple opens (non-critical if unsupported, e.g. ptys).
    if( ioctl( portDescriptor, TIOCEXCL ) == -1 )
        DebugLog( "SerialPort::open", "ioctl TIOCEXCL error, non critical" );

    // We want blocking IO actually (OSX parity).
    if( fcntl( portDescriptor, F_SETFL, 0 ) == -1 )
    {
        DebugLog( "SerialPort::open", "fcntl error" );
        close();
        return false;
    }

    struct termios options;
    if( tcgetattr( portDescriptor, &options ) == -1 )
    {
        DebugLog( "SerialPort::open", "can't get port settings" );
        close();
        return false;
    }

    // Non-canonical, read returns as soon as any data is received,
    // 0.5 second timeout (OSX parity).
    cfmakeraw( &options );
    options.c_cc[VMIN]  = 0;
    options.c_cc[VTIME] = 5;
    if( tcsetattr( portDescriptor, TCSANOW, &options ) == -1 )
    {
        DebugLog( "SerialPort::open", "can't set port settings (timeouts)" );
        close();
        return false;
    }
    return true;
}

void SerialPort::close()
{
    DebugLog( "SerialPort::close", "closing port:" + portPath );
    if( portDescriptor != -1 )
    {
        ::close( portDescriptor );
        portDescriptor = -1;
    }
}

bool SerialPort::setConfig( const SerialPortConfig& config )
{
    if( portDescriptor == -1 )
        return false;

    struct termios options;
    std::memset( &options, 0, sizeof( options ) );
    cfmakeraw( &options );
    options.c_cc[VMIN]  = 0;
    options.c_cc[VTIME] = 5;
    options.c_cflag |= CREAD;  // enable receiver
    options.c_cflag |= CLOCAL; // don't monitor modem control lines

    switch( config.databits )
    {
        case 5: options.c_cflag |= CS5; break;
        case 6: options.c_cflag |= CS6; break;
        case 7: options.c_cflag |= CS7; break;
        case 8:
        default: options.c_cflag |= CS8; break;
    }

    switch( config.parity )
    {
        case SerialPortConfig::SERIALPORT_PARITY_ODD:
            options.c_cflag |= PARENB;
            options.c_cflag |= PARODD;
            break;
        case SerialPortConfig::SERIALPORT_PARITY_EVEN:
            options.c_cflag |= PARENB;
            break;
        case SerialPortConfig::SERIALPORT_PARITY_MARK:
        case SerialPortConfig::SERIALPORT_PARITY_SPACE:
            DebugLog( "SerialPort::setConfig", "MARK/SPACE parity not supported on Linux" );
            return false;
        case SerialPortConfig::SERIALPORT_PARITY_NONE:
        default:
            break;
    }

    if( config.stopbits == SerialPortConfig::STOPBITS_1ANDHALF )
    {
        DebugLog( "SerialPort::setConfig", "STOPBITS_1ANDHALF not supported on Linux" );
        return false;
    }
    if( config.stopbits == SerialPortConfig::STOPBITS_2 )
        options.c_cflag |= CSTOPB;

    switch( config.flowcontrol )
    {
        case SerialPortConfig::FLOWCONTROL_XONXOFF:
            options.c_iflag |= IXON;
            options.c_iflag |= IXOFF;
            break;
        case SerialPortConfig::FLOWCONTROL_HARDWARE:
            options.c_cflag |= CRTSCTS;
            break;
        case SerialPortConfig::FLOWCONTROL_NONE:
        default:
            break;
    }

    const speed_t speed = baudToSpeed( config.bps );
    if( speed == B0 && config.bps != 0 )
    {
        DebugLog( "SerialPort::setConfig", "unsupported baud rate" );
        return false;
    }
    cfsetispeed( &options, speed );
    cfsetospeed( &options, speed );

    if( tcsetattr( portDescriptor, TCSANOW, &options ) == -1 )
    {
        DebugLog( "SerialPort::setConfig", "can't set port settings" );
        return false;
    }
    return true;
}

bool SerialPort::getConfig( SerialPortConfig& config )
{
    if( portDescriptor == -1 )
        return false;

    struct termios options;
    if( tcgetattr( portDescriptor, &options ) == -1 )
    {
        DebugLog( "SerialPort::getConfig", "cannot get port settings" );
        return false;
    }

    config.bps = (uint32_t)cfgetospeed( &options );
    switch( options.c_cflag & CSIZE )
    {
        case CS5: config.databits = 5; break;
        case CS6: config.databits = 6; break;
        case CS7: config.databits = 7; break;
        case CS8:
        default: config.databits = 8; break;
    }
    config.parity = SerialPortConfig::SERIALPORT_PARITY_NONE;
    if( options.c_cflag & PARENB )
        config.parity = ( options.c_cflag & PARODD ) ? SerialPortConfig::SERIALPORT_PARITY_ODD
                                                     : SerialPortConfig::SERIALPORT_PARITY_EVEN;
    config.stopbits    = ( options.c_cflag & CSTOPB ) ? SerialPortConfig::STOPBITS_2
                                                      : SerialPortConfig::STOPBITS_1;
    config.flowcontrol = SerialPortConfig::FLOWCONTROL_NONE;
    if( ( options.c_iflag & IXON ) || ( options.c_iflag & IXOFF ) )
        config.flowcontrol = SerialPortConfig::FLOWCONTROL_XONXOFF;
    else if( options.c_cflag & CRTSCTS )
        config.flowcontrol = SerialPortConfig::FLOWCONTROL_HARDWARE;
    return true;
}

bool SerialPort::exists()
{
    return portDescriptor != -1;
}

void SerialPort::cancel()
{
}

//==============================================================================
void SerialPortInputStream::run()
{
    // OSX reads a byte at a time; here we read whatever is available
    // (bounded) — the bufferedbytes/read() contract seen by callers is
    // identical. Unlike OSX we also survive the descriptor being -1 at
    // thread start (port opened after stream creation, or a reconnect).
    unsigned char chunk[512];
    while( !threadShouldExit() )
    {
        if( port == nullptr )
            break;
        if( port->portDescriptor == -1 )
        {
            juce::Thread::sleep( 20 );
            continue;
        }

        const auto bytesRead = ::read( port->portDescriptor, chunk, sizeof( chunk ) );
        if( bytesRead > 0 )
        {
            bool shouldNotify = false;
            {
                const juce::ScopedLock l( bufferCriticalSection );
                buffer.ensureSize( (size_t)( bufferedbytes + bytesRead ) );
                std::memcpy( (char*)buffer.getData() + bufferedbytes, chunk, (size_t)bytesRead );
                bufferedbytes += (int)bytesRead;

                if( notify == NOTIFY_ALWAYS )
                    shouldNotify = true;
                else if( notify == NOTIFY_ON_CHAR )
                {
                    for( int i = 0; i < bytesRead; i++ )
                        if( chunk[i] == notifyChar )
                            shouldNotify = true;
                }
            }
            if( shouldNotify )
                sendChangeMessage();
        }
        else if( bytesRead == -1 && errno != EAGAIN && errno != EINTR )
        {
            port->DebugLog( "SerialPortInputStream::run",
                            "::read() returned -1, errno: " + juce::String( errno ) );
            port->close();
            break;
        }
        // bytesRead == 0: VTIME timeout with no data — loop again.
    }
}

int SerialPortInputStream::read( void* destBuffer, int maxBytesToRead )
{
    if( port != nullptr && port->portDescriptor != -1 )
    {
        const juce::ScopedLock l( bufferCriticalSection );
        if( maxBytesToRead > bufferedbytes )
            maxBytesToRead = bufferedbytes;
        std::memcpy( destBuffer, buffer.getData(), (size_t)maxBytesToRead );
        buffer.removeSection( 0, (size_t)maxBytesToRead );
        bufferedbytes -= maxBytesToRead;
        return maxBytesToRead;
    }
    return -1;
}

void SerialPortInputStream::cancel()
{
}

//==============================================================================
void SerialPortOutputStream::run()
{
    unsigned char tempBuffer[writeBufferSize];
    while( port != nullptr && !threadShouldExit() )
    {
        if( port->portDescriptor == -1 )
        {
            triggerWrite.wait( 100 );
            continue;
        }
        if( !bufferedbytes )
            triggerWrite.wait( 100 );
        if( bufferedbytes )
        {
            int bytesToWrite;
            {
                const juce::ScopedLock l( bufferCriticalSection );
                bytesToWrite = bufferedbytes > (int)writeBufferSize ? (int)writeBufferSize : bufferedbytes;
                std::memcpy( tempBuffer, buffer.getData(), (size_t)bytesToWrite );
            }
            const auto bytesWritten = ::write( port->portDescriptor, tempBuffer, (size_t)bytesToWrite );
            if( bytesWritten > 0 )
            {
                const juce::ScopedLock l( bufferCriticalSection );
                buffer.removeSection( 0, (size_t)bytesWritten );
                bufferedbytes -= (int)bytesWritten;
            }
            else if( bytesWritten == -1 && errno != EAGAIN && errno != EINTR )
            {
                port->DebugLog( "SerialPortOutputStream::run",
                                "::write() couldn't write anything, errno: " + juce::String( errno ) );
                port->close();
                break;
            }
        }
    }
}

bool SerialPortOutputStream::write( const void* dataToWrite, size_t howManyBytes )
{
    {
        const juce::ScopedLock l( bufferCriticalSection );
        buffer.append( dataToWrite, howManyBytes );
        bufferedbytes += (int)howManyBytes;
    }
    triggerWrite.signal();
    return true;
}

void SerialPortOutputStream::cancel()
{
}

#endif // JUCE_LINUX
