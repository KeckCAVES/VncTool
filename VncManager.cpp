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

#include "VncManager.h"



namespace Voltaic {

//----------------------------------------------------------------------

inline Images::RGBImage::Color convertPixelToRGB(const rfbPixelFormat& format, rfbCARD32 pixel)
{
    return Images::RGBImage::Color( ((pixel >> format.redShift)   & format.redMax),
                                    ((pixel >> format.greenShift) & format.greenMax),
                                    ((pixel >> format.blueShift)  & format.blueMax)   );
}



static void writeString(Comm::MulticastPipe& pipe, const std::string& s)
{
    const std::string::size_type len = s.size();
    pipe.write(len);
    pipe.writeRaw(s.c_str(), len);
}



static void readString(Comm::MulticastPipe& pipe, std::string& s)
{
    std::string::size_type len;
    pipe.read(len);

    s.erase();
    if (len > 0)
    {
        s.reserve(len+1);
        while (len > 0)
        {
            char buf[256];
            const size_t n = (len <= sizeof(buf)) ? len : sizeof(buf);
            pipe.readRaw(buf, n);
            s.append(buf, n);
            len -= n;
        }
    }
}



//----------------------------------------------------------------------
// VncManager::TextureManager methods

VncManager::TextureManager::~TextureManager()
{
    close();
}



bool VncManager::TextureManager::init( GLsizei                 forWidth,
                                       GLsizei                 forHeight,
                                       Images::RGBImage::Color initialColor )
{
    static const GLint  texLevel          = 0;
    static const GLint  texInternalFormat = pixelBuf->numComponents;
    static const GLint  texBorder         = 0;
    static const GLenum texFormat         = GL_RGB;
    static const GLenum texType           = GL_UNSIGNED_BYTE;

    close();

    try
    {
        if ((forWidth > 0) && (forHeight > 0))
        {
            size_t maxDimBits = 8*sizeof(GLint) - 1;  // we want to be able to express all coordinates in a GLint, at least
            if (sizeof(GLsizei) < sizeof(GLint))  // this would be weird, but check anyway...
                maxDimBits = 8*sizeof(GLsizei);

            GLsizei maxDim = 0;
            for (size_t i = 0; i < maxDimBits; i++)
                maxDim = (GLsizei)(maxDim << 1) | (GLsizei)1;

            if ((forWidth <= maxDim) && (forHeight <= maxDim))
            {
                GLsizei tileMaxWidth, tileMaxHeight;
                if (getMaxTileSize(tileMaxWidth, tileMaxHeight, forWidth, forHeight, maxDimBits, texLevel, texInternalFormat, texFormat, texType))
                {
                    width  = forWidth;
                    height = forHeight;

                    // Note: (forWidth + tileMaxWidth-tileXOverlap-1) might overflow GLsizei, so calculate with conditional...
                    tileXCount = (forWidth - tileXOverlap) / (tileMaxWidth - tileXOverlap);
                    if ((forWidth - tileXOverlap) > (tileXCount * (tileMaxWidth - tileXOverlap)))
                        tileXCount++;

                    // Note: (forHeight + tileMaxHeight-tileYOverlap-1) might overflow GLsizei, so calculate with conditional...
                    tileYCount = (forHeight - tileYOverlap) / (tileMaxHeight - tileYOverlap);
                    if ((forHeight - tileYOverlap) > (tileYCount * (tileMaxHeight - tileYOverlap)))
                        tileYCount++;

                    tileXCoord = new GLint [tileXCount+1];  // may throw exception
                    if (tileXCoord)
                    {
                        tileXCoord[0] = 0;

                        for (GLsizei xi = 1; xi < tileXCount; xi++)
                            tileXCoord[xi] = tileXCoord[xi-1] + tileMaxWidth - tileXOverlap;

                        tileXCoord[tileXCount] = tileXCoord[tileXCount-1] + findLeastPow2GE(forWidth-tileXCoord[tileXCount-1], maxDimBits);

                        tileYCoord = new GLint [tileYCount+1];  // may throw exception
                        if (tileYCoord)
                        {
                            tileYCoord[0] = 0;

                            for (GLsizei yi = 1; yi < tileYCount; yi++)
                                tileYCoord[yi] = tileYCoord[yi-1] + tileMaxHeight - tileYOverlap;

                            tileYCoord[tileYCount] = tileYCoord[tileYCount-1] + findLeastPow2GE(forHeight-tileYCoord[tileYCount-1], maxDimBits);

                            tileTexID = new GLuint* [tileXCount];  // may throw exception
                            if (tileTexID)
                            {
                                memset(tileTexID, 0, tileXCount*sizeof(*tileTexID));

                                bool texIDArrayAllocFailed = false;
                                for (GLsizei xi = 0; xi < tileXCount; xi++)
                                {
                                    tileTexID[xi] = new GLuint [tileYCount];  // may throw exception
                                    if (!tileTexID[xi])
                                    {
                                        texIDArrayAllocFailed = true;
                                        break;
                                    }
                                    else
                                        memset(tileTexID[xi], 0, tileYCount*sizeof(*(tileTexID[xi])));
                                }

                                if (!texIDArrayAllocFailed)
                                {
                                    // BUG: tileMaxWidth*tileMaxHeight may overflow...

                                    pixelBufSize = tileMaxWidth*tileMaxHeight;
                                    pixelBuf     = new Images::RGBImage::Color [pixelBufSize];  // may throw exception
                                    if (pixelBuf)
                                    {
                                        for (size_t i = 0; i < tileMaxWidth*tileMaxHeight; i++)
                                            pixelBuf[i] = initialColor;

                                        bool texAllocFailed = false;
                                        for (GLsizei xi = 0; !texAllocFailed && (xi < tileXCount); xi++)
                                        {
                                            for (GLsizei yi = 0; !texAllocFailed && (yi < tileYCount); yi++)
                                            {
                                                glGenTextures(1, &tileTexID[xi][yi]);
                                                if ((glGetError() != GL_NO_ERROR) || !tileTexID[xi][yi])
                                                    texAllocFailed = true;
                                                else
                                                {
                                                    glBindTexture(GL_TEXTURE_2D, tileTexID[xi][yi]);
                                                    if (glGetError() != GL_NO_ERROR)
                                                        texAllocFailed = true;
                                                    else
                                                    {
                                                        if (!setTexParameters())
                                                            texAllocFailed = true;
                                                        else
                                                        {
                                                            const GLsizei w = getTileWidth(xi);
                                                            const GLsizei h = getTileHeight(yi);

                                                            glTexImage2D(GL_TEXTURE_2D, texLevel, texInternalFormat, w, h, texBorder, texFormat, texType, pixelBuf);
                                                            if (glGetError() != GL_NO_ERROR)
                                                                texAllocFailed = true;
                                                        }

                                                        glBindTexture(GL_TEXTURE_2D, 0);  // protect texture
                                                    }
                                                }
                                            }
                                        }

                                        if (!texAllocFailed)
                                            valid = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    catch (...)
    {
        close();

        throw;
    }

    if (!valid)
        close();

    return valid;
}



void VncManager::TextureManager::close()
{
    if (pixelBuf)
    {
        delete [] pixelBuf;

        pixelBuf = 0;
    }

    pixelBufSize = 0;

    if (tileTexID)
    {
        for (GLsizei xi = 0; xi < tileXCount; xi++)
        {
            GLuint* const ids = tileTexID[xi];
            if (ids)
            {
                for (GLsizei yi = 0; yi < tileYCount; yi++)
                {
                    GLuint id = ids[yi];
                    if (glIsTexture(id))
                        glDeleteTextures(1, &id);
                }

                delete [] ids;
            }
        }

        delete [] tileTexID;

        tileTexID = 0;
    }

    if (tileXCoord)
    {
        delete [] tileXCoord;

        tileXCoord = 0;
    }

    if (tileYCoord)
    {
        delete [] tileYCoord;

        tileYCoord = 0;
    }

    tileXCount = 0;
    tileYCount = 0;

    width  = 0;
    height = 0;

    valid  = false;
}



bool VncManager::TextureManager::setTexParameters() const
{
    glEnable(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR)
        return false;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    if (glGetError() != GL_NO_ERROR)
        return false;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  0);
    if (glGetError() != GL_NO_ERROR)
        return false;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP);
    if (glGetError() != GL_NO_ERROR)
        return false;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP);
    if (glGetError() != GL_NO_ERROR)
        return false;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (glGetError() != GL_NO_ERROR)
        return false;

#if 0  // Do not set GL_TEXTURE_MIN_FILTER so as to avoid seams in adjacent textures...
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    if (glGetError() != GL_NO_ERROR)
        return false;
#endif

    return true;
}



GLsizei VncManager::TextureManager::findLeastPow2GE(GLsizei n, size_t maxBits)  // static method; return least power of 2 number >= n that still fits in maxBits
{
    if (maxBits <= 0)
        return 1;
    else if (n <= 1)
        return 1;
    else
    {
        GLsizei maxPow2 = (GLsizei)1 << (maxBits - 1);

        if (n >= maxPow2)  // maxPow2 is the max for maxBits
            return maxPow2;
        else
        {
            GLsizei pow2 = 1;

            while (pow2 < maxPow2)
                if (pow2 >= n)
                    break;
                else
                    pow2 <<= 1;

            return pow2;
        }
    }
}



bool VncManager::TextureManager::getMaxTileSize( GLsizei& tileMaxWidth, GLsizei& tileMaxHeight,
                                                 GLsizei  forWidth,     GLsizei  forHeight,
                                                 size_t maxBits,
                                                 GLint texLevel, GLint texInternalFormat, GLenum texFormat, GLenum texType) const
{
    static const GLint texBorder = 0;

    tileMaxWidth  = findLeastPow2GE(forWidth,  maxBits);
    tileMaxHeight = findLeastPow2GE(forHeight, maxBits);

    while ((tileMaxWidth > 0) && (tileMaxHeight > 0))
    {
        GLint testWidth  = 0;
        GLint testHeight = 0;

        setTexParameters();

        glTexImage2D(GL_PROXY_TEXTURE_2D, texLevel, texInternalFormat, tileMaxWidth, tileMaxHeight, texBorder, texFormat, texType, NULL);
        if (glGetError() == GL_NO_ERROR)
        {
            glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, texLevel, GL_TEXTURE_WIDTH,  &testWidth);
            glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, texLevel, GL_TEXTURE_HEIGHT, &testHeight);
        }

        if ((testWidth == tileMaxWidth) && (testHeight == tileMaxHeight))
            break;  // found a workable tile size
        else
        {
            if (tileMaxWidth > tileMaxHeight)
                tileMaxWidth /= 2;  // avoid sign-extension problems with >> if GLsizei happens to be signed...
            else if (tileMaxHeight > tileMaxWidth)
                tileMaxHeight /= 2;  // avoid sign-extension problems with >> if GLsizei happens to be signed...
            else
            {
                // If the last-tested tile size was square, decrease the dimension
                // that is furthest from matching its requested dimension.

                if ((forWidth-tileMaxWidth) > (forHeight-tileMaxHeight))
                    tileMaxWidth /= 2;  // avoid sign-extension problems with >> if GLsizei happens to be signed...
                else
                    tileMaxHeight /= 2;  // avoid sign-extension problems with >> if GLsizei happens to be signed...
            }
        }
    }

    if ((tileMaxWidth > 0) && (tileMaxHeight > 0))
        return true;
    else
    {
        tileMaxWidth  = 0;
        tileMaxHeight = 0;

        return false;
    }
}



bool VncManager::TextureManager::write( GLint                          destX,
                                        GLint                          destY,
                                        GLsizei                        srcWidth,
                                        GLsizei                        srcHeight,
                                        const Images::RGBImage::Color* srcData ) const
{
    static const GLint  texLevel  = 0;
    static const GLenum texFormat = GL_RGB;
    static const GLenum texType   = GL_UNSIGNED_BYTE;

    if (!valid)
        return false;
    else
    {
        if ( (srcWidth            <= 0)      ||
             (srcHeight           <= 0)      ||
             ((destX + srcWidth)  <= 0)      ||
             ((destY + srcHeight) <= 0)      ||
             (destX               >= width)  ||
             (destY               >= height)    )
        {
            return true;  // nothing to do...
        }
        else if (!srcData)
        {
            return false;
        }
        else
        {
            // First, see if the srcData is contained in just one tile column.
            // If so, we can transfer the data directly.  Otherwise,
            // we'll have to extract individual scan lines into pixelBuf.
            // In the process of checking this, we'll also identify the
            // first tile column and the first tile row to be affected.

            GLsizei firstTileCol = 0;  // first tile column index that will contain srcData

            bool inOneCol = false;
            if (destX >= 0)
                for ( ; firstTileCol < tileXCount; firstTileCol++)
                    if ((tileXCoord[firstTileCol] <= destX) && (destX < tileXCoord[firstTileCol+1]))
                    {
                        if (firstTileCol < (tileXCount - 1))
                        {
                            if ((destX + srcWidth) <= (tileXCoord[firstTileCol+1] - tileXOverlap))
                                inOneCol = true;
                        }
                        else  // last column; no overlap with (nonexistent) next column
                        {
                            if ((destX + srcWidth) <= tileXCoord[tileXCount])
                                inOneCol = true;
                        }

                        break;
                    }

            GLsizei firstTileRow = 0;  // first tile row index that will contain srcData

            if (destY >= 0)
                for ( ; firstTileRow < tileYCount; firstTileRow++)
                    if ((tileYCoord[firstTileRow] <= destY) && (destY < tileYCoord[firstTileRow+1]))
                        break;

            if (inOneCol)
            {
                const GLsizei tileCol = firstTileCol;  // we're staying in this tile column
                const GLint   xOffset = destX - tileXCoord[firstTileCol];

                const GLsizei w = srcWidth;

                GLsizei tileRow         = firstTileRow;
                GLint   yOffset         = (destY < 0) ? 0 : (destY - tileYCoord[firstTileRow]);
                GLsizei rowsTransferred = (destY < 0) ? -destY : 0;  // will skip -destY rows if destY < 0

                const Images::RGBImage::Color* buf = (srcData + (rowsTransferred*srcWidth));

                while ((tileRow < tileYCount) && (rowsTransferred < srcHeight))
                {
                    const GLsizei tileHeight = getTileHeight(tileRow);

                    GLsizei h = srcHeight - rowsTransferred;
                    if (h > (tileHeight - yOffset))
                        h = (tileHeight - yOffset);

                    glBindTexture(GL_TEXTURE_2D, tileTexID[tileCol][tileRow]);
                    if (glGetError() != GL_NO_ERROR)
                        return false;

                    if (!setTexParameters())
                    {
                        glBindTexture(GL_TEXTURE_2D, 0);  // protect texture
                        return false;
                    }

                    bool xfError = false;
                    glTexSubImage2D(GL_TEXTURE_2D, texLevel, xOffset, yOffset, w, h, texFormat, texType, buf);
                    if (glGetError() != GL_NO_ERROR)
                        xfError = true;

                    glBindTexture(GL_TEXTURE_2D, 0);  // protect texture

                    if (xfError)
                        return false;

                    // Adjust h to be the effective value for this tile:
                    if (tileRow < (tileYCount-1))
                        h -= tileYOverlap;

                    tileRow++;
                    yOffset = 0;
                    rowsTransferred += h;

                    buf += (w * h);
                }

                return true;
            }
            else
            {
                GLsizei tileCol         = firstTileCol;
                GLint   xOffset         = (destX < 0) ? 0 : (destX - tileXCoord[firstTileCol]);
                GLsizei colsTransferred = (destX < 0) ? -destX : 0;  // will skip -destX cols if destX < 0

                while ((tileCol < tileXCount) && (colsTransferred < srcWidth))
                {
                    const GLsizei tileWidth = getTileWidth(tileCol);

                    GLsizei w = srcWidth - colsTransferred;
                    if (w > (tileWidth - xOffset))
                        w = (tileWidth - xOffset);

                    GLsizei tileRow         = firstTileRow;
                    GLint   yOffset         = (destY < 0) ? 0 : (destY - tileYCoord[firstTileRow]);
                    GLsizei rowsTransferred = (destY < 0) ? -destY : 0;  // will skip -destY rows if destY < 0

                    while ((tileRow < tileYCount) && (rowsTransferred < srcHeight))
                    {
                        const GLsizei tileHeight = getTileHeight(tileRow);

                        GLsizei h = srcHeight - rowsTransferred;
                        if (h > (tileHeight - yOffset))
                            h = (tileHeight - yOffset);

                        // Stage the pixel data into pixelBuf:
                        Images::RGBImage::Color* p = pixelBuf;
                        for (GLsizei pr = rowsTransferred; pr < (rowsTransferred + h); pr++)
                        {
                            memcpy(p, (srcData + ((pr*srcWidth) + colsTransferred)), w*sizeof(*srcData));
                            p += w;
                        }

                        glBindTexture(GL_TEXTURE_2D, tileTexID[tileCol][tileRow]);
                        if (glGetError() != GL_NO_ERROR)
                            return false;

                        if (!setTexParameters())
                            return false;

                        bool xfError = false;
                        glTexSubImage2D(GL_TEXTURE_2D, texLevel, xOffset, yOffset, w, h, texFormat, texType, pixelBuf);
                        if (glGetError() != GL_NO_ERROR)
                            xfError = true;

                        glBindTexture(GL_TEXTURE_2D, 0);  // protect texture

                        if (xfError)
                            return false;

                        // Adjust h to be the effective value for this tile:
                        if (tileRow < (tileYCount-1))
                            h -= tileYOverlap;

                        tileRow++;
                        yOffset = 0;
                        rowsTransferred += h;
                    }

                    // Adjust w to be the effective value for this tile:
                    if (tileCol < (tileXCount-1))
                        w -= tileXOverlap;

                    tileCol++;
                    xOffset = 0;
                    colsTransferred += w;
                }

                return true;
            }
        }
    }
}



bool VncManager::TextureManager::copy( GLint   destX,
                                       GLint   destY,
                                       GLint   srcX,
                                       GLint   srcY,
                                       GLsizei srcWidth,
                                       GLsizei srcHeight ) const
{
    return false;//!!! implement me !!!
}



bool VncManager::TextureManager::fill( GLint                   x,
                                       GLint                   y,
                                       GLsizei                 w,
                                       GLsizei                 h,
                                       Images::RGBImage::Color color ) const
{
    return false;//!!! implement me !!!
}



bool VncManager::TextureManager::displayInRectangle( GLfloat x00, GLfloat y00, GLfloat z00,
                                                     GLfloat x10, GLfloat y10, GLfloat z10,
                                                     GLfloat x11, GLfloat y11, GLfloat z11 ) const
{
    if (!valid)
        return false;
    else
    {
        for (GLsizei xi = 0; xi < tileXCount; xi++)
            for (GLsizei yi = 0; yi < tileYCount; yi++)
            {
                const GLfloat tx0 = 0.0;
                const GLfloat ty0 = 0.0;
                const GLfloat tx1 = (xi < (tileXCount-1)) ? 1.0 : ((GLfloat)(width  - tileXCoord[xi]) / getTileWidth(xi));
                const GLfloat ty1 = (yi < (tileYCount-1)) ? 1.0 : ((GLfloat)(height - tileYCoord[yi]) / getTileHeight(yi));

                const GLfloat u0 = ((GLfloat)tileXCoord[xi] / width);
                const GLfloat v0 = ((GLfloat)tileYCoord[yi] / height);
                const GLfloat u1 = (xi < (tileXCount-1)) ? ((GLfloat)tileXCoord[xi+1] / width)  : 1.0;
                const GLfloat v1 = (yi < (tileYCount-1)) ? ((GLfloat)tileYCoord[yi+1] / height) : 1.0;

                const GLfloat vx00 = x00 + u0*(x10 - x00) + v0*(x11 - x10);
                const GLfloat vy00 = y00 + u0*(y10 - y00) + v0*(y11 - y10);
                const GLfloat vz00 = z00 + u0*(z10 - z00) + v0*(z11 - z10);

                const GLfloat vx10 = x00 + u1*(x10 - x00) + v0*(x11 - x10);
                const GLfloat vy10 = y00 + u1*(y10 - y00) + v0*(y11 - y10);
                const GLfloat vz10 = z00 + u1*(z10 - z00) + v0*(z11 - z10);

                const GLfloat vx11 = x00 + u1*(x10 - x00) + v1*(x11 - x10);
                const GLfloat vy11 = y00 + u1*(y10 - y00) + v1*(y11 - y10);
                const GLfloat vz11 = z00 + u1*(z10 - z00) + v1*(z11 - z10);

                const GLfloat vx01 = x00 + u0*(x10 - x00) + v1*(x11 - x10);
                const GLfloat vy01 = y00 + u0*(y10 - y00) + v1*(y11 - y10);
                const GLfloat vz01 = z00 + u0*(z10 - z00) + v1*(z11 - z10);

                bool glErr = false;

                glBindTexture(GL_TEXTURE_2D, tileTexID[xi][yi]);

                glBegin(GL_QUADS);
                    glTexCoord2f(tx0, ty0); glVertex3f(vx00, vy00, vz00);
                    glTexCoord2f(tx1, ty0); glVertex3f(vx10, vy10, vz10);
                    glTexCoord2f(tx1, ty1); glVertex3f(vx11, vy11, vz11);
                    glTexCoord2f(tx0, ty1); glVertex3f(vx01, vy01, vz01);
                glEnd();

                glBindTexture(GL_TEXTURE_2D, 0);  // protect texture
            }

        return true;
    }
}



//----------------------------------------------------------------------
// VncManager::ActionQueue::*Item methods

VncManager::ActionQueue::Item::~Item()
{
}



bool VncManager::ActionQueue::Item::indicatesClose() const
{
    return false;
}



VncManager::ActionQueue::Item* VncManager::ActionQueue::Item::createFromPipeContainingTypeCode(ActionQueue& actionQueue, Comm::MulticastPipe& pipe)  // static member
{
    ItemType itemType;
    pipe.read(itemType);

    switch (itemType)
    {
        case ItemType_GetPasswordItem:              return GetPasswordItem::createFromPipe(pipe);
        case ItemType_InitDisplayItem:              return InitDisplayItem::createFromPipe(pipe);
        case ItemType_WriteItem:                    return WriteItem::createFromPipe(pipe);
        case ItemType_CopyItem:                     return CopyItem::createFromPipe(pipe);
        case ItemType_FillItem:                     return FillItem::createFromPipe(pipe);
        case ItemType_InternalErrorMessageItem:     return InternalErrorMessageItem::createFromPipe(pipe);
        case ItemType_ErrorMessageItem:             return ErrorMessageItem::createFromPipe(pipe);
        case ItemType_ErrorMessageFromServerItem:   return ErrorMessageFromServerItem::createFromPipe(pipe);
        case ItemType_InfoServerInitStartedItem:    return InfoServerInitStartedItem::createFromPipe(pipe);
        case ItemType_InfoProtocolVersionItem:      return InfoProtocolVersionItem::createFromPipe(pipe);
        case ItemType_InfoAuthenticationResultItem: return InfoAuthenticationResultItem::createFromPipe(pipe);
        case ItemType_InfoServerInitCompletedItem:  return InfoServerInitCompletedItem::createFromPipe(pipe);
        case ItemType_InfoCloseStartedItem:         return InfoCloseStartedItem::createFromPipe(pipe);
        case ItemType_InfoCloseCompletedItem:       return InfoCloseCompletedItem::createFromPipe(pipe);

        default:
        {
            std::string message = "Unknown ItemType from MulticastPipe: ";
            message += (int)itemType;
            actionQueue.addAndBroadcast(new ActionQueue::InternalErrorMessageItem("VncManager::ActionQueue::Item::createFromPipeContainingTypeCode", message.c_str()));
            return 0;
        }
    }
}



VncManager::ActionQueue::GetPasswordItem* VncManager::ActionQueue::GetPasswordItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    return new GetPasswordItem();
}



void VncManager::ActionQueue::GetPasswordItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_GetPasswordItem);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::GetPasswordItem::perform(VncManager& vncManager)
{
    vncManager.initiatePasswordRetrieval();
}



VncManager::ActionQueue::InitDisplayItem* VncManager::ActionQueue::InitDisplayItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    rfbServerInitMsg si;
    memset(&si, 0, sizeof(si));

    si.framebufferWidth    = pipe.read<unsigned int>();
    si.framebufferHeight   = pipe.read<unsigned int>();
    si.format.bitsPerPixel = pipe.read<unsigned char>();
    si.format.depth        = pipe.read<unsigned char>();
    si.format.bigEndian    = pipe.read<unsigned char>();
    si.format.trueColour   = pipe.read<unsigned char>();
    si.format.redMax       = pipe.read<unsigned int>();
    si.format.greenMax     = pipe.read<unsigned int>();
    si.format.blueMax      = pipe.read<unsigned int>();
    si.format.redShift     = pipe.read<unsigned char>();
    si.format.greenShift   = pipe.read<unsigned char>();
    si.format.blueShift    = pipe.read<unsigned char>();
    si.nameLength          = pipe.read<unsigned long>();

    std::string desktopName;
    readString(pipe, desktopName);

    return new InitDisplayItem(si, desktopName.c_str());
}



void VncManager::ActionQueue::InitDisplayItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InitDisplayItem);

