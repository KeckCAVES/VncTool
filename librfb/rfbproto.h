/*
 *  Copyright (C) 2002 RealVNC Ltd.  All Rights Reserved.
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
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

/*
 * rfbproto.h - header file for the RFB protocol version 3.3
 *
 * Uses types rfbCARD<n> for an n-bit unsigned integer, INT<n> for an n-bit signed
 * integer (for n = 8, 16 and 32).
 *
 * All multiple byte integers are in big endian (network) order (most
 * significant byte first).  Unless noted otherwise there is no special
 * alignment of protocol structures.
 *
 *
 * Once the initial handshaking is done, all messages start with a type byte,
 * (usually) followed by message-specific data.  The order of definitions in
 * this file is as follows:
 *
 *  (1) Structures used in several types of message.
 *  (2) Structures used in the initial handshaking.
 *  (3) Message types.
 *  (4) Encoding types.
 *  (5) For each message type, the form of the data following the type byte.
 *      Sometimes this is defined by a single structure but the more complex
 *      messages have to be explained by comments.
 */

#ifndef __RFBPROTO_H_INCLUDED__
#define __RFBPROTO_H_INCLUDED__

#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <zlib.h>
#include <X11/Xmd.h>  // for CARD* definitions

#ifdef __cplusplus
extern "C" {
#endif

typedef CARD8  rfbCARD8;
typedef CARD16 rfbCARD16;
typedef CARD32 rfbCARD32;



/*****************************************************************************
 *
 * Structures used in several messages
 *
 *****************************************************************************/

/*-----------------------------------------------------------------------------
 * Structure used to specify a rectangle.  This structure is a multiple of 4
 * bytes so that it can be interspersed with 32-bit pixel data without
 * affecting alignment.
 */

typedef struct {
    rfbCARD16 x;
    rfbCARD16 y;
    rfbCARD16 w;
    rfbCARD16 h;
} rfbRectangle;

#define sz_rfbRectangle 8


/*-----------------------------------------------------------------------------
 * Structure used to specify pixel format.
 */

typedef struct {

    rfbCARD8 bitsPerPixel;		/* 8,16,32 only */

    rfbCARD8 depth;		/* 8 to 32 */

    rfbCARD8 bigEndian;		/* True if multi-byte pixels are interpreted
				   as big endian, or if single-bit-per-pixel
				   has most significant bit of the byte
				   corresponding to first (leftmost) pixel. Of
				   course this is meaningless for 8 bits/pix */

    rfbCARD8 trueColour;		/* If false then we need a "colour map" to
				   convert pixels to RGB.  If true, xxxMax and
				   xxxShift specify bits used for red, green
				   and blue */

    /* the following fields are only meaningful if trueColour is true */

    rfbCARD16 redMax;		/* maximum red value (= 2^n - 1 where n is the
				   number of bits used for red). Note this
				   value is always in big endian order. */

    rfbCARD16 greenMax;		/* similar for green */

    rfbCARD16 blueMax;		/* and blue */

    rfbCARD8 redShift;		/* number of shifts needed to get the red
				   value in a pixel to the least significant
				   bit. To find the red value from a given
				   pixel, do the following:
				   1) Swap pixel value according to bigEndian
				      (e.g. if bigEndian is false and host byte
				      order is big endian, then swap).
				   2) Shift right by redShift.
				   3) AND with redMax (in host byte order).
				   4) You now have the red value between 0 and
				      redMax. */

    rfbCARD8 greenShift;		/* similar for green */

    rfbCARD8 blueShift;		/* and blue */

    rfbCARD8 pad1;
    rfbCARD16 pad2;

} rfbPixelFormat;

#define sz_rfbPixelFormat 16



/*****************************************************************************
 *
 * Initial handshaking messages
 *
 *****************************************************************************/

/*-----------------------------------------------------------------------------
 * Protocol Version
 *
 * The server always sends 12 bytes to start which identifies the latest RFB
 * protocol version number which it supports.  These bytes are interpreted
 * as a string of 12 ASCII characters in the format "RFB xxx.yyy\n" where
 * xxx and yyy are the major and minor version numbers (for version 3.3
 * this is "RFB 003.003\n").
 *
 * The client then replies with a similar 12-byte message giving the version
 * number of the protocol which should actually be used (which may be different
 * to that quoted by the server).
 *
 * It is intended that both clients and servers may provide some level of
 * backwards compatibility by this mechanism.  Servers in particular should
 * attempt to provide backwards compatibility, and even forwards compatibility
 * to some extent.  For example if a client demands version 3.1 of the
 * protocol, a 3.0 server can probably assume that by ignoring requests for
 * encoding types it doesn't understand, everything will still work OK.  This
 * will probably not be the case for changes in the major version number.
 *
 * The format string below can be used in sprintf or sscanf to generate or
 * decode the version string respectively.
 */

#define rfbProtocolVersionFormat "RFB %03d.%03d\n"
#define rfbProtocolMajorVersion 3
#define rfbProtocolMinorVersion 3

typedef char rfbProtocolVersionMsg[13];	/* allow extra byte for null */

#define sz_rfbProtocolVersionMsg 12


/*-----------------------------------------------------------------------------
 * Authentication
 *
 * Once the protocol version has been decided, the server then sends a 32-bit
 * word indicating whether any authentication is needed on the connection.
 * The value of this word determines the authentication scheme in use.  For
 * version 3.0 of the protocol this may have one of the following values:
 */

#define rfbConnFailed 0
#define rfbNoAuth 1
#define rfbVncAuth 2

/*
 * rfbConnFailed:	For some reason the connection failed (e.g. the server
 *			cannot support the desired protocol version).  This is
 *			followed by a string describing the reason (where a
 *			string is specified as a 32-bit length followed by that
 *			many ASCII characters).
 *
 * rfbNoAuth:		No authentication is needed.
 *
 * rfbVncAuth:		The VNC authentication scheme is to be used.  A 16-byte
 *			challenge follows, which the client encrypts as
 *			appropriate using the password and sends the resulting
 *			16-byte response.  If the response is correct, the
 *			server sends the 32-bit word rfbVncAuthOK.  If a simple
 *			failure happens, the server sends rfbVncAuthFailed and
 *			closes the connection. If the server decides that too
 *			many failures have occurred, it sends rfbVncAuthTooMany
 *			and closes the connection.  In the latter case, the
 *			server should not allow an immediate reconnection by
 *			the client.
 */

#define rfbVncAuthOK 0
#define rfbVncAuthFailed 1
#define rfbVncAuthTooMany 2


/*-----------------------------------------------------------------------------
 * Client Initialisation Message
 *
 * Once the client and server are sure that they're happy to talk to one
 * another, the client sends an initialisation message.  At present this
 * message only consists of a boolean indicating whether the server should try
 * to share the desktop by leaving other clients connected, or give exclusive
 * access to this client by disconnecting all other clients.
 */

typedef struct {
    rfbCARD8 shared;
} rfbClientInitMsg;

#define sz_rfbClientInitMsg 1


/*-----------------------------------------------------------------------------
 * Server Initialisation Message
 *
 * After the client initialisation message, the server sends one of its own.
 * This tells the client the width and height of the server's framebuffer,
 * its pixel format and the name associated with the desktop.
 */

typedef struct {
    rfbCARD16 framebufferWidth;
    rfbCARD16 framebufferHeight;
    rfbPixelFormat format;	/* the server's preferred pixel format */
    rfbCARD32 nameLength;
    /* followed by char name[nameLength] */
} rfbServerInitMsg;

#define sz_rfbServerInitMsg (8 + sz_rfbPixelFormat)


/*
 * Following the server initialisation message it's up to the client to send
 * whichever protocol messages it wants.  Typically it will send a
 * SetPixelFormat message and a SetEncodings message, followed by a
 * FramebufferUpdateRequest.  From then on the server will send
 * FramebufferUpdate messages in response to the client's
 * FramebufferUpdateRequest messages.  The client should send
 * FramebufferUpdateRequest messages with incremental set to true when it has
 * finished processing one FramebufferUpdate and is ready to process another.
 * With a fast client, the rate at which FramebufferUpdateRequests are sent
 * should be regulated to avoid hogging the network.
 */



/*****************************************************************************
 *
 * Message types
 *
 *****************************************************************************/

/* server -> client */

#define rfbFramebufferUpdate 0
#define rfbSetColourMapEntries 1
#define rfbBell 2
#define rfbServerCutText 3


/* client -> server */

#define rfbSetPixelFormat 0
#define rfbFixColourMapEntries 1	/* not currently supported */
#define rfbSetEncodings 2
#define rfbFramebufferUpdateRequest 3
#define rfbKeyEvent 4
#define rfbPointerEvent 5
#define rfbClientCutText 6




/*****************************************************************************
 *
 * Encoding types
 *
 *****************************************************************************/

#define rfbEncodingRaw 0
#define rfbEncodingCopyRect 1
#define rfbEncodingRRE 2
#define rfbEncodingCoRRE 4
#define rfbEncodingHextile 5
#define rfbEncodingZRLE 16



/*****************************************************************************
 *
 * Server -> client message definitions
 *
 *****************************************************************************/


/*-----------------------------------------------------------------------------
 * FramebufferUpdate - a block of rectangles to be copied to the framebuffer.
 *
 * This message consists of a header giving the number of rectangles of pixel
 * data followed by the rectangles themselves.  The header is padded so that
 * together with the type byte it is an exact multiple of 4 bytes (to help
 * with alignment of 32-bit pixels):
 */

typedef struct {
    rfbCARD8 type;			/* always rfbFramebufferUpdate */
    rfbCARD8 pad;
    rfbCARD16 nRects;
    /* followed by nRects rectangles */
} rfbFramebufferUpdateMsg;

#define sz_rfbFramebufferUpdateMsg 4

/*
 * Each rectangle of pixel data consists of a header describing the position
 * and size of the rectangle and a type word describing the encoding of the
 * pixel data, followed finally by the pixel data.  Note that if the client has
 * not sent a SetEncodings message then it will only receive raw pixel data.
 * Also note again that this structure is a multiple of 4 bytes.
 */

typedef struct {
    rfbRectangle r;
    rfbCARD32 encoding;	/* one of the encoding types rfbEncoding... */
} rfbFramebufferUpdateRectHeader;

#define sz_rfbFramebufferUpdateRectHeader (sz_rfbRectangle + 4)


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * Raw Encoding.  Pixels are sent in top-to-bottom scanline order,
 * left-to-right within a scanline with no padding in between.
 */


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * CopyRect Encoding.  The pixels are specified simply by the x and y position
 * of the source rectangle.
 */

typedef struct {
    rfbCARD16 srcX;
    rfbCARD16 srcY;
} rfbCopyRect;

#define sz_rfbCopyRect 4


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * RRE - Rise-and-Run-length Encoding.  We have an rfbRREHeader structure
 * giving the number of subrectangles following.  Finally the data follows in
 * the form [<bgpixel><subrect><subrect>...] where each <subrect> is
 * [<pixel><rfbRectangle>].
 */

typedef struct {
    rfbCARD32 nSubrects;
} rfbRREHeader;

#define sz_rfbRREHeader 4


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * CoRRE - Compact RRE Encoding.  We have an rfbRREHeader structure giving
 * the number of subrectangles following.  Finally the data follows in the form
 * [<bgpixel><subrect><subrect>...] where each <subrect> is
 * [<pixel><rfbCoRRERectangle>].  This means that
 * the whole rectangle must be at most 255x255 pixels.
 */

typedef struct {
    rfbCARD8 x;
    rfbCARD8 y;
    rfbCARD8 w;
    rfbCARD8 h;
} rfbCoRRERectangle;

#define sz_rfbCoRRERectangle 4


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * Hextile Encoding.  The rectangle is divided up into "tiles" of 16x16 pixels,
 * starting at the top left going in left-to-right, top-to-bottom order.  If
 * the width of the rectangle is not an exact multiple of 16 then the width of
 * the last tile in each row will be correspondingly smaller.  Similarly if the
 * height is not an exact multiple of 16 then the height of each tile in the
 * final row will also be smaller.  Each tile begins with a "subencoding" type
 * byte, which is a mask made up of a number of bits.  If the Raw bit is set
 * then the other bits are irrelevant; w*h pixel values follow (where w and h
 * are the width and height of the tile).  Otherwise the tile is encoded in a
 * similar way to RRE, except that the position and size of each subrectangle
 * can be specified in just two bytes.  The other bits in the mask are as
 * follows:
 *
 * BackgroundSpecified - if set, a pixel value follows which specifies
 *    the background colour for this tile.  The first non-raw tile in a
 *    rectangle must have this bit set.  If this bit isn't set then the
 *    background is the same as the last tile.
 *
 * ForegroundSpecified - if set, a pixel value follows which specifies
 *    the foreground colour to be used for all subrectangles in this tile.
 *    If this bit is set then the SubrectsColoured bit must be zero.
 *
 * AnySubrects - if set, a single byte follows giving the number of
 *    subrectangles following.  If not set, there are no subrectangles (i.e.
 *    the whole tile is just solid background colour).
 *
 * SubrectsColoured - if set then each subrectangle is preceded by a pixel
 *    value giving the colour of that subrectangle.  If not set, all
 *    subrectangles are the same colour, the foreground colour;  if the
 *    ForegroundSpecified bit wasn't set then the foreground is the same as
 *    the last tile.
 *
 * The position and size of each subrectangle is specified in two bytes.  The
 * Pack macros below can be used to generate the two bytes from x, y, w, h,
 * and the Extract macros can be used to extract the x, y, w, h values from
 * the two bytes.
 */

#define rfbHextileRaw			(1 << 0)
#define rfbHextileBackgroundSpecified	(1 << 1)
#define rfbHextileForegroundSpecified	(1 << 2)
#define rfbHextileAnySubrects		(1 << 3)
#define rfbHextileSubrectsColoured	(1 << 4)

#define rfbHextilePackXY(x,y) (((x) << 4) | (y))
#define rfbHextilePackWH(w,h) ((((w)-1) << 4) | ((h)-1))
#define rfbHextileExtractX(byte) ((byte) >> 4)
#define rfbHextileExtractY(byte) ((byte) & 0xf)
#define rfbHextileExtractW(byte) (((byte) >> 4) + 1)
#define rfbHextileExtractH(byte) (((byte) & 0xf) + 1)


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * ZRLE - encoding combining Zlib compression, tiling, palettisation and
 * run-length encoding.
 */

typedef struct {
    rfbCARD32 length;
} rfbZRLEHeader;

#define sz_rfbZRLEHeader 4

#define rfbZRLETileWidth  64
#define rfbZRLETileHeight 64


/*-----------------------------------------------------------------------------
 * SetColourMapEntries - these messages are only sent if the pixel
 * format uses a "colour map" (i.e. trueColour false) and the client has not
 * fixed the entire colour map using FixColourMapEntries.  In addition they
 * will only start being sent after the client has sent its first
 * FramebufferUpdateRequest.  So if the client always tells the server to use
 * trueColour then it never needs to process this type of message.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbSetColourMapEntries */
    rfbCARD8 pad;
    rfbCARD16 firstColour;
    rfbCARD16 nColours;

    /* Followed by nColours * 3 * rfbCARD16
       r1, g1, b1, r2, g2, b2, r3, g3, b3, ..., rn, bn, gn */

} rfbSetColourMapEntriesMsg;

