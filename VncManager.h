/***********************************************************************
  VncManager - Vrui Object which connects to and manages the display
  for a remote VNC server.
  Copyright (c) 2007,2008 Voltaic

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2 of the License, or (at your
  option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#ifndef VNCMANAGER_INCLUDED
#define VNCMANAGER_INCLUDED

#include <string>
#include <deque>
#include <string.h>
#include <Vrui/Vrui.h>
#include <GLMotif/Types.h>
#include <Images/RGBImage.h>
#include <Threads/Thread.h>
#include <Threads/Mutex.h>
#include <Threads/Barrier.h>
#include <Comm/MulticastPipe.h>

#include "librfb/rfbproto.h"



namespace Voltaic {

    class VncManager
    {
    //----------------------------------------------------------------------
    public:
        struct RFBProtocolStartupData
        {
            RFBProtocolStartupData();

            bool           initViaConnect;
            const char*    desktopHost;
            unsigned       rfbPort;
            rfbPixelFormat requestedPixelFormat;
            const char*    requestedEncodings;
            bool           sharedDesktopFlag;
        };

    //----------------------------------------------------------------------
    public:
        class TextureManager
        {
        public:
            enum
            {
                tileXOverlap = 0,
                tileYOverlap = 0
            };

        protected:
            bool                     valid;
            GLsizei                  width;
            GLsizei                  height;
            GLsizei                  tileXCount;
            GLsizei                  tileYCount;
            GLint*                   tileXCoord;
            GLint*                   tileYCoord;
            GLuint**                 tileTexID;
            Images::RGBImage::Color* pixelBuf;
            GLsizei                  pixelBufSize;

            // INVARIANTS:
            //
            // If valid is false, then width, height, tileXCount, tileYCount, tileXCoord, tileYCoord and tileTexID, pixelBuf, pixelBufSize are all 0.
            //
            // If valid is true, then:
            //
            //     width > 0 and height > 0.
            //
            //     tileXCount >= 1 and tileYCount >= 1.
            //
            //     For 0 <= xi <= tileXCount, the tileXCoord[xi] values are monotonically increasing.
            //     For 0 <= xi < tileXCount, tileXCoord[xi]+tileXOverlap is a power of 2.
            //     tileXCoord[0] = 0.
            //     tileXCoord[tileXCount] is the least value for which tileXCoord[tileXCount]-tileXCoord[tileXCount-1] is a power of 2 and tileXCoord[tileXCount] >= width.
            //
            //     For 0 <= yi <= tileYCount, the tileYCoord[yi] values are monotonically increasing.
            //     For 0 <= yi < tileYCount, tileYCoord[yi]+tileYOverlap is a power of 2.
            //     tileYCoord[0] = 0.
            //     tileYCoord[tileYCount] is the least value for which tileYCoord[tileYCount]-tileYCoord[tileYCount-1] is a power of 2 and tileYCoord[tileYCount] >= height.
            //
            //     tileTexID is not 0 and tileTexName[xi][yi] is the (xi, yi) openGL texture ID for 0 <= xi < tileXCount and 0 <= yi < tileYCount.
            //
            //     pixelBuf is not zero and contains enough entries for the largest tile.
            //     pixelBufSize is the number of entries in pixelBuf.

        public:
            TextureManager() :
                valid(false),
                width(0), height(0),
                tileXCount(0), tileYCount(0),
                tileXCoord(0), tileYCoord(0),
                tileTexID(0),
                pixelBuf(0),
                pixelBufSize(0)
            {
            }

            virtual ~TextureManager();  // calls close(); override close() instead of destructor in derived classes

            virtual bool init( GLsizei                 forWidth,
                               GLsizei                 forHeight,
                               Images::RGBImage::Color initialColor = Images::RGBImage::Color(0, 0, 255) );  // isValid() iff true is returned

            virtual void close();

            bool isValid() const { return valid; }

        protected:
            virtual bool setTexParameters() const;

            static GLsizei findLeastPow2GE(GLsizei n, size_t maxBits);  // return least power of 2 number >= n that still fits in maxBits

            virtual bool getMaxTileSize( GLsizei& tileMaxWidth, GLsizei& tileMaxHeight,
                                         GLsizei  forWidth,     GLsizei  forHeight,
                                         size_t maxBits,
                                         GLint texLevel, GLint texInternalFormat, GLenum texFormat, GLenum texType) const;

        public:
            GLsizei getWidth()  const { return width;  }
            GLsizei getHeight() const { return height; }

            GLsizei getTileXCount() const { return tileXCount; }
            GLsizei getTileYCount() const { return tileYCount; }

            GLint   getTileXCoord(GLsizei xi) const { return tileXCoord[xi]; }
            GLint   getTileYCoord(GLsizei yi) const { return tileYCoord[yi]; }

            GLsizei getTileWidth(GLsizei xi)  const { return (tileXCoord[xi+1] - tileXCoord[xi] + ((xi < (tileXCount-1)) ? tileXOverlap : 0)); }
            GLsizei getTileHeight(GLsizei yi) const { return (tileYCoord[yi+1] - tileYCoord[yi] + ((yi < (tileYCount-1)) ? tileYOverlap : 0)); }

        public:
            virtual bool write( GLint                          destX,
                                GLint                          destY,
                                GLsizei                        srcWidth,
                                GLsizei                        srcHeight,
                                const Images::RGBImage::Color* srcData ) const;

            virtual bool copy( GLint                          destX,
                               GLint                          destY,
                               GLint                          srcX,
                               GLint                          srcY,
                               GLsizei                        srcWidth,
                               GLsizei                        srcHeight ) const;

            virtual bool fill( GLint                   x,
                               GLint                   y,
                               GLsizei                 w,
                               GLsizei                 h,
                               Images::RGBImage::Color color ) const;

        public:
            virtual bool displayInRectangle( GLfloat x00, GLfloat y00, GLfloat z00,
                                             GLfloat x10, GLfloat y10, GLfloat z10,
                                             GLfloat x11, GLfloat y11, GLfloat z11 ) const;

        private:
            // Disable these copiers:
            TextureManager& operator=(const TextureManager&);
            TextureManager(const TextureManager&);
        };

    //----------------------------------------------------------------------
    public:
        class MessageManager
        {
        public:
            virtual void internalErrorMessage(const char* where, const char* message) = 0;

            virtual void errorMessage(const char* where, const char* message) = 0;

            // errorMessageFromServer() function is separated from the normal errorMessage()
            // because the "message" string comes from the server.  If you are displaying
            // in some environment (e.g., a Web browser) that could be vulnerable to
            // code injection (e.g., javascript), then take care when displaying "message"
            // in a safe manner.
            virtual void errorMessageFromServer(const char* where, const char* message);  // default implementation calls errorMessage(where, message);

            virtual void infoServerInitStarted() = 0;
            virtual void infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion) = 0;
            virtual void infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult) = 0;
            virtual void infoServerInitCompleted(bool succeeded) = 0;
            virtual void infoCloseStarted() = 0;
            virtual void infoCloseCompleted() = 0;
        };

    //----------------------------------------------------------------------
    public:
        class PasswordRetrievalCompletionThunk
        {
        public:
            virtual void postPassword(const char* theRetrievedPassword) = 0;
        };

        class PasswordRetrievalThunk
        {
        public:
            // The implementation of getPassword() cause passwordRetrievalCompletionThunk.postPassword()
            // to be called at some point in the future.  This point at which this call occurs must be
            // be followed by an immediate return from any UI callback or processing; it is not safe to
            // perform any other processing after calling passwordRetrievalCompletionThunk.postPassword().
            // A NULL retrievedPassword indicates the user cancelled authentication.
            // Note: VncManager::eraseStringContents() may be safely called after the call to
            // passwordRetrievalCompletionThunk.postPassword() if you need to erase any latent copies
            // of the password from memory.
            virtual void getPassword(PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk) = 0;
        };

    //----------------------------------------------------------------------
    public:
        // ActionQueue is used to get work messages from the remoteCommThread to
        // be performed on the main thread.  This helps prevent problems where
        // UI elements are not reentrancy- or thread-safe.
        class ActionQueue
        {
        public:
            class Item
            {
                // Note: when creating a derived class, update the following:
                //   - enum ActionQueue::Item::ItemType
                //   - switch statement in ActionQueue::Item::createFromPipe()

            public:
                enum ItemType  // codes used when reading/writing items to a pipe
                {
                    ItemType_GetPasswordItem,
                    ItemType_InitDisplayItem,
                    ItemType_WriteItem,
                    ItemType_CopyItem,
                    ItemType_FillItem,
                    ItemType_InternalErrorMessageItem,
                    ItemType_ErrorMessageItem,
                    ItemType_ErrorMessageFromServerItem,
                    ItemType_InfoServerInitStartedItem,
                    ItemType_InfoProtocolVersionItem,
                    ItemType_InfoAuthenticationResultItem,
                    ItemType_InfoServerInitCompletedItem,
                    ItemType_InfoCloseStartedItem,
                    ItemType_InfoCloseCompletedItem
                };

            public:
                Item() {}
                virtual ~Item();

                // createFromPipeContainingTypeCode() reads ItemType, then calls appropriate createFromPipe() for that value
                static Item* createFromPipeContainingTypeCode(ActionQueue& actionQueue, Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const = 0;
                virtual bool perform(VncManager& vncManager) = 0;

            public:
                virtual bool indicatesClose() const;  // returns false; only class InfoCloseCompletedItem overrides to return true

            private:
                // Disable these copiers:
                Item& operator=(const Item&);
                Item(const Item&);
            };

            class GetPasswordItem : public Item
            {
            public:
                GetPasswordItem() {}

                static GetPasswordItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InitDisplayItem : public Item
            {
            protected:
                const rfbServerInitMsg si;
                const std::string      desktopName;

            public:
                InitDisplayItem(const rfbServerInitMsg& si, const char* desktopName) :
                    si(si),
                    desktopName(desktopName)
                {
                }

                static InitDisplayItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class WriteItem : public Item
            {
            protected:
                const GLint                    destX;
                const GLint                    destY;
                const GLsizei                  srcWidth;
                const GLsizei                  srcHeight;
                Images::RGBImage::Color* const srcData;

            public:
                WriteItem( GLint                    destX,
                           GLint                    destY,
                           GLsizei                  srcWidth,
                           GLsizei                  srcHeight,
                           Images::RGBImage::Color* srcData ) :
                    destX(destX),
                    destY(destY),
                    srcWidth(srcWidth),
                    srcHeight(srcHeight),
                    srcData(srcData)
                {
                }

                virtual ~WriteItem();  // deletes srcData

                static WriteItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class CopyItem : public Item
            {
            protected:
                const GLint   destX;
                const GLint   destY;
                const GLint   srcX;
                const GLint   srcY;
                const GLsizei srcWidth;
                const GLsizei srcHeight;

            public:
                CopyItem( GLint   destX,
                          GLint   destY,
                          GLint   srcX,
                          GLint   srcY,
                          GLsizei srcWidth,
                          GLsizei srcHeight ) :
                    srcX(srcX),
                    srcY(srcY),
                    destX(destX),
                    destY(destY),
                    srcWidth(srcWidth),
                    srcHeight(srcHeight)
                {
                }

                static CopyItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class FillItem : public Item
            {
            protected:
                const GLint                   x;
                const GLint                   y;
                const GLsizei                 w;
                const GLsizei                 h;
                const Images::RGBImage::Color color;

            public:
                FillItem( GLint                   x,
                          GLint                   y,
                          GLsizei                 w,
                          GLsizei                 h,
                          Images::RGBImage::Color color ) :
                    x(x),
                    y(y),
                    w(w),
                    h(h),
                    color(color)
                {
                }

                static FillItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InternalErrorMessageItem : public Item
            {
            protected:
                std::string where;
                std::string message;

            public:
                InternalErrorMessageItem(const char* where, const char* message) :
                    where(where),
                    message(message)
                {
                }

                static InternalErrorMessageItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class ErrorMessageItem : public Item
            {
            protected:
                std::string where;
                std::string message;

            public:
                ErrorMessageItem(const char* where, const char* message) :
                    where(where),
                    message(message)
                {
                }

                static ErrorMessageItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class ErrorMessageFromServerItem : public Item
            {
            protected:
                std::string where;
                std::string message;

            public:
                ErrorMessageFromServerItem(const char* where, const char* message) :
                    where(where),
                    message(message)
                {
                }

                static ErrorMessageFromServerItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InfoServerInitStartedItem : public Item
            {
            public:
                InfoServerInitStartedItem() {}

                static InfoServerInitStartedItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InfoProtocolVersionItem : public Item
            {
            protected:
                const int serverMajorVersion;
                const int serverMinorVersion;
                const int clientMajorVersion;
                const int clientMinorVersion;

            public:
                InfoProtocolVersionItem(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion) :
                    serverMajorVersion(serverMajorVersion),
                    serverMinorVersion(serverMinorVersion),
                    clientMajorVersion(clientMajorVersion),
                    clientMinorVersion(clientMinorVersion)
                {
                }

                static InfoProtocolVersionItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InfoAuthenticationResultItem : public Item
            {
            protected:
                const bool      succeeded;
                const rfbCARD32 authScheme;
                const rfbCARD32 authResult;

            public:
                InfoAuthenticationResultItem(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult) :
                    succeeded(succeeded),
                    authScheme(authScheme),
                    authResult(authResult)
                {
                }

                static InfoAuthenticationResultItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InfoServerInitCompletedItem : public Item
            {
            protected:
                const bool succeeded;

            public:
                InfoServerInitCompletedItem(bool succeeded) :
                    succeeded(succeeded)
                {
                }

                static InfoServerInitCompletedItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InfoCloseStartedItem : public Item
            {
            public:
                InfoCloseStartedItem() {}

                static InfoCloseStartedItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);
            };

            class InfoCloseCompletedItem : public Item
            {
            public:
                InfoCloseCompletedItem() {}

                static InfoCloseCompletedItem* createFromPipe(Comm::MulticastPipe& pipe);

            public:
                virtual void broadcast(Comm::MulticastPipe& pipe) const;
                virtual bool perform(VncManager& vncManager);

            public:
                virtual bool indicatesClose() const;  // returns true; ActionQueue::threadStartForSlaveNodes() uses this to know when to quit
            };

        public:
            ActionQueue(Comm::MulticastPipe* clusterMulticastPipe) :  // takes ownership of clusterMulticastPipe
                mutex(),
                queue(),
                clusterMulticastPipe(clusterMulticastPipe)
            {
            }

            virtual ~ActionQueue();  // deletes clusterMulticastPipe if clusterMulticastPipe != 0

        public:
            // Remote communication thread operations:
            virtual void  add(Item* item);                               // does nothing if item == 0
            virtual void  addAndBroadcast(Item* item);                   // broadcasts item to clusterMulticastPipe if clusterMulticastPipe != 0, the performs add(item)

        public:
            // Main thread operations:
            virtual Item* removeNext();                                  // caller must delete returned value if not 0
            virtual bool  performQueuedActions(VncManager& vncManager);  // perform all actions currently in queue; returns true iff some actions were performed
                    void  flush() { flush(false); }                      // removes and deletes all items in queue

        protected:
            virtual void flush(bool withoutLocking);                     // removes and deletes all items in queue

        public:
            // Thread loop for slave nodes:
            virtual void* threadStartForSlaveNodes();

        protected:
            typedef std::deque<Item*> Queue;

            Threads::Mutex             mutex;
            Queue                      queue;
            Comm::MulticastPipe* const clusterMulticastPipe;  // pipe connecting the nodes in a rendering cluster

        private:
            // Disable these copiers:
            ActionQueue& operator=(const ActionQueue&);
            ActionQueue(const ActionQueue&);
        };

    //----------------------------------------------------------------------
    public:
        class RFBProtocolImplementation :
            public rfb::RFBProtocol
        {
        public:
            virtual void* threadStartForMasterNode(RFBProtocolStartupData startupData);

        public:
            RFBProtocolImplementation( VncManager&  vncManager,
                                       ActionQueue& actionQueue ) :
                rfb::RFBProtocol(),
                vncManager(vncManager),
                actionQueue(actionQueue),
                passwordRetrievalBarrier(2),
                retrievedPassword()
            {
            }

            virtual ~RFBProtocolImplementation();

            virtual void close();  // to be safe, call close() before this object is destroyed

        protected:
            virtual bool receivedSetColourMapEntries(const rfbSetColourMapEntriesMsg& msg);
            virtual bool receivedBell(const rfbBellMsg& msg);
            virtual bool receivedServerCutText(const rfbServerCutTextMsg& msg);

        protected:
            virtual void copyRectData(void* data, int x, int y, size_t w, size_t h);
            virtual void copyRect(int fromX, int fromY, int toX, int toY, size_t w, size_t h);
            virtual void fillRect(rfbCARD32 color, int x, int y, size_t w, size_t h);

        protected:
            // The default implementation routes all error messages through errorMessage().
            // If your display environment is vulnerable to malicious strings (e.g., javascript
            // in a Web browser), then you should override errorMessageFromServer() to carefully
            // handle its message.
            virtual void errorMessage(const char* where, const char* message) const;

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

            // errorMessageFromServer() function is separated from the normal errorMessage()
            // because the "message" string comes from the server.  If you are displaying
            // in some environment (e.g., a Web browser) that could be vulnerable to
            // code injection (e.g., javascript), then take care when displaying "message"
            // in a safe manner.
            virtual void errorMessageFromServer(const char* where, const char* message) const;  // default implementation calls errorMessage(where, message);

            virtual void infoServerInitStarted() const;
            virtual void infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion) const;
            virtual void infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult) const;
            virtual void infoServerInitCompleted(bool succeeded) const;

            virtual void infoCloseStarted()   const;  // warning: if not closed when destructor called, this will be called after derived instance destructor has already completed...
            virtual void infoCloseCompleted() const;  // warning: if not closed when destructor called, this will be called after derived instance destructor has already completed...

        protected:
            // We implement an asynchronous password retrieval (with respect to the main UI
            // thread) through the use of the passwordRetrievalBarrier semaphore.
            // returnPassword() returns a dummy password, and encryptChallenge() is
            // implemented to wait on the passwordRetrievalBarrier.
            virtual char* returnPassword();

            virtual bool encryptChallenge(const unsigned char* challenge,
                                          size_t               challengeSize,
                                          const unsigned char* ignoredPasswd,
                                          unsigned char*       response,
                                          size_t               maxResponseSize);
        public:
            // This method is called from the main UI thread after the
            // password has been retrieved.
            virtual void postRetrievedPassword(const char* theRetrievedPassword);

        protected:
            VncManager&      vncManager;
            ActionQueue&     actionQueue;
            Threads::Barrier passwordRetrievalBarrier;
            std::string      retrievedPassword;

        private:
            // Disable these copiers:
            RFBProtocolImplementation& operator=(const RFBProtocolImplementation&);
            RFBProtocolImplementation(const RFBProtocolImplementation&);
        };

    //----------------------------------------------------------------------
    public:
        class UIPasswordRetrievalCompletionThunk :
            public PasswordRetrievalCompletionThunk
        {
        public:
            UIPasswordRetrievalCompletionThunk(VncManager& vncManager) :
                vncManager(vncManager)
            {
            }

        public:
            virtual void postPassword(const char* theRetrievedPassword);

        protected:
            VncManager& vncManager;
        };

        friend class UIPasswordRetrievalCompletionThunk;  // for access to getRfbProto

    //----------------------------------------------------------------------
    public:
        VncManager( MessageManager&         messageManager,
                    PasswordRetrievalThunk& passwordRetrievalThunk,
                    bool                    isSlave                         = false,
                    Comm::MulticastPipe*    clusterMulticastPipe = 0 ) :
            messageManager(messageManager),
            passwordRetrievalThunk(passwordRetrievalThunk),
            passwordRetrievalCompletionThunk(*this),
            isSlave(isSlave),
            actionQueue(clusterMulticastPipe),
            remoteDisplay(),
            remoteCommThread(),
            remoteCommThreadStarted(false),
            rfbProto(0)
        {
        }

        virtual ~VncManager();

    public:
        static void eraseStringContents(std::string& s)  // useful for clearing password strings
        {
            const std::string::size_type n = s.size();
            if (n > 0)
            {
                s.replace(0, n, n, ' ');  // clear string contents from memory
                s.erase();
            }
        }

    public:
        virtual void initiatePasswordRetrieval();

    protected:
        RFBProtocolImplementation* getRfbProto() const { return rfbProto; }  // for use by friend class UIPasswordRetrievalCompletionThunk

    public:
        bool                      getRfbIsOpen()             const { return !rfbProto ? false                        : rfbProto->getIsOpen(); }
        bool                      getRfbIsSameMachine()      const { return !rfbProto ? false                        : rfbProto->getIsSameMachine(); }
        const char*               getRfbDesktopHost()        const { return !rfbProto ? 0                            : rfbProto->getDesktopHost(); }  // 0 if connected by listening
        const struct sockaddr_in* getRfbDesktopIPAddress()   const { return !rfbProto ? (const struct sockaddr_in*)0 : &rfbProto->getDesktopIPAddress(); }
        unsigned                  getRfbRfbPort()            const { return !rfbProto ? 0                            : rfbProto->getRfbPort(); }
        unsigned                  getRfbPortOffset()         const { return !rfbProto ? 0                            : rfbProto->getPortOffset(); }
        unsigned                  getRfbTcpPort()            const { return !rfbProto ? 0                            : rfbProto->getTcpPort(); }
        const rfbPixelFormat*     getRfbPixelFormat()        const { return !rfbProto ? (const rfbPixelFormat*)0     : &rfbProto->getPixelFormat(); }
        bool                      getRfbShouldMapColor()     const { return !rfbProto ? false                        : rfbProto->getShouldMapColor(); }
        const char*               getRfbRequestedEncodings() const { return !rfbProto ? 0                            : rfbProto->getRequestedEncodings(); }
        const char*               getRfbDesktopName()        const { return !rfbProto ? 0                            : rfbProto->getDesktopName(); }

        bool                      getRfbSockConnected()      const { return !rfbProto ? false                        : rfbProto->getSockConnected(); }
        const rfbServerInitMsg*   getRfbServerInitMsg()      const { return !rfbProto ? (const rfbServerInitMsg*)0   : &rfbProto->getServerInitMsg(); }
        rfbCARD8                  getRfbCurrentEncoding()    const { return !rfbProto ? (rfbCARD8)rfbEncodingRaw     : rfbProto->getCurrentEncoding(); }
        bool                      getRfbIsBigEndian()        const { return !rfbProto ? false                        : rfbProto->getIsBigEndian(); }

    public:
        virtual void startup(const RFBProtocolStartupData& rfbProtocolStartupData);
        virtual void shutdown();

        // performQueuedActions() must be called periodically
        // to make sure that updates from the remote desktop
        // are posted to the remoteDisplay object.  true is
        // returned iff something changed.
        virtual bool performQueuedActions();

        // Use drawRemoteDisplaySurface() to send OpenGL commands
        // to show the current remote display.
        virtual void drawRemoteDisplaySurface( GLfloat x00, GLfloat y00, GLfloat z00,
                                               GLfloat x10, GLfloat y10, GLfloat z10,
                                               GLfloat x11, GLfloat y11, GLfloat z11  ) const;

        // These methods return the current screen size of the remote host.
        // They both return 0 before the remote host is connected.
        GLsizei getRemoteDisplayWidth()  const { return remoteDisplay.getWidth();  }
        GLsizei getRemoteDisplayHeight() const { return remoteDisplay.getHeight(); }

        // The send* methods are pass-throughs to rfbProto.
        // They return false (always) if isSlave is true,
        // i.e., they are usable only from the master node.
        virtual bool sendFramebufferUpdateRequest(int x, int y, size_t w, size_t h, bool incremental);
        virtual bool sendKeyEvent(rfbCARD32 key, bool down);
        virtual bool sendPointerEvent(int x, int y, int buttonMask);
        virtual bool sendClientCutText(const char* str, size_t len);

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
        bool            getIsSlave()       const { return isSlave; }
        TextureManager& getRemoteDisplay()       { return remoteDisplay; }

    public:
        MessageManager& messageManager;

    protected:
        PasswordRetrievalThunk&            passwordRetrievalThunk;
        UIPasswordRetrievalCompletionThunk passwordRetrievalCompletionThunk;
        const bool                         isSlave;
        ActionQueue                        actionQueue;
        TextureManager                     remoteDisplay;
        Threads::Thread                    remoteCommThread;
        bool                               remoteCommThreadStarted;
        RFBProtocolImplementation*         rfbProto;  // 0 if isSlave

    private:
        // Disable these copiers:
        VncManager& operator=(const VncManager&);
        VncManager(const VncManager&);
    };

}  // end of namespace Voltaic

#endif  // VNCMANAGER_INCLUDED