    pipe.write<unsigned int>(si.framebufferWidth);
    pipe.write<unsigned int>(si.framebufferHeight);
    pipe.write<unsigned char>(si.format.bitsPerPixel);
    pipe.write<unsigned char>(si.format.depth);
    pipe.write<unsigned char>(si.format.bigEndian);
    pipe.write<unsigned char>(si.format.trueColour);
    pipe.write<unsigned int>(si.format.redMax);
    pipe.write<unsigned int>(si.format.greenMax);
    pipe.write<unsigned int>(si.format.blueMax);
    pipe.write<unsigned char>(si.format.redShift);
    pipe.write<unsigned char>(si.format.greenShift);
    pipe.write<unsigned char>(si.format.blueShift);
    pipe.write<unsigned long>(si.nameLength);

    writeString(pipe, desktopName);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InitDisplayItem::perform(VncManager& vncManager)
{
    TextureManager& remoteDisplay = vncManager.getRemoteDisplay();
    remoteDisplay.close();
    return remoteDisplay.init(si.framebufferWidth, si.framebufferHeight);
}



VncManager::ActionQueue::WriteItem* VncManager::ActionQueue::WriteItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    GLint                    destX;
    GLint                    destY;
    GLsizei                  srcWidth;
    GLsizei                  srcHeight;
    Images::RGBImage::Color* srcData;

