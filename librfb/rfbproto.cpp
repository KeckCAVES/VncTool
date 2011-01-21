/*
 *  RFBProtocol.cpp
 *
 *  Copyright (C) 2007 Voltaic.  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

#include "rfbproto.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>
#include "d3des.h"

using namespace rfb;



//----------------------------------------------------------------------

static bool TestIsLocalMachine(uint32_t hostAddrInNetworkByteOrder)
{
    // NOTE: This should be more sophisticated...

    const struct hostent* const hp = gethostbyname("localhost");

    if (!hp || (hp->h_addrtype != AF_INET))
        return false;
    else
    {
        for (int i = 0; i < hp->h_length; i++)
        {
            const uint32_t addr = ((struct in_addr*)hp->h_addr_list[0])->s_addr;

            if (addr == hostAddrInNetworkByteOrder)
                return true;
        }

        return false;
    }
}



//----------------------------------------------------------------------
// Nested class ZlibDecompressor

RFBProtocol::ZlibDecompressor::ZlibDecompressor() :
    outer(0),
    isOpen(false),
    start(buffer),
    end(buffer),
    ptr(buffer),
    offset(0),
    bytesIn(0)
{
    memset(&zs, 0, sizeof(zs));
    zs.zalloc  = Z_NULL;
    zs.zfree   = Z_NULL;
    zs.opaque  = Z_NULL;
    zs.next_in = Z_NULL;

    memset(buffer, 0, sizeof(buffer));
}



RFBProtocol::ZlibDecompressor::~ZlibDecompressor()
{
    close();
}



bool RFBProtocol::ZlibDecompressor::init()
{
    if (isOpen)
        return false;
    else if (inflateInit(&zs) != Z_OK)
    {
        memset(&zs, 0, sizeof(zs));
        zs.zalloc  = Z_NULL;
        zs.zfree   = Z_NULL;
        zs.opaque  = Z_NULL;
        zs.next_in = Z_NULL;

        return false;
    }
    else
    {
        isOpen = true;

        return true;
    }
}



void RFBProtocol::ZlibDecompressor::close()
{
    if (isOpen)
    {
        inflateEnd(&zs);

        start   = buffer;
        end     = buffer;
        ptr     = buffer;
        offset  = 0;
        bytesIn = 0;

        memset(&zs, 0, sizeof(zs));
        zs.zalloc  = Z_NULL;
        zs.zfree   = Z_NULL;
        zs.opaque  = Z_NULL;
        zs.next_in = Z_NULL;

        memset(buffer, 0, sizeof(buffer));

        isOpen = false;
    }
}



void RFBProtocol::ZlibDecompressor::reset()
{
    ptr = end = start;

    while (bytesIn > 0)
    {
        (void)decompress();
        end = start; // throw away any data
    }
}



int RFBProtocol::ZlibDecompressor::pos()
{
    return (offset + ptr - start);
}



int RFBProtocol::ZlibDecompressor::overrun(size_t itemSize, unsigned nItems)
{
    if (itemSize > BUFFER_SIZE)
        return -1;
    else if (nItems > INT_MAX)
        return -1;
    else
    {
        if ((end - ptr) != 0)
            memmove(start, ptr, end-ptr);

        const int diff = ptr - start;
        offset += diff;
        end    -= diff;

        ptr = start;

        while ((end < ptr) || (size_t(end - ptr) < itemSize))
            if (!decompress())
                return -1;

        if (end < ptr)
            nItems = 0;
        else if (itemSize*nItems > size_t(end - ptr))
            nItems = (end - ptr) / itemSize;

        return nItems;
    }
}



bool RFBProtocol::ZlibDecompressor::readBytes(void* data, int length)
{
    rfbCARD8*             dataPtr = (rfbCARD8*)data;
    const rfbCARD8* const dataEnd = dataPtr+length;

    while (dataPtr < dataEnd)
    {
        const int n = fill(1, dataEnd-dataPtr);
        if (n < 0)
            return false;

        memcpy(dataPtr, ptr, n);
        ptr     += n;
        dataPtr += n;
    }

    return true;
}



// decompress() calls the decompressor once.  Note that this won't necessarily
// generate any output data - it may just consume some input data.

bool RFBProtocol::ZlibDecompressor::decompress()
{
    zs.next_out  = (rfbCARD8*)end;
    zs.avail_out = start + BUFFER_SIZE - end;

    size_t avail = outer->commBufferFill - outer->commBufferPos;
    if (avail < 1)
        if (!outer->checkAvailableFromRFBServer(1))
            return false;

    zs.next_in  = (rfbCARD8*)(outer->commBuffer + outer->commBufferPos);
    zs.avail_in = avail;
    if (zs.avail_in > bytesIn)
        zs.avail_in = bytesIn;

    if (inflate(&zs, Z_SYNC_FLUSH) != Z_OK)
        return false;

    bytesIn -= ((const char*)zs.next_in - (const char*)(outer->commBuffer + outer->commBufferPos));
    end = zs.next_out;
    outer->commBufferPos = ((const char*)zs.next_in - (const char*)outer->commBuffer);

    return true;
}



//----------------------------------------------------------------------
// Main class RFBProtocol

const rfbCARD16 RFBProtocol::EndianTest = 1;



RFBProtocol::RFBProtocol() :
    isOpen(false),
    isSameMachine(false),
    desktopHost(0),
    rfbPort(0),
    portOffset(0),
    requestedEncodings(0),
    desktopName(0),
    sock(-1),
    sockConnected(false),
    currentEncoding(0),
    serverCutText(0),
    newServerCutText(false),
    isBigEndian(false),
    updateNeededX1(0),
    updateNeededY1(0),
    updateNeededX2(0),
    updateNeededY2(0),
    updateNeeded(false),
    updateDefinitelyExpected(false),
    zlibDecompressor(),
    commBufferPos(0),
    commBufferFill(0)
{
    zlibDecompressor.outer = this;

    memset(&desktopIPAddress, 0, sizeof(desktopIPAddress));
    memset(&pixelFormat,      0, sizeof(pixelFormat));
    memset(&si,               0, sizeof(si));
    memset(commBuffer,        0, COMM_BUFFER_SIZE);
    memset(decodeBuffer,      0, DECODE_BUFFER_SIZE);
}



RFBProtocol::~RFBProtocol()
{
    this->close();
}



bool RFBProtocol::initViaConnect(const char*           theDesktopHost,
                                 unsigned              theRfbPort,
                                 const rfbPixelFormat& theRequestedPixelFormat,
                                 const char*           theRequestedEncodings,
                                 bool                  theSharedDesktopFlag)
{
    bool result = true;  // for now...

    this->infoServerInitStarted();

    if (isOpen)
    {
        this->errorMessage("RFBProtocol::initViaConnect", "attempt to initialize when already open");
        result = false;
    }
    else
    {
        isOpen = true;  // set this regardless so that close() will clean up if we fail, also so that readFromRFBServer() and writeToRFBServer() don't fail...

        pixelFormat = theRequestedPixelFormat;

        if (theRequestedEncodings)
            requestedEncodings = strdup(theRequestedEncodings);

        if (theRequestedEncodings && !requestedEncodings)
        {
            if (isOpen) this->errorMessage("RFBProtocol::initViaConnect", "memory allocation failed");
            result = false;
        }
        else
        {
            if (!theDesktopHost)
                theDesktopHost = "";

            desktopHost = strdup(theDesktopHost);
            if (!desktopHost)
            {
                if (isOpen) this->errorMessage("RFBProtocol::initViaConnect", "memory allocation failed");
                result = false;
            }
            else
            {
                // Get the IP address for desktopHost
                uint32_t hostAddr = 0;  // local

                if (!*desktopHost)  // 0 or empty ==> local
                    isSameMachine = true;
                else
                {

                    hostAddr = inet_addr(desktopHost);
                    if (hostAddr == (uint32_t)-1)
                    {
                        const struct hostent* const hp = gethostbyname(desktopHost);

                        if (hp && (hp->h_addrtype == AF_INET))
                            hostAddr = *(uint32_t*)hp->h_addr;
                    }
                }

                if (hostAddr == (uint32_t)-1)
                {
                    if (isOpen) this->errorMessage("RFBProtocol::initViaConnect", "hostname resolution failed");
                    result = false;
                }
                else
                {
                    if (!isSameMachine)
                        isSameMachine = TestIsLocalMachine(hostAddr);

                    rfbPort    = theRfbPort;
                    portOffset = CONNECT_PORT_OFFSET;

                    memset(&desktopIPAddress, 0, sizeof(desktopIPAddress));
                    desktopIPAddress.sin_family      = AF_INET;
                    desktopIPAddress.sin_port        = htons(this->getTcpPort());
                    desktopIPAddress.sin_addr.s_addr = hostAddr;

                    sock = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock < 0)
                    {
                        if (isOpen) this->errorMessage("RFBProtocol::initViaConnect", "socket allocation failed");
                        sock = -1;  // keep "closed" value standard
                        result = false;
                    }
                    else if (connect(sock, (struct sockaddr*)&desktopIPAddress, sizeof(desktopIPAddress)) < 0)
                    {
                        if (isOpen) this->errorMessage("RFBProtocol::initViaConnect", "socket connect failed");
                        result = false;
                    }
                    else
                    {
                        sockConnected = true;

                        result = this->finishInit(theSharedDesktopFlag);
                    }
                }
            }
        }
    }

    this->infoServerInitCompleted(result);

    if (!result)
        this->close();  // Note: after closing, this object may have been deleted (in response to the infoCloseCompleted() notification)

    return result;
}



bool RFBProtocol::initViaListen(unsigned              theRfbPort,
                                const rfbPixelFormat& theRequestedPixelFormat,
                                const char*           theRequestedEncodings,
                                bool                  theSharedDesktopFlag)
{
    bool result = true;  // for now...

    this->infoServerInitStarted();

    if (isOpen)
    {
        this->errorMessage("RFBProtocol::initViaListen", "attempt to initialize when already open");
        result = false;
    }
    else
    {
        isOpen = true;  // set this regardless so that close() will clean up if we fail, also so that readFromRFBServer() and writeToRFBServer() don't fail...

        pixelFormat = theRequestedPixelFormat;

        if (theRequestedEncodings)
            requestedEncodings = strdup(theRequestedEncodings);

        if (theRequestedEncodings && !requestedEncodings)
        {
            if (isOpen) this->errorMessage("RFBProtocol::initViaListen", "memory allocation failed");
            result = false;
        }
        else
        {
            rfbPort    = theRfbPort;
            portOffset = LISTEN_PORT_OFFSET;

            memset(&desktopIPAddress, 0, sizeof(desktopIPAddress));

            const int acceptSock = socket(AF_INET, SOCK_STREAM, 0);
            if (acceptSock < 0)
            {
                if (isOpen) this->errorMessage("RFBProtocol::initViaListen", "socket allocation failed");
                result = false;
            }
            else
            {
                struct sockaddr_in acceptIPAddress;
                memset(&acceptIPAddress, 0, sizeof(acceptIPAddress));
                acceptIPAddress.sin_family      = AF_INET;
                acceptIPAddress.sin_port        = htons(this->getTcpPort());
                acceptIPAddress.sin_addr.s_addr = htonl(INADDR_ANY);

                if (bind(acceptSock, (struct sockaddr*)&acceptIPAddress, sizeof(acceptIPAddress)) < 0)
                {
                    if (isOpen) this->errorMessage("RFBProtocol::initViaListen", "socket bind failed");
                    result = false;
                }
                else
                {
                    const int backlog = 8;
                    if (listen(acceptSock, backlog) < 0)
                    {
                        if (isOpen) this->errorMessage("RFBProtocol::initViaListen", "socket accept failed");
                        sock = -1;  // keep "closed" value standard
                        result = false;
                    }
                    else
                    {
                        if ((sock = accept(acceptSock, 0, 0)) < 0)
                        {
                            if (isOpen) this->errorMessage("RFBProtocol::initViaListen", "socket accept failed");
                            sock = -1;  // keep "closed" value standard
                            result = false;
                        }
                        else
                        {
                            //!!! set desktopIPAddress !!!
                            //!!! set isSameMachine correctly !!!
                            isSameMachine = true;

                            sockConnected = true;

                            result = this->finishInit(theSharedDesktopFlag);
                        }
                    }
                }

                ::close(acceptSock);
            }
        }
    }

    this->infoServerInitCompleted(result);

    if (!result)
        this->close();  // Note: after closing, this object may have been deleted (in response to the infoCloseCompleted() notification)

    return result;
}



void RFBProtocol::close()
{
    if (isOpen)
    {
        isOpen = false;

        this->infoCloseStarted();

        if (sock >= 0)
        {
            if (sockConnected)
            {
                ::shutdown(sock, SHUT_RDWR);
                sockConnected = false;
            }

            ::close(sock);
            sock = -1;
        }

        isSameMachine = false;
        if (desktopHost) { free((void*)desktopHost); desktopHost = 0; }
        memset(&desktopIPAddress, 0, sizeof(desktopIPAddress));
        rfbPort    = 0;
        portOffset = 0;
        memset(&pixelFormat, 0, sizeof(pixelFormat));
        if (requestedEncodings) { free((void*)requestedEncodings); requestedEncodings = 0; }
        if (desktopName) { free((void*)desktopName); desktopName = 0; }
        memset(&si, 0, sizeof(si));
        currentEncoding = 0;
        if (serverCutText) { free((void*)serverCutText); serverCutText = 0; }
        newServerCutText = false;
        isBigEndian = false;
        updateNeededX1 = 0;
        updateNeededY1 = 0;
        updateNeededX2 = 0;
        updateNeededY2 = 0;
        updateNeeded = false;
        updateDefinitelyExpected = false;
        zlibDecompressor.close();
        commBufferPos  = 0;
        commBufferFill = 0;
        memset(commBuffer,   0, COMM_BUFFER_SIZE);
        memset(decodeBuffer, 0, DECODE_BUFFER_SIZE);

        this->infoCloseCompleted();
    }
}



bool RFBProtocol::finishInit(bool theSharedDesktopFlag)
{
    bool result = true;  // for now...

    if (!zlibDecompressor.init())
    {
        if (isOpen) this->errorMessage("RFBProtocol::finishInit", "ZRLE decompressor initialization failed");
        result = false;
    }
    else
    {
        rfbProtocolVersionMsg pv;

        if (!this->readFromRFBServer(pv, sz_rfbProtocolVersionMsg))
        {
            if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
            result = false;
        }
        else
        {
            pv[sz_rfbProtocolVersionMsg] = 0;

            int major, minor;
            if (sscanf(pv, rfbProtocolVersionFormat, &major, &minor) != 2)
            {
                if (isOpen) this->errorMessage("RFBProtocol:finishInit", "unexpected protocol version format");
                result = false;
            }
            else
            {
                this->infoProtocolVersion(major, minor, rfbProtocolMajorVersion, rfbProtocolMinorVersion);

                sprintf(pv, rfbProtocolVersionFormat, rfbProtocolMajorVersion, rfbProtocolMinorVersion);

                if (!this->writeToRFBServer(pv, sz_rfbProtocolVersionMsg))
                {
                    if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket write error");
                    result = false;
                }
                else
                {
                    rfbCARD32 authScheme;
                    if (!this->readFromRFBServer(&authScheme, sizeof(authScheme)))
                    {
                        if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
                        result = false;
                    }
                    else
                    {
                        authScheme = Swap32IfLE(authScheme);

                        rfbCARD32 authResult = (rfbCARD32)-1;

                        switch (authScheme)
                        {
                            case rfbConnFailed:
                            {
                                rfbCARD32 reasonLen;
                                if (!this->readFromRFBServer(&reasonLen, sizeof(reasonLen)))
                                {
                                    if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
                                    result = false;
                                }
                                else
                                {
                                    reasonLen = Swap32IfLE(reasonLen);

                                    char* const reason = (char*)malloc(reasonLen + 1);
                                    if (!reason)
                                    {
                                        if (isOpen) this->errorMessage("RFBProtocol:finishInit", "memory allocation failed");
                                        result = false;
                                    }
                                    else
                                    {
                                        if (!this->readFromRFBServer(reason, reasonLen))
                                        {
                                            if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
                                            result = false;
                                        }
                                        else
                                        {
                                            reason[reasonLen] = 0;

                                            // Call the special errorMessageFromServer() in case "reason"
                                            // is from a malicious server....
                                            if (isOpen) this->errorMessageFromServer("RFBProtocol:finishInit", reason);
                                            result = false;  // we already failed and were just reporting the error...
                                        }

                                        free(reason);
                                    }
                                }
                            }
                            break;

                            case rfbVncAuth:
                            {
                                const size_t CHALLENGESIZE = 16;

                                rfbCARD8 challenge[CHALLENGESIZE];
                                if (!this->readFromRFBServer(challenge, CHALLENGESIZE))
                                {
                                    if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
                                    result = false;
                                }
                                else
                                {
                                    char* const passwd = this->returnPassword();

                                    if (!passwd || !isOpen)
                                    {
                                        if (passwd) free(passwd);
                                        if (isOpen) this->errorMessage("RFBProtocol:finishInit", "no password specified");
                                        result = false;
                                    }
                                    else
                                    {
                                        const int passwdLen = strlen(passwd);
                                        if (passwdLen <= 0)
                                        {
                                            if (isOpen) this->errorMessage("RFBProtocol:finishInit", "empty password specified");
                                            result = false;
                                        }
                                        else
                                        {
                                            if (passwdLen > 8)
                                                passwd[8] = 0;

                                            const size_t RESPONSESIZE = CHALLENGESIZE;
                                            rfbCARD8 response[RESPONSESIZE];
                                            const bool encryptChallengeFailed = !this->encryptChallenge(challenge, CHALLENGESIZE, (const unsigned char*)passwd, response, RESPONSESIZE);

                                            // Erase the password from memory:
                                            memset(passwd, 0, passwdLen);

                                            if (encryptChallengeFailed || !isOpen)
                                            {
                                                if (isOpen) this->errorMessage("RFBProtocol:finishInit", "empty password specified");
                                                result = false;
                                            }
                                            else
                                            {
                                                if (!this->writeToRFBServer(response, RESPONSESIZE))
                                                {
                                                    if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket write error");
                                                    result = false;
                                                }
                                                else
                                                {
                                                    if (!this->readFromRFBServer(&authResult, sizeof(authResult)))
                                                    {
                                                        if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
                                                        result = false;
                                                    }
                                                    else
                                                    {
                                                        authResult = Swap32IfLE(authResult);

                                                        result = (authResult == rfbVncAuthOK);
                                                        if (!result)
                                                        {
                                                            if (isOpen) this->errorMessage("RFBProtocol:finishInit", "authentication failed");
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        free(passwd);
                                    }
                                }
                            }
                            break;

                            case rfbNoAuth:
                            {
                                // No authentication...
                            }
                            break;

                            default:
                            {
                                if (isOpen) this->errorMessage1l("RFBProtocol:finishInit", "unknown authentication scheme", authScheme);
                                result = false;
                            }
                            break;
                        }

                        if (!isOpen)
                            result = false;  // in case this object was closed while we were waiting for input
                        else
                            this->infoAuthenticationResult(result, authScheme, rfbVncAuthOK);

                        if (result)
                        {
                            rfbClientInitMsg ci;
                            memset(&ci, 0, sizeof(ci));

                            ci.shared = (theSharedDesktopFlag ? 1 : 0);

                            if (!this->writeToRFBServer(&ci, sz_rfbClientInitMsg))
                            {
                                if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket write error");
                                result = false;
                            }
                            else if (!this->readFromRFBServer(&si, sz_rfbServerInitMsg))
                            {
                                if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
                                result = false;
                            }
                            else
                            {
                                si.framebufferWidth  = Swap16IfLE(si.framebufferWidth);
                                si.framebufferHeight = Swap16IfLE(si.framebufferHeight);
                                si.format.redMax     = Swap16IfLE(si.format.redMax);
                                si.format.greenMax   = Swap16IfLE(si.format.greenMax);
                                si.format.blueMax    = Swap16IfLE(si.format.blueMax);
                                si.nameLength        = Swap32IfLE(si.nameLength);

                                framebufferWidth  = si.framebufferWidth;
                                framebufferHeight = si.framebufferHeight;

                                char* const dn = (char*)malloc(si.nameLength + 1);
                                if (!dn)
                                {
                                    if (isOpen) this->errorMessage("RFBProtocol:finishInit", "memory allocation failed");
                                    result = false;
                                }
                                else if (!this->readFromRFBServer(dn, si.nameLength))
                                {
                                    if (isOpen) this->errorMessage("RFBProtocol:finishInit", "socket read error");
                                    free(dn);
                                    result = false;
                                }
                                else
                                {
                                    dn[si.nameLength] = 0;

                                    desktopName = dn;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return result;
}



bool RFBProtocol::sendSetPixelFormat()
{
    rfbSetPixelFormatMsg spf;
    memset(&spf, 0, sizeof(spf));

    spf.type   = rfbSetPixelFormat;
    spf.format = pixelFormat;

    spf.format.redMax   = Swap16IfLE(spf.format.redMax);
    spf.format.greenMax = Swap16IfLE(spf.format.greenMax);
    spf.format.blueMax  = Swap16IfLE(spf.format.blueMax);

    if (!this->writeToRFBServer(&spf, sz_rfbSetPixelFormatMsg))
    {
        if (isOpen) this->errorMessageForSend("RFBProtocol::sendSetPixelFormat", "socket write error");
        return false;
    }
    else
        return true;
}



// Specifiers for encodings, in order of preference.
// rfbEncodingCopyRect must be first....
static const struct { const char* name; rfbCARD32 value; } SupportedEncodings[] =
{
    { "copyrect",    rfbEncodingCopyRect    },  // rfbEncodingCopyRect must be first
    { "zrle",        rfbEncodingZRLE        },
    { "hextile",     rfbEncodingHextile     },
    { "corre",       rfbEncodingCoRRE       },
    { "rre",         rfbEncodingRRE         },
    { "raw",         rfbEncodingRaw         },
    { "desktopsize", rfbEncodingDesktopSize }
};

#define NUM_SUPPORTED_ENCODINGS  (sizeof(SupportedEncodings)/sizeof(*SupportedEncodings))



size_t RFBProtocol::scanEncodingsString(rfbCARD32* pDest = 0) const
{
    size_t n = 0;

    const char* encStr = requestedEncodings;
    if (encStr)
    {
        do
        {
            const char* nextEncStr = strchr(encStr, ' ');

            const int encStrLen = nextEncStr ? (int)(nextEncStr++ - encStr) : strlen(encStr);
            if (encStrLen > 0)
            {
                bool found = false;
                for (size_t i = 0; i < NUM_SUPPORTED_ENCODINGS; i++)
                {
                    if (strncasecmp(encStr, SupportedEncodings[i].name, encStrLen) == 0)
                    {
                        found = true;

                        if (pDest)
                            *pDest++ = Swap32IfLE(SupportedEncodings[i].value);

                        break;
                    }
                }

                if (found)
                    n++;
                else if (!pDest)  // only emit error messages on first call to this method (the counting pass with pDest == 0)
                {
                    if (isOpen) this->errorMessageForSend("RFBProtocol::scanEncodingsString", "unknown encoding type encountered");
                }
            }

            encStr = nextEncStr;
        }
        while (encStr);  // end of do-while
    }

    return n;
}



bool RFBProtocol::sendSetEncodings()
{
    bool result = true;

    const size_t n =
        requestedEncodings
            ? scanEncodingsString()           // count encodings in requestedEncodings if specified
            : (NUM_SUPPORTED_ENCODINGS + 1);  // one extra just in case currentEncoding is not in the list

    const size_t bufSize = sz_rfbSetEncodingsMsg + n*sizeof(rfbCARD32);
    char* const  buf     = (char*)malloc(bufSize);
    if (!buf)
    {
        if (isOpen) this->errorMessageForSend("RFBProtocol::sendSetEncodings", "memory allocation failed");
        result = false;
    }
    else
    {
        memset(buf, 0, bufSize);

        rfbSetEncodingsMsg* const se   = (rfbSetEncodingsMsg*)buf;
        rfbCARD32* const          encs = (rfbCARD32*)(&buf[sz_rfbSetEncodingsMsg]);

        se->type       = rfbSetEncodings;
        se->nEncodings = 0;

        if (requestedEncodings)
        {
            (void)scanEncodingsString(encs);
            se->nEncodings = n;
        }
        else
        {
            for (size_t i = 0; i < NUM_SUPPORTED_ENCODINGS; i++)
            {
                const rfbCARD32 encoding = SupportedEncodings[i].value;

                if (encoding != currentEncoding)
                    encs[se->nEncodings++] = Swap32IfLE(encoding);

                if (i == 0)
                    encs[se->nEncodings++] = Swap32IfLE(currentEncoding);  // insert currentEncoding as second entry, after rfbEncodingCopyRect (which is always SupportedEncodings[0])
            }
        }
//{ fprintf(stderr, "%ld encodings:\n", (long)se->nEncodings); for (int i = 0; i < se->nEncodings; i++) fprintf(stderr, "    - 0x%08lx\n", (long)Swap32IfLE(encs[i])); }//!!!

        const size_t len = sz_rfbSetEncodingsMsg + se->nEncodings*sizeof(rfbCARD32);

        se->nEncodings = Swap16IfLE(se->nEncodings);

        if (!this->writeToRFBServer(buf, len))
        {
            if (isOpen) this->errorMessageForSend("RFBProtocol::sendSetEncodings", "socket write error");
            result = false;
        }

        free(buf);
    }

    return result;
}



bool RFBProtocol::sendFramebufferUpdateRequest(int x, int y, size_t w, size_t h, bool incremental)
{
    rfbFramebufferUpdateRequestMsg fur;
    memset(&fur, 0, sizeof(fur));

    fur.type        = rfbFramebufferUpdateRequest;
    fur.incremental = incremental ? 1 : 0;
    fur.x           = Swap16IfLE((rfbCARD16)x);
    fur.y           = Swap16IfLE((rfbCARD16)y);
    fur.w           = Swap16IfLE((rfbCARD16)w);
    fur.h           = Swap16IfLE((rfbCARD16)h);

    if (!this->writeToRFBServer(&fur, sz_rfbFramebufferUpdateRequestMsg))
    {
        if (isOpen) this->errorMessageForSend("RFBProtocol::sendFramebufferUpdateRequest", "socket write error");
        return false;
    }
    else
        return true;
}



bool RFBProtocol::sendKeyEvent(rfbCARD32 key, bool down)
{
    rfbKeyEventMsg ke;
    memset(&ke, 0, sizeof(ke));

    ke.type = rfbKeyEvent;
    ke.down = down ? 1 : 0;
    ke.key  = Swap32IfLE(key);

    if (!this->writeToRFBServer(&ke, sz_rfbKeyEventMsg))
    {
        if (isOpen) this->errorMessageForSend("RFBProtocol::sendKeyEvent", "socket write error");
        return false;
    }
    else
        return true;
}



bool RFBProtocol::sendPointerEvent(int x, int y, int buttonMask)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    rfbPointerEventMsg pe;
    memset(&pe, 0, sizeof(pe));

    pe.type       = rfbPointerEvent;
    pe.buttonMask = buttonMask;
    pe.x          = Swap16IfLE((rfbCARD16)x);
    pe.y          = Swap16IfLE((rfbCARD16)y);

    if (!this->writeToRFBServer(&pe, sz_rfbPointerEventMsg))
    {
        if (isOpen) this->errorMessageForSend("RFBProtocol::sendPointerEvent", "socket write error");
        return false;
    }
    else
        return true;
}



bool RFBProtocol::sendClientCutText(const char* str, size_t len)
{
    if (serverCutText)
    {
        free((void*)serverCutText);
        serverCutText = 0;
    }

    if (!str)
    {
        if (isOpen) this->errorMessageForSend("RFBProtocol::sendClientCutText", "null cut text");
        return false;
    }
    else
    {
        rfbClientCutTextMsg cct;
        memset(&cct, 0, sizeof(cct));

        cct.type   = rfbClientCutText;
        cct.length = Swap32IfLE(len);

        if ( !this->writeToRFBServer(&cct, sz_rfbClientCutTextMsg) ||
             ((len > 0) && !this->writeToRFBServer(str, len)) )
        {
            if (isOpen) this->errorMessageForSend("RFBProtocol::sendClientCutText", "socket write error");
            return false;
        }
        else
            return true;
    }
}



bool RFBProtocol::sendStringViaKeyEvents( const char* str,
                                          size_t      len,
                                          rfbCARD32   tabKeySym,
                                          rfbCARD32   enterKeySym,
                                          rfbCARD32   leftControlKeySym )
{
    if (!str && (len > 0))
        return false;
    else
    {
        while (len > 0)
        {
            const char ch = *str;

            if ((tabKeySym != 0) && (ch == '\t'))
            {
                if ( !sendKeyEvent((rfbCARD32)tabKeySym, true)  ||
                     !sendKeyEvent((rfbCARD32)tabKeySym, false)    )
                {
                    return false;
                }
            }
            else if ((enterKeySym != 0) && (ch == '\n'))
            {
                if ( !sendKeyEvent((rfbCARD32)enterKeySym, true)  ||
                     !sendKeyEvent((rfbCARD32)enterKeySym, false)    )
                {
                    return false;
                }
            }
            else if ((leftControlKeySym != 0) && (ch < 32))
            {
                if ( !sendKeyEvent(leftControlKeySym,  true)  ||
                     !sendKeyEvent((rfbCARD32)(ch+64), true)  ||
                     !sendKeyEvent((rfbCARD32)(ch+64), false) ||
                     !sendKeyEvent(leftControlKeySym,  false)    )
                {
                    return false;
                }
            }
            else
            {
                if ( !sendKeyEvent((rfbCARD32)ch, true)  ||
                     !sendKeyEvent((rfbCARD32)ch, false)    )
                {
                    return false;
                }
            }

            len--;
            str++;
        }

        return true;
    }
}



bool RFBProtocol::handleRFBServerMessage()
{
    rfbServerToClientMsg msg;
    memset(&msg, 0, sizeof(msg));

    if (!this->readFromRFBServer(&msg, 1))
    {
        if (isOpen) this->errorMessage("RFBProtocol::handleRFBServerMessage", "socket read error for message type");
        return false;
    }
    else
    {
        if (!isOpen)
            return false;  // in case this object was closed while we were waiting for input
        else
        {
            switch (msg.type)
            {
                case rfbFramebufferUpdate:
                {
                    if (!this->readFromRFBServer((char*)&msg + 1, sz_rfbFramebufferUpdateMsg - 1))
                    {
                        if (isOpen) this->errorMessage("RFBProtocol::handleRFBServerMessage", "socket read error for rfbFramebufferUpdate");
                        return false;
                    }
                    else
                    {
                        msg.fu.nRects = Swap16IfLE(msg.fu.nRects);

                        return this->receivedFramebufferUpdate(msg.fu);
                    }
                }
                break;

                case rfbSetColourMapEntries:
                {
                    if (!this->readFromRFBServer((char*)&msg + 1, sz_rfbSetColourMapEntriesMsg - 1))
                    {
                        if (isOpen) this->errorMessage("RFBProtocol::handleRFBServerMessage", "socket read error for rfbSetColourMapEntries");
                        return false;
                    }
                    else
                    {
                        msg.scme.firstColour = Swap16IfLE(msg.scme.firstColour);
                        msg.scme.nColours    = Swap16IfLE(msg.scme.nColours);

                        return this->receivedSetColourMapEntries(msg.scme);
                    }
                }
                break;

                case rfbBell:
                {
                    return this->receivedBell(msg.b);
                }
                break;

                case rfbServerCutText:
                {
                    if (!this->readFromRFBServer((char*)&msg + 1, sz_rfbServerCutTextMsg - 1))
                    {
                        if (isOpen) this->errorMessage("RFBProtocol::handleRFBServerMessage", "socket read error for rfbServerCutText");
                        return false;
                    }
                    else
                    {
                        msg.sct.length = Swap32IfLE(msg.sct.length);

                        newServerCutText = false;

                        if (serverCutText)
                        {
                            free((void*)serverCutText);
                            serverCutText = 0;
                        }

                        char* const sct = (char*)malloc(msg.sct.length + 1);
                        if (!sct)
                        {
                            if (isOpen) this->errorMessage("RFBProtocol::handleRFBServerMessage", "memory allocation failed for rfbServerCutText");
                            return false;
                        }
                        else if (!this->readFromRFBServer(sct, msg.sct.length))
                        {
                            if (isOpen) this->errorMessage("RFBProtocol::handleRFBServerMessage", "socket read error for rfbServerCutText");
                            free(sct);
                            return false;
                        }
                        else
                        {
                            sct[msg.sct.length] = 0;

                            serverCutText    = sct;
                            newServerCutText = true;

                            return this->receivedServerCutText(msg.sct);
                        }
                    }
                }
                break;

                default:
                {
                    if (isOpen) this->errorMessage1l("RFBProtocol::handleRFBServerMessage", "unknown server message type", msg.type);
                    return false;
                }
                break;
            }
        }
    }

    return true;
}



bool RFBProtocol::receivedFramebufferUpdate(const rfbFramebufferUpdateMsg& msg)
{
    for (rfbCARD16 i = 0; i < msg.nRects; i++)
    {
        rfbFramebufferUpdateRectHeader rect;
        if (!this->readFromRFBServer(&rect, sz_rfbFramebufferUpdateRectHeader))
        {
            if (isOpen) this->errorMessage("RFBProtocol::receivedFramebufferUpdate", "socket write error");
            return false;
        }
        else
        {
            rect.r.x = Swap16IfLE(rect.r.x);
            rect.r.y = Swap16IfLE(rect.r.y);
            rect.r.w = Swap16IfLE(rect.r.w);
            rect.r.h = Swap16IfLE(rect.r.h);

            rect.encoding = Swap32IfLE(rect.encoding);

            if ( ( (rect.r.x + rect.r.w > framebufferWidth) ||
                   (rect.r.y + rect.r.h > framebufferHeight)   ) &&
                 (rect.encoding != rfbEncodingDesktopSize) )
            {
                if (isOpen) this->errorMessageRect("RFBProtocol::receivedFramebufferUpdate", "rectangle too large", rect.r.x, rect.r.y, rect.r.w, rect.r.h);
                return false;
            }
            else
            {
                if ((rect.r.h * rect.r.w) == 0)
                {
                    if (isOpen) this->errorMessage("RFBProtocol::receivedFramebufferUpdate", "ignoring zero size rectangle");
                    continue;
                }
                else
                {
//fprintf(stderr, "rect.encoding = %ld\n", (long)rect.encoding);//!!!
                    switch (rect.encoding)
                    {
                        case rfbEncodingRaw:
                        {
                            const size_t bytesPerLine = rect.r.w * pixelFormat.bitsPerPixel / 8;

                            size_t linesToRead = DECODE_BUFFER_SIZE / bytesPerLine;
                            while (rect.r.h > 0)
                            {
                                if (linesToRead > rect.r.h)
                                    linesToRead = rect.r.h;

                                if (!this->readFromRFBServer(decodeBuffer, bytesPerLine*linesToRead))
                                {
                                    if (isOpen) this->errorMessage("RFBProtocol::receivedFramebufferUpdate", "socket read error");
                                    return false;
                                }
                                else
                                {
                                    this->copyRectData(decodeBuffer, rect.r.x, rect.r.y, rect.r.w, linesToRead);

                                    rect.r.h -= linesToRead;
                                    rect.r.y += linesToRead;
                                }
                            }
                        }
                        break;

                        case rfbEncodingCopyRect:
                        {
                            rfbCopyRect cr;

                            if (!this->readFromRFBServer(&cr, sz_rfbCopyRect))
                            {
                                if (isOpen) this->errorMessage("RFBProtocol::receivedFramebufferUpdate", "socket read error");
                                return false;
                            }
                            else
                            {
                                cr.srcX = Swap16IfLE(cr.srcX);
                                cr.srcY = Swap16IfLE(cr.srcY);

                                this->copyRect(cr.srcX, cr.srcY, rect.r.x, rect.r.y, rect.r.w, rect.r.h);
                            }

                        }
                        break;

                        case rfbEncodingRRE:
                        {
                            switch (pixelFormat.bitsPerPixel)
                            {
                                case 8:
                                    if (!this->handleRRE8(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                case 16:
                                    if (!this->handleRRE16(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                case 32:
                                    if (!this->handleRRE32(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                default:
                                    if (isOpen) this->errorMessage1l("RFBProtocol::receivedFramebufferUpdate", "unimplemented bits per pixel", pixelFormat.bitsPerPixel);
                                    return false;
                            }
                        }
                        break;

                        case rfbEncodingCoRRE:
                        {
                            switch (pixelFormat.bitsPerPixel)
                            {
                                case 8:
                                    if (!this->handleCoRRE8(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                case 16:
                                    if (!this->handleCoRRE16(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                case 32:
                                    if (!this->handleCoRRE32(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                default:
                                    if (isOpen) this->errorMessage1l("RFBProtocol::receivedFramebufferUpdate", "unimplemented bits per pixel", pixelFormat.bitsPerPixel);
                                    return false;
                            }
                        }
                        break;

                        case rfbEncodingHextile:
                        {
                            switch (pixelFormat.bitsPerPixel)
                            {
                                case 8:
                                    if (!this->handleHextile8(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                case 16:
                                    if (!this->handleHextile16(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                case 32:
                                    if (!this->handleHextile32(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;
                                default:
                                    if (isOpen) this->errorMessage1l("RFBProtocol::receivedFramebufferUpdate", "unimplemented bits per pixel", pixelFormat.bitsPerPixel);
                                    return false;
                            }
                        }
                        break;

                        case rfbEncodingZRLE:
                        {
                            switch (pixelFormat.bitsPerPixel)
                            {
                                case 8:
                                    if (!this->handleZRLE8(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;

                                case 16:
                                    if (!this->handleZRLE16(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                        return false;
                                    break;

                                case 32:
                                {
                                    const bool fitsInLS3Bytes = ( (pixelFormat.redMax   << pixelFormat.redShift)   < (1<<24) &&
                                                                  (pixelFormat.greenMax << pixelFormat.greenShift) < (1<<24) &&
                                                                  (pixelFormat.blueMax  << pixelFormat.blueShift)  < (1<<24)    );

                                    const bool fitsInMS3Bytes = ( (pixelFormat.redShift   > 7) &&
                                                                  (pixelFormat.greenShift > 7) &&
                                                                  (pixelFormat.blueShift  > 7)    );

                                    if ( (fitsInLS3Bytes && !pixelFormat.bigEndian) ||
                                         (fitsInMS3Bytes &&  pixelFormat.bigEndian)    )
                                    {
                                        if (!this->handleZRLE24A(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                            return false;
                                    }
                                    else if ((fitsInLS3Bytes &&  pixelFormat.bigEndian) ||
                                             (fitsInMS3Bytes && !pixelFormat.bigEndian)    )
                                    {
                                        if (!this->handleZRLE24B(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                            return false;
                                    }
                                    else
                                    {
                                        if (!this->handleZRLE32(rect.r.x, rect.r.y, rect.r.w, rect.r.h))
                                            return false;
                                    }
                                }
                                break;

                                default:
                                    if (isOpen) this->errorMessage1l("RFBProtocol::receivedFramebufferUpdate", "unimplemented bits per pixel", pixelFormat.bitsPerPixel);
                                    return false;
                            }
                        }
                        break;

                        case rfbEncodingDesktopSize:
                        {
                            framebufferWidth  = rect.r.w;
                            framebufferHeight = rect.r.h;
                            this->infoDesktopSizeReceived(framebufferWidth, framebufferHeight);
                        }
                        break;

                        default:
                        {
                            if (isOpen) this->errorMessage1l("RFBProtocol::receivedFramebufferUpdate", "unknown rectangle encoding", rect.encoding);
                            return false;
                        }
                    }
                }
            }
        }
    }

    return this->requestNewUpdate();
}



void RFBProtocol::errorMessage(const char* where, const char* message) const
{
    // default implementation does nothing...
}



void RFBProtocol::errorMessageForSend(const char* where, const char* message) const
{
    this->errorMessage(where, message);
}



static const size_t MaxLongDisplayLen = 64;  // plenty of room for a long...



void RFBProtocol::errorMessage1l(const char* where, const char* message, long lParam1) const
{
    static const size_t      MaxLongDisplayLen = 64;         // plenty of room for a long...
    static const char* const fmt               = "%s: %ld";  // 2 extra characters (": ") added by fmt
    static const size_t      fmtExtraChars     = 2;

    char* const buf = (char*)malloc(strlen(message) + fmtExtraChars + MaxLongDisplayLen + 1);
    if (!buf)
    {
        if (isOpen) this->errorMessage(where, message);  // fallback for memory allocation error
    }
    else
    {
        sprintf(buf, fmt, message, lParam1);
        if (isOpen) this->errorMessage(where, buf);

        free(buf);
    }
}



void RFBProtocol::errorMessageRect(const char* where, const char* message, long x, long y, long w, long h) const
{
    static const char* const fmt               = "%s: (%ld, %ld) %ld x %ld";  // 10 extra characters (": (" & ", " & ") " & " x ") added by fmt
    static const size_t      fmtExtraChars     = 10;

    char* const buf = (char*)malloc(strlen(message) + fmtExtraChars + 4*MaxLongDisplayLen + 1);
    if (!buf)
    {
        if (isOpen) this->errorMessage(where, message);  // fallback for memory allocation error
    }
    else
    {
        sprintf(buf, fmt, message, x, y, w, h);
        if (isOpen) this->errorMessage(where, buf);

        free(buf);
    }
}



void RFBProtocol::errorMessageFromServer(const char* where, const char* message) const
{
    this->errorMessage(where, message);
}



void RFBProtocol::infoServerInitStarted() const
{
    // default implementation does nothing...
}



void RFBProtocol::infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion) const
{
    // default implementation does nothing...
}



void RFBProtocol::infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult) const
{
    // default implementation does nothing...
}



void RFBProtocol::infoServerInitCompleted(bool succeeded) const
{
    // default implementation does nothing...
}



void RFBProtocol::infoDesktopSizeReceived(rfbCARD16 newWidth, rfbCARD16 newHeight) const
{
    // default implementation does nothing...
}



void RFBProtocol::infoCloseStarted() const
{
    // default implementation does nothing...
}



void RFBProtocol::infoCloseCompleted() const
{
    // default implementation does nothing...
}



bool RFBProtocol::encryptChallenge(const unsigned char* challenge,
                                   size_t               challengeSize,
                                   const unsigned char* passwd,
                                   unsigned char*       response,
                                   size_t               maxResponseSize)
{
    if (challengeSize > maxResponseSize)
        challengeSize = maxResponseSize;

    if (!passwd)
        passwd = (const unsigned char*)"";

    const size_t passwdLen = strlen((const char*)passwd);

    // key is simply password padded with nulls
    const size_t KeySize = 8;
    unsigned char key[KeySize];
    memset(key, 0, sizeof(key));
    strncpy((char*)key, (const char*)passwd, ((passwdLen > KeySize) ? KeySize : passwdLen));

    // Note: if challengeSize is not a multiple of KeySize, then only
    // the first full-KeySize blocks of challenge are encrypted.
    deskey(key, EN0);
    for (size_t i = 0; i+KeySize <= challengeSize; i += KeySize)
        des(challenge+i, response+i);

    return true;
}



rfbCARD32 RFBProtocol::doMapColor(rfbCARD32 color)
{
    return color;
}



bool RFBProtocol::requestNewUpdate()
{
    updateDefinitelyExpected = false;

    if (updateNeeded)
    {
        if (!this->checkUpdateNeeded())
            return false;
    }
    else
    {
        if (!this->sendFramebufferUpdateRequest(0, 0, framebufferWidth, framebufferHeight, true))
            return false;
    }

    return true;
}



bool RFBProtocol::checkUpdateNeeded()
{
    if (updateNeeded && !updateDefinitelyExpected)
    {
        if (!this->sendFramebufferUpdateRequest(updateNeededX1,
                                                updateNeededY1,
                                                (size_t)updateNeededX2-updateNeededX1,
                                                (size_t)updateNeededY2-updateNeededY1,
                                                false))
        {
            return false;
        }

        updateDefinitelyExpected = true;
        updateNeeded             = false;
    }

    return true;
}



void RFBProtocol::performUpdateNeeded(int x, int y, size_t w, size_t h)
{
    if (!updateNeeded)
    {
        updateNeededX1 = x;
        updateNeededY1 = y;
        updateNeededX2 = x+w;
        updateNeededY2 = y+h;
        updateNeeded   = true;
    }
    else
    {
        if (x        < updateNeededX1) updateNeededX1 = x;
        if (y        < updateNeededY1) updateNeededY1 = y;
        if (x+(int)w > updateNeededX2) updateNeededX2 = x+w;
        if (y+(int)h > updateNeededY2) updateNeededY2 = y+h;
    }
}



bool RFBProtocol::checkAvailableFromRFBServer(size_t n)
{
    if (n > COMM_BUFFER_SIZE)
        return false;

    if (commBufferPos > 0)
    {
        memmove(commBuffer, commBuffer+commBufferPos, commBufferFill-commBufferPos);

        commBufferFill -= commBufferPos;
        commBufferPos = 0;
    }

    bool lastReadReturnedZeroBytes = false;

    while (n > commBufferFill)
    {
        const int ne = read(sock, commBuffer+commBufferFill, COMM_BUFFER_SIZE-commBufferFill);

        if (!isOpen || (ne < 0))
        {
            return false;
        }
        else if (ne == 0)
        {
            if (lastReadReturnedZeroBytes)
                usleep(1000);

            lastReadReturnedZeroBytes = true;
        }
        else
        {
            commBufferFill += (size_t)ne;

            lastReadReturnedZeroBytes = false;
        }
    }

    return true;
}



bool RFBProtocol::readFromRFBServer(void* buf, size_t n)
{
    if (!isOpen)
        return false;  // in case this object was closed while we were waiting for input

    while (n > 0)
    {
        size_t avail = commBufferFill - commBufferPos;

        if (avail <= 0)
        {
            if (!this->checkAvailableFromRFBServer((n > COMM_BUFFER_SIZE) ? COMM_BUFFER_SIZE : n))
                return false;
            else
                avail = commBufferFill - commBufferPos;
        }

        const size_t nr = (avail > n) ? n : avail;

        memcpy(buf, commBuffer+commBufferPos, nr);

        buf           =  (char*)buf + nr;
        commBufferPos += nr;
        n             -= nr;
    }

    return isOpen;
}



bool RFBProtocol::writeToRFBServer(const void* buf, size_t n)
{
    if (!isOpen)
        return false;  // in case this object was closed while we were waiting for input

    while (n > 0)
    {
        const int ne = write(sock, buf, n);
        if (!isOpen || (ne < 0))
            return false;

        buf =  (char*)buf + ne;
        n   -= ne;
    }

    return isOpen;
}



//----------------------------------------------------------------------
// Instantiate handle* methods

// CONCAT2 concatenates its two arguments.
// CONCAT2E does the same but also expands its arguments if they are macros.
#define CONCAT2(a,b) a##b
#define CONCAT2E(a,b) CONCAT2(a,b)



#define GET_PIXEL8(pix, ptr)   do { ((pix) = *(ptr)++); } while (0)

#define GET_PIXEL16(pix, ptr)  do { ((rfbCARD8*)&(pix))[0] = *(ptr)++; \
                                    ((rfbCARD8*)&(pix))[1] = *(ptr)++; } while (0)

#define GET_PIXEL32(pix, ptr)  do { ((rfbCARD8*)&(pix))[0] = *(ptr)++; \
                                    ((rfbCARD8*)&(pix))[1] = *(ptr)++; \
                                    ((rfbCARD8*)&(pix))[2] = *(ptr)++; \
                                    ((rfbCARD8*)&(pix))[3] = *(ptr)++; } while (0)
#define BPP 8
#include "rre.cppinc"
#include "corre.cppinc"
#include "hextile.cppinc"
#undef BPP
#define BPP 16
#include "rre.cppinc"
#include "corre.cppinc"
#include "hextile.cppinc"
#undef BPP
#define BPP 32
#include "rre.cppinc"
#include "corre.cppinc"
#include "hextile.cppinc"
#undef BPP

#undef GET_PIXEL32
#undef GET_PIXEL18
#undef GET_PIXEL8



#define BPP 8
#include "zrle.cppinc"
#undef BPP

#define BPP 16
#include "zrle.cppinc"
#undef BPP

#define BPP 32
#include "zrle.cppinc"
#define CPIXEL 24A
#include "zrle.cppinc"
#undef CPIXEL
#define CPIXEL 24B
#include "zrle.cppinc"
#undef CPIXEL
#undef BPP



#undef CONCAT2E
#undef CONCAT2