#define sz_rfbSetColourMapEntriesMsg 6



/*-----------------------------------------------------------------------------
 * Bell - ring a bell on the client if it has one.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbBell */
} rfbBellMsg;

#define sz_rfbBellMsg 1



/*-----------------------------------------------------------------------------
 * ServerCutText - the server has new text in its cut buffer.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbServerCutText */
    rfbCARD8 pad1;
    rfbCARD16 pad2;
    rfbCARD32 length;
    /* followed by char text[length] */
} rfbServerCutTextMsg;

#define sz_rfbServerCutTextMsg 8


/*-----------------------------------------------------------------------------
 * Union of all server->client messages.
 */

typedef union {
    rfbCARD8 type;
    rfbFramebufferUpdateMsg fu;
    rfbSetColourMapEntriesMsg scme;
    rfbBellMsg b;
    rfbServerCutTextMsg sct;
} rfbServerToClientMsg;



/*****************************************************************************
 *
 * Message definitions (client -> server)
 *
 *****************************************************************************/


/*-----------------------------------------------------------------------------
 * SetPixelFormat - tell the RFB server the format in which the client wants
 * pixels sent.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbSetPixelFormat */
    rfbCARD8 pad1;
    rfbCARD16 pad2;
    rfbPixelFormat format;
} rfbSetPixelFormatMsg;