    pipe.read(destX);
    pipe.read(destY);
    pipe.read(srcWidth);
    pipe.read(srcHeight);

    srcData = new Images::RGBImage::Color [srcWidth*srcHeight];
    try
    {
        pipe.readRaw(srcData, srcWidth*srcHeight*sizeof(*srcData));

        return new WriteItem(destX, destY, srcWidth, srcHeight, srcData);
    }
    catch (...)
    {
        delete [] srcData;
        throw;
    }
}



void VncManager::ActionQueue::WriteItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_WriteItem);

    pipe.write(destX);
    pipe.write(destY);
    pipe.write(srcWidth);
    pipe.write(srcHeight);
    pipe.writeRaw(srcData, srcWidth*srcHeight*sizeof(*srcData));

    pipe.finishMessage();
}



bool VncManager::ActionQueue::WriteItem::perform(VncManager& vncManager)
{
    TextureManager& remoteDisplay = vncManager.getRemoteDisplay();
    return remoteDisplay.write(destX, destY, srcWidth, srcHeight, srcData);
}



VncManager::ActionQueue::WriteItem::~WriteItem()
{
    if (srcData)
        delete [] srcData;
}



VncManager::ActionQueue::CopyItem* VncManager::ActionQueue::CopyItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    GLint   destX;
    GLint   destY;
    GLint   srcX;
    GLint   srcY;
    GLsizei srcWidth;
    GLsizei srcHeight;

    pipe.read(destX);
    pipe.read(destY);
    pipe.read(srcX);
    pipe.read(srcY);
    pipe.read(srcWidth);
    pipe.read(srcHeight);

    return new CopyItem(destX, destY, srcX, srcY, srcWidth, srcHeight);
}



void VncManager::ActionQueue::CopyItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_CopyItem);

    pipe.write(destX);
    pipe.write(destY);
    pipe.write(srcX);
    pipe.write(srcY);
    pipe.write(srcWidth);
    pipe.write(srcHeight);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::CopyItem::perform(VncManager& vncManager)
{
    TextureManager& remoteDisplay = vncManager.getRemoteDisplay();
    return remoteDisplay.copy(destX, destY, srcX, srcY, srcWidth, srcHeight);
}



VncManager::ActionQueue::FillItem* VncManager::ActionQueue::FillItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    GLint                   x;
    GLint                   y;
    GLsizei                 w;
    GLsizei                 h;
    Images::RGBImage::Color color;

    pipe.read(x);
    pipe.read(y);
    pipe.read(w);
    pipe.read(h);
    pipe.readRaw(&color, sizeof(color));

    return new FillItem(x, y, w, h, color);
}



void VncManager::ActionQueue::FillItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_FillItem);

    pipe.write(x);
    pipe.write(y);
    pipe.write(w);
    pipe.write(h);
    pipe.writeRaw(&color, sizeof(color));

    pipe.finishMessage();
}



bool VncManager::ActionQueue::FillItem::perform(VncManager& vncManager)
{
    TextureManager& remoteDisplay = vncManager.getRemoteDisplay();
    return remoteDisplay.fill(x, y, w, h, color);
}