#define sz_rfbSetPixelFormatMsg (sz_rfbPixelFormat + 4)


/*-----------------------------------------------------------------------------
 * FixColourMapEntries - when the pixel format uses a "colour map", fix
 * read-only colour map entries.
 *
 *    ***************** NOT CURRENTLY SUPPORTED *****************
 */

typedef struct {
    rfbCARD8 type;			/* always rfbFixColourMapEntries */
    rfbCARD8 pad;
    rfbCARD16 firstColour;
    rfbCARD16 nColours;

    /* Followed by nColours * 3 * rfbCARD16
       r1, g1, b1, r2, g2, b2, r3, g3, b3, ..., rn, bn, gn */

} rfbFixColourMapEntriesMsg;

#define sz_rfbFixColourMapEntriesMsg 6


/*-----------------------------------------------------------------------------
 * SetEncodings - tell the RFB server which encoding types we accept.  Put them
 * in order of preference, if we have any.  We may always receive raw
 * encoding, even if we don't specify it here.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbSetEncodings */
    rfbCARD8 pad;
    rfbCARD16 nEncodings;
    /* followed by nEncodings * rfbCARD32 encoding types */
} rfbSetEncodingsMsg;

#define sz_rfbSetEncodingsMsg 4


/*-----------------------------------------------------------------------------
 * FramebufferUpdateRequest - request for a framebuffer update.  If incremental
 * is true then the client just wants the changes since the last update.  If
 * false then it wants the whole of the specified rectangle.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbFramebufferUpdateRequest */
    rfbCARD8 incremental;
    rfbCARD16 x;
    rfbCARD16 y;
    rfbCARD16 w;
    rfbCARD16 h;
} rfbFramebufferUpdateRequestMsg;