VncManager::ActionQueue::InternalErrorMessageItem* VncManager::ActionQueue::InternalErrorMessageItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    std::string where;
    std::string message;

    readString(pipe, where);
    readString(pipe, message);

    return new InternalErrorMessageItem(where.c_str(), message.c_str());
}



void VncManager::ActionQueue::InternalErrorMessageItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InternalErrorMessageItem);

    writeString(pipe, where);
    writeString(pipe, message);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InternalErrorMessageItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.internalErrorMessage(where.c_str(), message.c_str());
    return true;
}



VncManager::ActionQueue::ErrorMessageItem* VncManager::ActionQueue::ErrorMessageItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    std::string where;
    std::string message;

    readString(pipe, where);
    readString(pipe, message);

    return new ErrorMessageItem(where.c_str(), message.c_str());
}



void VncManager::ActionQueue::ErrorMessageItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_ErrorMessageItem);

    writeString(pipe, where);
    writeString(pipe, message);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::ErrorMessageItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.errorMessage(where.c_str(), message.c_str());
    return true;
}



VncManager::ActionQueue::ErrorMessageFromServerItem* VncManager::ActionQueue::ErrorMessageFromServerItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    std::string where;
    std::string message;

    readString(pipe, where);
    readString(pipe, message);

    return new ErrorMessageFromServerItem(where.c_str(), message.c_str());
}



void VncManager::ActionQueue::ErrorMessageFromServerItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_ErrorMessageFromServerItem);

    writeString(pipe, where);
    writeString(pipe, message);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::ErrorMessageFromServerItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.errorMessageFromServer(where.c_str(), message.c_str());
    return true;
}



VncManager::ActionQueue::InfoServerInitStartedItem* VncManager::ActionQueue::InfoServerInitStartedItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    return new InfoServerInitStartedItem();
}



void VncManager::ActionQueue::InfoServerInitStartedItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InfoServerInitStartedItem);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InfoServerInitStartedItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.infoServerInitStarted();
    return true;
}



VncManager::ActionQueue::InfoProtocolVersionItem* VncManager::ActionQueue::InfoProtocolVersionItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    int serverMajorVersion;
    int serverMinorVersion;
    int clientMajorVersion;
    int clientMinorVersion;

    pipe.read(serverMajorVersion);
    pipe.read(serverMinorVersion);
    pipe.read(clientMajorVersion);
    pipe.read(clientMinorVersion);

    return new InfoProtocolVersionItem(serverMajorVersion, serverMinorVersion, clientMajorVersion, clientMinorVersion);
}



void VncManager::ActionQueue::InfoProtocolVersionItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InfoProtocolVersionItem);

    pipe.write(serverMajorVersion);
    pipe.write(serverMinorVersion);
    pipe.write(clientMajorVersion);
    pipe.write(clientMinorVersion);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InfoProtocolVersionItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.infoProtocolVersion(serverMajorVersion, serverMinorVersion, clientMajorVersion, clientMinorVersion);
    return true;
}



VncManager::ActionQueue::InfoAuthenticationResultItem* VncManager::ActionQueue::InfoAuthenticationResultItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    bool      succeeded;
    rfbCARD32 authScheme;
    rfbCARD32 authResult;

    pipe.read(succeeded);
    pipe.read(authScheme);
    pipe.read(authResult);

    return new InfoAuthenticationResultItem(succeeded, authScheme, authResult);
}



void VncManager::ActionQueue::InfoAuthenticationResultItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InfoAuthenticationResultItem);

    pipe.write(succeeded);
    pipe.write(authScheme);
    pipe.write(authResult);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InfoAuthenticationResultItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.infoAuthenticationResult(succeeded, authScheme, authResult);
    return true;
}



VncManager::ActionQueue::InfoServerInitCompletedItem* VncManager::ActionQueue::InfoServerInitCompletedItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    bool succeeded;

    pipe.read(succeeded);

    return new InfoServerInitCompletedItem(succeeded);
}



void VncManager::ActionQueue::InfoServerInitCompletedItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InfoServerInitCompletedItem);

    pipe.write(succeeded);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InfoServerInitCompletedItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.infoServerInitCompleted(succeeded);
    return true;
}



VncManager::ActionQueue::InfoCloseStartedItem* VncManager::ActionQueue::InfoCloseStartedItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    return new InfoCloseStartedItem();
}



void VncManager::ActionQueue::InfoCloseStartedItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InfoCloseStartedItem);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InfoCloseStartedItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.infoCloseStarted();
    return true;
}



VncManager::ActionQueue::InfoCloseCompletedItem* VncManager::ActionQueue::InfoCloseCompletedItem::createFromPipe(Comm::MulticastPipe& pipe)  // static member
{
    return new InfoCloseCompletedItem();
}



void VncManager::ActionQueue::InfoCloseCompletedItem::broadcast(Comm::MulticastPipe& pipe) const
{
    pipe.write(ItemType_InfoCloseCompletedItem);

    pipe.finishMessage();
}



bool VncManager::ActionQueue::InfoCloseCompletedItem::perform(VncManager& vncManager)
{
    vncManager.messageManager.infoCloseCompleted();
    return true;
}



bool VncManager::ActionQueue::InfoCloseCompletedItem::indicatesClose() const
{
    return true;  // VncManager::ActionQueue::threadStartForSlaveNodes() uses this to know when to quit
}



//----------------------------------------------------------------------
// VncManager::ActionQueue methods

VncManager::ActionQueue::~ActionQueue()
{
    flush(true);

    // shut down cluster communication:
    if (clusterMulticastPipe)
        delete clusterMulticastPipe;
}



void VncManager::ActionQueue::add(Item* item)  // called from either thread
{
    if (item)
    {
        mutex.lock();
        queue.push_back(item);
        mutex.unlock();
    }
}



void VncManager::ActionQueue::addAndBroadcast(Item* item)  // called from remoteCommThread
{
    if (item)
    {
        // Broadcast the item before adding it to the queue.
        // Ohterwise, there is a race condition where the item
        // may be deleted before it is broadcast....
        if (clusterMulticastPipe)
            item->broadcast(*clusterMulticastPipe);

        add(item);
    }
}



VncManager::ActionQueue::Item* VncManager::ActionQueue::removeNext()  // called from remoteCommThread
{
    mutex.lock();

    Item* const item = static_cast<Item*>(queue.empty() ? 0 : queue.front());
    if (item)
        queue.pop_front();

    mutex.unlock();

    return item;
}



void VncManager::ActionQueue::flush(bool withoutLocking)  // called from either thread
{
    if (!withoutLocking)
        mutex.lock();

    Item* item;
    while ((item = removeNext()) != 0)
        delete item;

    if (!withoutLocking)
        mutex.unlock();
}



bool VncManager::ActionQueue::performQueuedActions(VncManager& vncManager)  // called from main thread
{
    bool anyActionsPerformed = false;

    for (;;)
    {
        Item* const action = removeNext();
        if (!action)
            break;
        else
        {
            if (!action->perform(vncManager))
            {
                static const char messageFormat[] = "action failed for item type %d";
                char message[sizeof(messageFormat)+20];
                snprintf(message, sizeof(message), messageFormat, (int)action->itemType);
                vncManager.messageManager.internalErrorMessage("VncManager::ActionQueue::performQueuedActions", message);
            }

            delete action;

            anyActionsPerformed = true;
        }
    }

    return anyActionsPerformed;
}



void* VncManager::ActionQueue::threadStartForSlaveNodes()  // called from remoteCommThread; slave nodes only
{
    if (clusterMulticastPipe)
        for (;;)
        {
            Item* const action = Item::createFromPipeContainingTypeCode(*this, *clusterMulticastPipe);
            if (action)
            {
                if (action->indicatesClose())
                {
                    delete action;
                    break;
                }
                else
                {
                    add(action);
                }
            }
        }

    return 0;
}



//----------------------------------------------------------------------
// VncManager::RFBProtocolStartupData methods

VncManager::RFBProtocolStartupData::RFBProtocolStartupData() :
    initViaConnect(false),
    desktopHost(0),
    rfbPort(0),
    requestedPixelFormat(DefaultRequestedPixelFormat),
    requestedEncodings(0),
    sharedDesktopFlag(false)
{
}



//----------------------------------------------------------------------
// VncManager::MessageManager methods

void VncManager::MessageManager::errorMessageFromServer(const char* where, const char* message)
{
    this->errorMessage(where, message);
}



//----------------------------------------------------------------------
// VncManager::RFBProtocolImplementation methods

void* VncManager::RFBProtocolImplementation::threadStartForMasterNode(RFBProtocolStartupData startupData)  // called from remoteCommThread; master node only
{
    Threads::Thread::setCancelState(Threads::Thread::CANCEL_ENABLE);
    Threads::Thread::setCancelType(Threads::Thread::CANCEL_ASYNCHRONOUS);

    bool initSucceeded = (startupData.initViaConnect)
                             ? initViaConnect(startupData.desktopHost, startupData.rfbPort, startupData.requestedPixelFormat, startupData.requestedEncodings, startupData.sharedDesktopFlag)
                             : initViaListen(startupData.rfbPort, startupData.requestedPixelFormat, startupData.requestedEncodings, startupData.sharedDesktopFlag);

    if (initSucceeded)
        initSucceeded = ( sendSetPixelFormat() &&
                          sendSetEncodings()   &&
                          sendFramebufferUpdateRequest(0, 0, si.framebufferWidth, si.framebufferHeight, false) );

    if (initSucceeded)
        while (getIsOpen() && handleRFBServerMessage())
            ;

    return 0;
}



VncManager::RFBProtocolImplementation::~RFBProtocolImplementation()
{
    close();
}