#define sz_rfbFramebufferUpdateRequestMsg 10


/*-----------------------------------------------------------------------------
 * KeyEvent - key press or release
 *
 * Keys are specified using the "keysym" values defined by the X Window System.
 * For most ordinary keys, the keysym is the same as the corresponding ASCII
 * value.  Other common keys are:
 *
 * BackSpace		0xff08
 * Tab			0xff09
 * Return or Enter	0xff0d
 * Escape		0xff1b
 * Insert		0xff63
 * Delete		0xffff
 * Home			0xff50
 * End			0xff57
 * Page Up		0xff55
 * Page Down		0xff56
 * Left			0xff51
 * Up			0xff52
 * Right		0xff53
 * Down			0xff54
 * F1			0xffbe
 * F2			0xffbf
 * ...			...
 * F12			0xffc9
 * Shift		0xffe1
 * Control		0xffe3
 * Meta			0xffe7
 * Alt			0xffe9
 */

typedef struct {
    rfbCARD8 type;			/* always rfbKeyEvent */
    rfbCARD8 down;			/* true if down (press), false if up */
    rfbCARD16 pad;
    rfbCARD32 key;			/* key is specified as an X keysym */
} rfbKeyEventMsg;

#define sz_rfbKeyEventMsg 8


/*-----------------------------------------------------------------------------
 * PointerEvent - mouse/pen move and/or button press.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbPointerEvent */
    rfbCARD8 buttonMask;		/* bits 0-7 are buttons 1-8, 0=up, 1=down */
    rfbCARD16 x;
    rfbCARD16 y;
} rfbPointerEventMsg;