void VncManager::RFBProtocolImplementation::close()
{
    actionQueue.performQueuedActions(vncManager);  // peform any last work

    this->RFBProtocol::close();
}



bool VncManager::RFBProtocolImplementation::receivedSetColourMapEntries(const rfbSetColourMapEntriesMsg& msg)
{
    // We're using true color, so we don't expect to get this message...
    errorMessage("VncManager::RFBProtocolImplementation::receivedSetColourMapEntries", "received unexpected SetColourMapEntries message");
    return false;
}



bool VncManager::RFBProtocolImplementation::receivedBell(const rfbBellMsg& msg)
{
    return false;//!!! implement me !!!
}



bool VncManager::RFBProtocolImplementation::receivedServerCutText(const rfbServerCutTextMsg& msg)
{
    return false;//!!! implement me !!!
}



void VncManager::RFBProtocolImplementation::copyRectData(void* data, int x, int y, size_t w, size_t h)
{
    if ((w > 0) && (h > 0))
    {
        const size_t pixelCount = (w * h);

        Images::RGBImage::Color* srcData = 0;
        try
        {
            srcData = new Images::RGBImage::Color [pixelCount];  // may throw exception
        }
        catch (...)
        {
            // nothing...
        }

        if (!srcData)
            errorMessage("VncManager::RFBProtocolImplementation::copyRectData", "unable to allocate pixel buffer");
        else
        {
            Images::RGBImage::Color* dest = srcData;

            // Note: bitmaps are handed to us in top-to-bottom row order.
            // We switch them to bottom-to-top to be compatible with OpenGL.

            switch (si.format.bitsPerPixel)
            {
                case 8:
                {
                    for (size_t j = 0; j < h; j++)
                    {
                        const rfbCARD8* src = (const rfbCARD8*)data + ((h-1-j)*w);
                        for (size_t i = 0; i < w; i++)
                            *dest++ = convertPixelToRGB(si.format, *src++);
                    }
                }
                break;

                case 16:
                {
                    for (size_t j = 0; j < h; j++)
                    {
                        const rfbCARD16* src = (const rfbCARD16*)data + ((h-1-j)*w);
                        for (size_t i = 0; i < w; i++)
                            *dest++ = convertPixelToRGB(si.format, rfb::Swap16IfLE(*src++));
                    }
                }
                break;

                case 32:
                {
                    for (size_t j = 0; j < h; j++)
                    {
                        const rfbCARD32* src = (const rfbCARD32*)data + ((h-1-j)*w);
                        for (size_t i = 0; i < w; i++)
                            *dest++ = convertPixelToRGB(si.format, rfb::Swap32IfLE(*src++));
                    }
                }
                break;

                default:
                    errorMessage1l("VncManager::RFBProtocolImplementation::copyRectData", "illegal pixel format; bits/pixel not 8, 16 or 32", si.format.bitsPerPixel);
                    delete [] srcData;
                    return;
            }

            actionQueue.addAndBroadcast(new ActionQueue::WriteItem(x, si.framebufferHeight-1-y-h, w, h, srcData));  // srcData will be deleted by ~WriteItem()
        }
    }
}



void VncManager::RFBProtocolImplementation::copyRect(int fromX, int fromY, int toX, int toY, size_t w, size_t h)
{
    actionQueue.addAndBroadcast(new ActionQueue::CopyItem(toX, toY, fromX, fromY, w, h));
}



void VncManager::RFBProtocolImplementation::fillRect(rfbCARD32 color, int x, int y, size_t w, size_t h)
{
    actionQueue.addAndBroadcast(new ActionQueue::FillItem(x, y, w, h, convertPixelToRGB(si.format, color)));
}



void VncManager::RFBProtocolImplementation::errorMessage(const char* where, const char* message) const
{
    actionQueue.addAndBroadcast(new ActionQueue::ErrorMessageItem(where, message));
}



void VncManager::RFBProtocolImplementation::errorMessageForSend(const char* where, const char* message) const
{
    // This happens on the main thread as opposed to
    // the other error and info messages which happen
    // on the remoteCommThread. Therefore, we call
    // the messageManager directly.

    vncManager.messageManager.errorMessage(where, message);
}



void VncManager::RFBProtocolImplementation::errorMessageFromServer(const char* where, const char* message) const
{
    actionQueue.addAndBroadcast(new ActionQueue::ErrorMessageFromServerItem(where, message));
}



void VncManager::RFBProtocolImplementation::infoServerInitStarted() const
{
    actionQueue.addAndBroadcast(new ActionQueue::InfoServerInitStartedItem());
}



void VncManager::RFBProtocolImplementation::infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion) const
{
    actionQueue.addAndBroadcast(new ActionQueue::InfoProtocolVersionItem(serverMajorVersion, serverMinorVersion, clientMajorVersion, clientMinorVersion));
}



void VncManager::RFBProtocolImplementation::infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult) const
{
    actionQueue.addAndBroadcast(new ActionQueue::InfoAuthenticationResultItem(succeeded, authScheme, authResult));
}



void VncManager::RFBProtocolImplementation::infoServerInitCompleted(bool succeeded) const
{
    if (succeeded)
        actionQueue.addAndBroadcast(new ActionQueue::InitDisplayItem(si, desktopName));

    actionQueue.addAndBroadcast(new ActionQueue::InfoServerInitCompletedItem(succeeded));
}



void VncManager::RFBProtocolImplementation::infoCloseStarted() const
{
    actionQueue.addAndBroadcast(new ActionQueue::InfoCloseStartedItem());
}



void VncManager::RFBProtocolImplementation::infoCloseCompleted() const
{
    actionQueue.addAndBroadcast(new ActionQueue::InfoCloseCompletedItem());
}



char* VncManager::RFBProtocolImplementation::returnPassword()
{
    return strdup("password");  // not used; password is retrieved within VncManager::RFBProtocolImplementation::encryptChallenge()
}



bool VncManager::RFBProtocolImplementation::encryptChallenge(const unsigned char* challenge,
                                                             size_t               challengeSize,
                                                             const unsigned char* ignoredPasswd,
                                                             unsigned char*       response,
                                                             size_t               maxResponseSize)
{
    // This is called from the remote display thread:

    actionQueue.addAndBroadcast(new ActionQueue::GetPasswordItem());
    passwordRetrievalBarrier.synchronize();  // wait here until main thread posts the retrieved password

    const bool result = ( (retrievedPassword.size() > 0) &&
                          this->RFBProtocol::encryptChallenge( challenge, challengeSize,
                                                               (const unsigned char*)retrievedPassword.c_str(),
                                                               response, maxResponseSize ) );
    VncManager::eraseStringContents(retrievedPassword);

    return result;
}