#define rfbButton1Mask 1
#define rfbButton2Mask 2
#define rfbButton3Mask 4
#define rfbButton4Mask 8
#define rfbButton5Mask 16
#define rfbWheelUpMask rfbButton4Mask
#define rfbWheelDownMask rfbButton5Mask

#define sz_rfbPointerEventMsg 6



/*-----------------------------------------------------------------------------
 * ClientCutText - the client has new text in its cut buffer.
 */

typedef struct {
    rfbCARD8 type;			/* always rfbClientCutText */
    rfbCARD8 pad1;
    rfbCARD16 pad2;
    rfbCARD32 length;
    /* followed by char text[length] */
} rfbClientCutTextMsg;

#define sz_rfbClientCutTextMsg 8



/*-----------------------------------------------------------------------------
 * Union of all client->server messages.
 */

typedef union {
    rfbCARD8 type;
    rfbSetPixelFormatMsg spf;
    rfbFixColourMapEntriesMsg fcme;
    rfbSetEncodingsMsg se;
    rfbFramebufferUpdateRequestMsg fur;
    rfbKeyEventMsg ke;
    rfbPointerEventMsg pe;
    rfbClientCutTextMsg cct;
} rfbClientToServerMsg;



/*-----------------------------------------------------------------------------
 * Equality test for rfbPixelFormat structs
 */

#define rfbPixelFormat_EQ(x, y)                                        \
    ( ((x).bitsPerPixel == (y).bitsPerPixel)                        && \
      ((x).depth        == (y).depth)                               && \
      (((x).bigEndian == (y).bigEndian) || ((x).bitsPerPixel == 8)) && \
      ((x).trueColour == (y).trueColour)                            && \
      (!(x).trueColour || ( ((x).redMax     == (y).redMax)     &&      \
                            ((x).greenMax   == (y).greenMax)   &&      \
                            ((x).blueMax    == (y).blueMax)    &&      \
                            ((x).redShift   == (y).redShift)   &&      \
                            ((x).greenShift == (y).greenShift) &&      \
                            ((x).blueShift  == (y).blueShift))    )    )



/*-----------------------------------------------------------------------------
 * C++ implementation
 */

#ifdef __cplusplus
}  // end of extern "C" block



namespace rfb
{
    class RFBProtocol
    {
    protected:
        friend class ZlibDecompressor;

        class ZlibDecompressor
        {
        friend class RFBProtocol;
        protected:
            RFBProtocol* outer;  // set by outer RFBProtocol instance

        public:
            ZlibDecompressor();
            virtual ~ZlibDecompressor();

            virtual bool init();
            virtual void close();

        public:
            virtual void reset();
            virtual int  pos();

            int fill(size_t itemSize, unsigned nItems = 1)
            {
                if ((ptr + itemSize*nItems) > end)
                {
                    if ((ptr + itemSize) > end)
                        return overrun(itemSize, nItems);

                    nItems = (end - ptr) / itemSize;
                }

                return nItems;
            }

            int skip(unsigned bytes)
            {
                for (unsigned b = bytes; b > 0; )
                {
                    const int n = fill(1, b);
                    if (n < 0)
                        return -1;

                    ptr += n;
                    b   -= n;
                }

                return bytes;
            }

        protected:
            virtual int overrun(size_t itemSize, unsigned nItems);

        public:
            bool readrfbCARD8(rfbCARD8* pv) { if (!fill(1)) return false; *pv = *ptr++; return true; }

            // readOpaque*() reads a quantity without byte-swapping.
            bool readOpaque8(rfbCARD8* pv)    { return readrfbCARD8(pv); }
            bool readOpaque16(rfbCARD16* pv)  { if (!fill(2)) return false; *pv = 0; ((rfbCARD8*)pv)[0] = *ptr++; ((rfbCARD8*)pv)[1] = *ptr++; return true; }
            bool readOpaque24A(rfbCARD32* pv) { if (!fill(3)) return false; *pv = 0; ((rfbCARD8*)pv)[0] = *ptr++; ((rfbCARD8*)pv)[1] = *ptr++; ((rfbCARD8*)pv)[2] = *ptr++; return true; }
            bool readOpaque24B(rfbCARD32* pv) { if (!fill(3)) return false; *pv = 0; ((rfbCARD8*)pv)[1] = *ptr++; ((rfbCARD8*)pv)[2] = *ptr++; ((rfbCARD8*)pv)[3] = *ptr++; return true; }
            bool readOpaque32(rfbCARD32* pv)  { if (!fill(4)) return false; *pv = 0; ((rfbCARD8*)pv)[0] = *ptr++; ((rfbCARD8*)pv)[1] = *ptr++; ((rfbCARD8*)pv)[2] = *ptr++; ((rfbCARD8*)pv)[3] = *ptr++; return true; }

            virtual bool readBytes(void* data, int length);

        public:
            virtual bool            getIsOpen() const         { return isOpen; }
            virtual const rfbCARD8* getPtr()    const         { return ptr; }
            virtual const rfbCARD8* getEnd()    const         { return end; }
            virtual void            setPtr(const rfbCARD8* p) { ptr = p; }

        protected:
            virtual bool decompress();

        protected:
            bool            isOpen;
            rfbCARD8*       start;
            const rfbCARD8* end;
            const rfbCARD8* ptr;
            int             offset;
            unsigned        bytesIn;
            z_stream_s      zs;

        public:
            enum { BUFFER_SIZE = 16384 };

        protected:
            rfbCARD8 buffer[BUFFER_SIZE];

        private:
            // Disable these copiers:
            ZlibDecompressor& operator=(const ZlibDecompressor&) { return *this; }
            ZlibDecompressor(const ZlibDecompressor&) {}
        };


    public:
        RFBProtocol();
        virtual ~RFBProtocol();

    public:
        bool                      getIsOpen()             const { return isOpen; }
        bool                      getIsSameMachine()      const { return isSameMachine; }
        const char*               getDesktopHost()        const { return desktopHost; }  // 0 if connected by listening
        const struct sockaddr_in& getDesktopIPAddress()   const { return desktopIPAddress; }
        unsigned                  getRfbPort()            const { return rfbPort; }
        unsigned                  getPortOffset()         const { return portOffset; }
        unsigned                  getTcpPort()            const { return (rfbPort + portOffset); }
        const rfbPixelFormat&     getPixelFormat()        const { return pixelFormat; }
        bool                      getShouldMapColor()     const { return shouldMapColor; }
        const char*               getRequestedEncodings() const { return requestedEncodings; }
        const char*               getDesktopName()        const { return desktopName; }

        bool                      getSockConnected()      const { return sockConnected; }
        const rfbServerInitMsg&   getServerInitMsg()      const { return si; }
        rfbCARD8                  getCurrentEncoding()    const { return currentEncoding; }
        bool                      getIsBigEndian()        const { return isBigEndian; }


    public:
        virtual bool initViaConnect(const char*           theDesktopHost,           // copied
                                    unsigned              theRfbPort,
                                    const rfbPixelFormat& theRequestedPixelFormat,  // copied
                                    const char*           theRequestedEncodings,    // copied
                                    bool                  theSharedDesktopFlag);    // fails if isOpen

        virtual bool initViaListen(unsigned              theRfbPort,
                                   const rfbPixelFormat& theRequestedPixelFormat,   // copied
                                   const char*           theRequestedEncodings,     // copied
                                   bool                  theSharedDesktopFlag);     // fails if isOpen

        virtual void close();  // to be safe, call close() before this object is destroyed

    protected:
        virtual bool finishInit(bool theSharedDesktopFlag);  // called by initViaConnect() and initViaListen()

    public:
        // After successful initialization, call the send* functions as necessary and
        // then loop on calls to handleRFBServerMessage() until false is returned.
        //
        // The send* functions may safely be called from a different thread than
        // the thread on which the rest of this object is controlled.  They only
        // call writeToRFBServer(), errorMessageForSend() and (in the case of
        // sendSetEncodings()), scanEncodingsString().

        virtual bool sendSetPixelFormat();  // sends pixelFormat to server
        virtual bool sendSetEncodings();    // sends requestedEncodings to server
        virtual bool sendFramebufferUpdateRequest(int x, int y, size_t w, size_t h, bool incremental);
        virtual bool sendKeyEvent(rfbCARD32 key, bool down);
        virtual bool sendPointerEvent(int x, int y, int buttonMask);
        virtual bool sendClientCutText(const char* str, size_t len);

    public:
        // sendStringViaKeyEvents() sends the given string as a series of KeySyms via sendKeyEvent().
        // With the following exceptions, a character's KeySym is its ASCII value.
        // If tabKeySym is not 0, the tab character ('\t') is send as tabKeySym  If enterKeySym
        // is not 0, the newline character ('\n') is sent as enterKeySym, and, if leftControlKeySym
        // is not 0, other control characters are as sent as their corresponding "capital" letter
        // with the leftControlKeySym held down.
        virtual bool sendStringViaKeyEvents( const char* str,
                                             size_t      len,
                                             rfbCARD32   tabKeySym         = 0xff09,
                                             rfbCARD32   enterKeySym       = 0xff0d,
                                             rfbCARD32   leftControlKeySym = 0xffe3 );