void VncManager::RFBProtocolImplementation::postRetrievedPassword(const char* theRetrievedPassword)
{
    // This is called from the main thread:

    retrievedPassword = (theRetrievedPassword ? theRetrievedPassword : "");
    passwordRetrievalBarrier.synchronize();  // release waiting remote display thread in VncManager::RFBProtocolImplementation::encryptChallenge()
}



//----------------------------------------------------------------------

void VncManager::UIPasswordRetrievalCompletionThunk::postPassword(const char* theRetrievedPassword)
{
    // Note: Slave nodes will have a NULL rfbProto, and they should not
    // be calling postPassword() anyway.  All they do is show the UI
    // which basically mimics the master node's UI.  Also, by retrieving
    // the rfbProto here (when it is finally needed), we are protected
    // from the situation where a password retrieval is initiated but
    // then the rfbProto is torn down before the user finishes authentication.

    RFBProtocolImplementation* const rfbProto = vncManager.getRfbProto();
    if (rfbProto)
        rfbProto->postRetrievedPassword(theRetrievedPassword);
}



//----------------------------------------------------------------------
// VncManager::VncManager static data members and methods

const rfbPixelFormat VncManager::DefaultRequestedPixelFormat =  // static member
{
     32,  // rfbCARD8  bitsPerPixel;  /* 8,16,32 only */

     24,  // rfbCARD8  depth;         /* 8 to 32 */

      1,  // rfbCARD8  bigEndian;     /* True if multi-byte pixels are interpreted
          //                             as big endian, or if single-bit-per-pixel
          //                             has most significant bit of the byte
          //                             corresponding to first (leftmost) pixel. Of
          //                             course this is meaningless for 8 bits/pix */

      1,  // rfbCARD8  trueColour;    /* If false then we need a "colour map" to
          //                             convert pixels to RGB.  If true, xxxMax and
          //                             xxxShift specify bits used for red, green
          //                             and blue */


          // /* the following fields are only meaningful if trueColour is true */

    255,  // rfbCARD16 redMax;        /* maximum red value (= 2^n - 1 where n is the
          //                             number of bits used for red). Note this
          //                             value is always in big endian order. */

    255,  // rfbCARD16 greenMax;      /* similar for green */

    255,  // rfbCARD16 blueMax;       /* and blue */

     16,  // rfbCARD8  redShift;      /* number of shifts needed to get the red
          //                             value in a pixel to the least significant
          //                             bit. To find the red value from a given
          //                             pixel, do the following:
          //                             1) Swap pixel value according to bigEndian
          //                                (e.g. if bigEndian is false and host byte
          //                                order is big endian, then swap).
          //                             2) Shift right by redShift.
          //                             3) AND with redMax (in host byte order).
          //                             4) You now have the red value between 0 and
          //                                redMax. */

      8,  // rfbCARD8  greenShift;    /* similar for green */

      0,  // rfbCARD8  blueShift;     /* and blue */

      0,  // rfbCARD8  pad1;
      0   // rfbCARD16 pad2;
};



VncManager::~VncManager()
{
    shutdown();
}



void VncManager::initiatePasswordRetrieval()
{
    passwordRetrievalThunk.getPassword(passwordRetrievalCompletionThunk);
}



void VncManager::startup(const RFBProtocolStartupData& rfbProtocolStartupData)
{
    shutdown();

    if (isSlave)
    {
        remoteCommThreadStarted = true;
        remoteCommThread.start(&actionQueue, &ActionQueue::threadStartForSlaveNodes);
    }
    else
    {
        rfbProto = new RFBProtocolImplementation(*this, actionQueue);
        remoteCommThreadStarted = true;
        remoteCommThread.start(rfbProto, &RFBProtocolImplementation::threadStartForMasterNode, rfbProtocolStartupData);
    }
}



void VncManager::shutdown()
{
    RFBProtocolImplementation* const rp = rfbProto;
    rfbProto = 0;  // make sure nothing uses this while we're closing
    if (rp)
    {
        // Note: rfbProto is always 0 for slave nodes in a cluster environment, so this code is only executed by master nodes
        rp->close();
        usleep(250000);  // allow cluster slave nodes to respond to the infoCloseCompleted sent by rp->close()
    }

    if (remoteCommThreadStarted)
    {
        remoteCommThread.cancel();

        if (!remoteCommThread.isJoined())
            remoteCommThread.join();

        remoteCommThreadStarted = false;
    }

    // We wait to delete rp (which was the value of rfbProto) until
    // after joining the remoteCommThread so we don't pull the rug
    // out from any pending operation on the remoteCommThread.
    if (rp)
        delete rp;

    remoteDisplay.close();
}



bool VncManager::performQueuedActions()
{
    return actionQueue.performQueuedActions(*this);
}



void VncManager::drawRemoteDisplaySurface( GLfloat x00, GLfloat y00, GLfloat z00,
                                           GLfloat x10, GLfloat y10, GLfloat z10,
                                           GLfloat x11, GLfloat y11, GLfloat z11  ) const
{
    glPushAttrib(GL_TEXTURE_BIT);
    GLint lightModelColorControl;
    glGetIntegerv(GL_LIGHT_MODEL_COLOR_CONTROL, &lightModelColorControl);

    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
    glEnable(GL_TEXTURE_2D);

    (void)remoteDisplay.displayInRectangle( x00, y00, z00,
                                            x10, y10, z10,
                                            x11, y11, z11 );

    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, lightModelColorControl);
    glPopAttrib();
}



bool VncManager::sendFramebufferUpdateRequest(int x, int y, size_t w, size_t h, bool incremental)
{
    return (rfbProto != 0) && rfbProto->sendFramebufferUpdateRequest(x, y, w, h, incremental);
}



bool VncManager::sendKeyEvent(rfbCARD32 key, bool down)
{
    return (rfbProto != 0) && rfbProto->sendKeyEvent(key, down);
}



bool VncManager::sendPointerEvent(int x, int y, int buttonMask)
{
    return (rfbProto != 0) && rfbProto->sendPointerEvent(x, y, buttonMask);
}



bool VncManager::sendClientCutText(const char* str, size_t len)
{
    return (rfbProto != 0) && rfbProto->sendClientCutText(str, len);
}



bool VncManager::sendStringViaKeyEvents( const char* str,
                                         size_t      len,
                                         rfbCARD32   tabKeySym,
                                         rfbCARD32   enterKeySym,
                                         rfbCARD32   leftControlKeySym )
{
    return (rfbProto != 0) && rfbProto->sendStringViaKeyEvents(str, len, tabKeySym, enterKeySym, leftControlKeySym);
}



}  // end of namespace Voltaic