        bool sendStringViaKeyEvents( const char* str,
                                     rfbCARD32   tabKeySym         = 0xff09,
                                     rfbCARD32   enterKeySym       = 0xff0d,
                                     rfbCARD32   leftControlKeySym = 0xffe3)
        {
            return !str || sendStringViaKeyEvents(str, (size_t)::strlen(str), tabKeySym, enterKeySym, leftControlKeySym);
        }

    public:
        // Once this object is initialized and configured via the send* functions above,
        // call handleRFBServerMessage() repeatedly until it returns false.  This will
        // listen for and dispatch the messages from the server.
        virtual bool handleRFBServerMessage();

    protected:
        // The following methods are called when server to client messages are received.
        // The default implementation of receivedFramebufferUpdate() uses copyRectData(),
        // copyRect() and fillRect() to handle framebuffer updates from the server.
        // receivedSetColourMapEntries() must set up your color map if you are using one.
        virtual bool receivedFramebufferUpdate(const rfbFramebufferUpdateMsg& msg);  // default implementation calls copyRectData(), copyRect() and fillRect() below
        virtual bool receivedSetColourMapEntries(const rfbSetColourMapEntriesMsg& msg)    = 0;
        virtual bool receivedBell(const rfbBellMsg& msg)                                  = 0;
        virtual bool receivedServerCutText(const rfbServerCutTextMsg& msg)                = 0;

    protected:
        // The implementation of the following methods controls how the bitmaps from the
        // server are displayed on the client when the default implementation of
        // receivedFramebufferUpdate() is used.
        //
        // Note: the bitmap data passed to copyRectData() is row-major ordered with
        // x increasing to the left, y increasing downward, and (x, y) at the upper-left
        // corner of the bitmap.
        virtual void copyRectData(void* data, int x, int y, size_t w, size_t h)           = 0;
        virtual void copyRect(int fromX, int fromY, int toX, int toY, size_t w, size_t h) = 0;
        virtual void fillRect(rfbCARD32 color, int x, int y, size_t w, size_t h)          = 0;  // color is sent in rfbCARD32 value regardless of actual size/format

    protected:
        // The default implementation routes all error messages through errorMessage().
        // If your display environment is vulnerable to malicious strings (e.g., javascript
        // in a Web browser), then you should override errorMessageFromServer() to carefully
        // handle its message.
        virtual void errorMessage(const char* where, const char* message) const;  // default implementation does nothing...

        // The default implementation of errorMessageForSend() performs:
        //
        //     errorMessage(where, message);
        //
        // errorMessageForSend() is called by the send* functions.
        // This is provided to separate the error reports in case
        // the send* functions are called from a different thread
        // and your implementation cannot handle the error messages
        // coming from a different thread.  If this is the case,
        // override errorMessageForSend() to forward the message
        // to the other thread.
        virtual void errorMessageForSend(const char* where, const char* message) const;

        // The default implementation of errorMessage1l() performs:
        //
        //     sprintf(buf, "%s: %ld", message, i);
        //     errorMessage(where, buf)
        //
        // on an internally allocated buffer "buf".
        virtual void errorMessage1l(const char* where, const char* message, long lParam1) const;

        // The default implementation of errorMessageRect() performs:
        //
        //     sprintf(buf, "%s: (%ld, %ld) %ld x %ld", message, x, y, w, h);
        //     errorMessage(where, buf)
        //
        // on an internally allocated buffer "buf".
        virtual void errorMessageRect(const char* where, const char* message, long x, long y, long w, long h) const;

        // errorMessageFromServer() function is separated from the normal errorMessage()
        // because the "message" string comes from the server.  If you are displaying
        // in some environment (e.g., a Web browser) that could be vulnerable to
        // code injection (e.g., javascript), then take care when displaying "message"
        // in a safe manner.
        virtual void errorMessageFromServer(const char* where, const char* message) const;  // default implementation calls errorMessage(where, message);

        // The default implementations of the following info* methods do nothing

        virtual void infoServerInitStarted() const;
        virtual void infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion) const;
        virtual void infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult) const;
        virtual void infoServerInitCompleted(bool succeeded) const;

        virtual void infoCloseStarted()   const;  // warning: if not closed when destructor called, this will be called after derived instance destructor has already completed...
        virtual void infoCloseCompleted() const;  // warning: if not closed when destructor called, this will be called after derived instance destructor has already completed...

    protected:
        virtual char* returnPassword() = 0;  // must return 0 or a string allocated via malloc()

        // This may be overridden for a custom authenticator.
        // The server's challenge is passed in and the response
        // is expected back.  If false is returned, then an
        // immediate failure is assumed.
        virtual bool encryptChallenge(const unsigned char* challenge,
                                      size_t               challengeSize,
                                      const unsigned char* passwd,
                                      unsigned char*       response,
                                      size_t               maxResponseSize);

    protected:
        rfbCARD32 mapColor(rfbCARD32 color) { return shouldMapColor ? doMapColor(color) : color; }

        // doMapColor() is called by mapColor() only if shouldMapColor is true.
        virtual rfbCARD32 doMapColor(rfbCARD32 color);  // default implementation is identity function

    public:
        virtual bool requestNewUpdate();
        virtual bool checkUpdateNeeded();
        virtual void performUpdateNeeded(int x, int y, size_t w, size_t h);

    protected:
        virtual bool checkAvailableFromRFBServer(size_t n);
        virtual bool readFromRFBServer(void* buf, size_t n);
        virtual bool writeToRFBServer(const void* buf, size_t n);

    protected:
        virtual bool handleRRE8(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleRRE16(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleRRE32(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleCoRRE8(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleCoRRE16(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleCoRRE32(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleHextile8(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleHextile16(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleHextile32(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleZRLE8(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleZRLE16(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleZRLE24A(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleZRLE24B(int rx, int ry, size_t rw, size_t rh);
        virtual bool handleZRLE32(int rx, int ry, size_t rw, size_t rh);

    public:
        static const rfbCARD16 EndianTest /* = 1 */;

    protected:
        size_t scanEncodingsString(rfbCARD32* pDest) const;

    public:
        enum { CONNECT_PORT_OFFSET = 5900 };
        enum { LISTEN_PORT_OFFSET  = 5500 };

    protected:
        bool               isOpen;
        bool               isSameMachine;       // conntected to same machine as this client?  false if !isOpen
        const char*        desktopHost;         // specified in initViaConnect(); 0 if !isOpen; allocted via malloc() or 0 if connected by listening
        struct sockaddr_in desktopIPAddress;    // zeroed if !isOpen; derived from desktopHost
        unsigned           rfbPort;             // specified in initViaConnect() or initViaListen(); 0 if !isOpen; add portOffset to get TCP port
        unsigned           portOffset;          // set to either 0 (if closed) or CONNECT_PORT_OFFSET or LISTEN_PORT_OFFSET if open
        rfbPixelFormat     pixelFormat;         // specified in initVia*(); zeroed if !isOpen
        bool               shouldMapColor;      // specified in initVia*(); false if !isOpen
        const char*        requestedEncodings;  // specified in initVia*(); 0 if !isOpen; allocted via malloc()
        const char*        desktopName;         // 0 until infoServerInitCompleted() called; allocted via malloc()

    protected:
        // Internal state variables
        int              sock;                  // -1 if !isOpen
        bool             sockConnected;         // true iff sock is in connected or accepted state
        rfbServerInitMsg si;
        rfbCARD8         currentEncoding;
        const char*      serverCutText;         // allocated via malloc()
        bool             newServerCutText;
        bool             isBigEndian;
        int              updateNeededX1, updateNeededY1, updateNeededX2, updateNeededY2;
        bool             updateNeeded;
        bool             updateDefinitelyExpected;
        ZlibDecompressor zlibDecompressor;  // see above

    private:
        size_t commBufferPos;
        size_t commBufferFill;
        enum { COMM_BUFFER_SIZE = 4096 };
        char commBuffer[COMM_BUFFER_SIZE];

    protected:
        // The CoRRE encoding uses this buffer and assumes it is big enough
        // to hold 255 * 255 * 32 bits -> 260100 bytes.  640*480 = 307200 bytes
        // Also, hextile assumes it is big enough to hold 16 * 16 * 32 bits = 1024 bytes.
        // Finally, the Zrle encoding assumes this buffer is big enough to hold
        // (rfbZRLETileWidth * rfbZRLETileHeight * 4) = 64 * 64 * 4 = 16384 bytes.
        enum { DECODE_BUFFER_SIZE = (640*480) };
        rfbCARD8 decodeBuffer[DECODE_BUFFER_SIZE];

    private:
        // Disable these copiers:
        RFBProtocol& operator=(const RFBProtocol&) { return *this; }
        RFBProtocol(const RFBProtocol&) {}
    };



    inline rfbCARD16 Swap16IfLE(rfbCARD16 s)
    {
        return (*(char*)&RFBProtocol::EndianTest ? ( ((s & 0xff) << 8) |
                                                     ((s >> 8) & 0xff)   )
                                                 : s);
    }



    inline rfbCARD32 Swap32IfLE(rfbCARD32 l)
    {
        return (*(char*)&RFBProtocol::EndianTest ? ( ((l & 0xff000000) >> 24) |
                                                     ((l & 0x00ff0000) >> 8)  |
                                                     ((l & 0x0000ff00) << 8)  |
                                                     ((l & 0x000000ff) << 24)   )
                                                 : l);
    }

}  // end of namespace rfb



#endif  /* __cplusplus */

# endif  /* __RFBPROTO_H_INCLUDED__ */
