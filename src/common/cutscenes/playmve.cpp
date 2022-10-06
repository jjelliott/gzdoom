/*
 * InterplayDecoder
 * Copyright (C) 2020 sirlemonhead
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* This code is based on interplayvideo.c, dpcm.c and ipmovie.c from the FFmpeg project which can be obtained
 * from http://www.ffmpeg.org/. Below is the license from interplayvideo.c
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Interplay MVE Video Decoder
 * Copyright (C) 2003 The FFmpeg project
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#include <algorithm>
#include "playmve.h"
#include "printf.h"
#include "v_draw.h"
#include "s_music.h"
#include "cmdlib.h"




static const int16_t delta_table[] = {
         0,      1,      2,      3,      4,      5,      6,      7,
         8,      9,     10,     11,     12,     13,     14,     15,
        16,     17,     18,     19,     20,     21,     22,     23,
        24,     25,     26,     27,     28,     29,     30,     31,
        32,     33,     34,     35,     36,     37,     38,     39,
        40,     41,     42,     43,     47,     51,     56,     61,
        66,     72,     79,     86,     94,    102,    112,    122,
       133,    145,    158,    173,    189,    206,    225,    245,
       267,    292,    318,    348,    379,    414,    452,    493,
       538,    587,    640,    699,    763,    832,    908,    991,
      1081,   1180,   1288,   1405,   1534,   1673,   1826,   1993,
      2175,   2373,   2590,   2826,   3084,   3365,   3672,   4008,
      4373,   4772,   5208,   5683,   6202,   6767,   7385,   8059,
      8794,   9597,  10472,  11428,  12471,  13609,  14851,  16206,
     17685,  19298,  21060,  22981,  25078,  27367,  29864,  32589,
    -29973, -26728, -23186, -19322, -15105, -10503,  -5481,     -1,
         1,      1,   5481,  10503,  15105,  19322,  23186,  26728,
     29973, -32589, -29864, -27367, -25078, -22981, -21060, -19298,
    -17685, -16206, -14851, -13609, -12471, -11428, -10472,  -9597,
     -8794,  -8059,  -7385,  -6767,  -6202,  -5683,  -5208,  -4772,
     -4373,  -4008,  -3672,  -3365,  -3084,  -2826,  -2590,  -2373,
     -2175,  -1993,  -1826,  -1673,  -1534,  -1405,  -1288,  -1180,
     -1081,   -991,   -908,   -832,   -763,   -699,   -640,   -587,
      -538,   -493,   -452,   -414,   -379,   -348,   -318,   -292,
      -267,   -245,   -225,   -206,   -189,   -173,   -158,   -145,
      -133,   -122,   -112,   -102,    -94,    -86,    -79,    -72,
       -66,    -61,    -56,    -51,    -47,    -43,    -42,    -41,
       -40,    -39,    -38,    -37,    -36,    -35,    -34,    -33,
       -32,    -31,    -30,    -29,    -28,    -27,    -26,    -25,
       -24,    -23,    -22,    -21,    -20,    -19,    -18,    -17,
       -16,    -15,    -14,    -13,    -12,    -11,    -10,     -9,
        -8,     -7,     -6,     -5,     -4,     -3,     -2,     -1
};


// macros to fetch little-endian words from a bytestream
#define LE_16(x)  ((uint16_t)((*(x)) | ((*((x)+1)) << 8)))
#define LE_32(x)  (LE_16(x) | ((uint32_t)LE_16(x+2) << 16))
#define LE_64(x)  (LE_32(x) | ((uint64_t)LE_32(x+4) << 32))

static bool StreamCallbackFunc(SoundStream* stream, void* buff, int len, void* userdata)
{
    InterplayDecoder* pId = (InterplayDecoder*)userdata;
    memcpy(buff, &pId->audio.samples[pId->audio.nRead], len);
    pId->audio.nRead += len / 2;
    if (pId->audio.nRead >= (int)countof(pId->audio.samples)) pId->audio.nRead = 0;
    return true;
}

InterplayDecoder::InterplayDecoder(bool soundenabled)
{
    bIsPlaying = false;
    bAudioStarted = !soundenabled;  // This prevents the stream from getting created

    nWidth  = 0;
    nHeight = 0;
    nFrame  = 0;

    memset(palette, 0, sizeof(palette));
    memset(&audio, 0, sizeof(audio));
    audio.nRead = 18000;    // skip the initial silence. This is needed to sync audio and video because OpenAL's lag is a bit on the high side.


    nFps = 0.0;
    nFrameDuration = 0;
    nTimerRate = 0;
    nTimerDiv  = 0;

    pVideoBuffers[0] = nullptr;
    pVideoBuffers[1] = nullptr;

    decodeMap.pData = nullptr;
    decodeMap.nSize = 0;

    nCurrentVideoBuffer  = 0;
    nPreviousVideoBuffer = 1;

    videoStride = 0;
}

InterplayDecoder::~InterplayDecoder()
{
    Close();
}

void InterplayDecoder::SwapFrames()
{
    int t = nPreviousVideoBuffer;
    nPreviousVideoBuffer = nCurrentVideoBuffer;
    nCurrentVideoBuffer = t;
}

void InterplayDecoder::Close()
{
    fr.Close();
    bIsPlaying = false;
    if (stream)
        S_StopCustomStream(stream);
    stream = nullptr;

    if (decodeMap.pData) {
        delete[] decodeMap.pData;
        decodeMap.pData = nullptr;
    }

    if (pVideoBuffers[0]) {
        delete[] pVideoBuffers[0];
        pVideoBuffers[0] = nullptr;
    }
    if (pVideoBuffers[1]) {
        delete[] pVideoBuffers[1];
        pVideoBuffers[1] = nullptr;
    }

}

bool InterplayDecoder::Open(FileReader &fr_)
{
    // open the file (read only)
    char lsig[20];

    // check the file signature
    fr_.Read((uint8_t*)lsig, sizeof(lsig));
    if (memcmp(lsig, "Interplay MVE File\x1A\0", sizeof(lsig)) != 0)
    {
        Printf(TEXTCOLOR_RED "InterplayDecoder: Unknown MVE signature\n ");
        return false;
    }

    // skip the next 6 bytes
    fr_.Seek(6, FileReader::SeekCur);
    fr = std::move(fr_);

    //Run();

    return true;
}

bool InterplayDecoder::RunFrame(uint64_t clock)
{
    uint8_t chunkPreamble[CHUNK_PREAMBLE_SIZE];
    uint8_t opcodePreamble[OPCODE_PREAMBLE_SIZE];
    uint8_t opcodeType;
    uint8_t opcodeVersion;
    int opcodeSize, chunkSize;
    int chunkType = 0;

    // iterate through the chunks in the file
    do
    {
        // handle timing - wait until we're ready to process the next frame.
        if (nNextFrameTime > clock) {
            return true;
        }
        else {
            nNextFrameTime += nFrameDuration;
        }

        if (fr.Read(chunkPreamble, CHUNK_PREAMBLE_SIZE) != CHUNK_PREAMBLE_SIZE) {
            Printf(TEXTCOLOR_RED "InterplayDecoder: could not read from file (EOF?)\n");
            return false;
        }

        chunkSize = LE_16(&chunkPreamble[0]);
        chunkType = LE_16(&chunkPreamble[2]);

        ChunkData.resize(chunkSize);
        if (fr.Read(ChunkData.data(), chunkSize) != chunkSize) {
            Printf(TEXTCOLOR_RED "InterplayDecoder: could not read from file (EOF?)\n");
            return false;
        }

        // iterate through individual opcodes
        ChunkPtr = ChunkData.data();
        while (chunkSize > 0 && chunkType != CHUNK_BAD)
        {
            if (chunkSize < OPCODE_PREAMBLE_SIZE)
            {
                Printf(TEXTCOLOR_RED "InterplayDecoder: opcode size too small\n");
                return false;
            }
            opcodeSize = LE_16(ChunkPtr);
            opcodeType = ChunkPtr[2];
            opcodeVersion = ChunkPtr[3];

            ChunkPtr += OPCODE_PREAMBLE_SIZE;
            chunkSize -= OPCODE_PREAMBLE_SIZE;
            if (chunkSize < opcodeSize)
            {
                Printf(TEXTCOLOR_RED "InterplayDecoder: opcode size too large for chunk\n");
                return false;
            }
            chunkSize -= opcodeSize;

            switch (opcodeType)
            {
            case OPCODE_END_OF_STREAM:
            {
                ChunkPtr += opcodeSize;
                break;
            }

            case OPCODE_END_OF_CHUNK:
            {
                ChunkPtr += opcodeSize;
                break;
            }

            case OPCODE_CREATE_TIMER:
            {
                nTimerRate = LE_32(ChunkPtr);
                nTimerDiv  = LE_16(ChunkPtr+4);
                ChunkPtr += 6;
                nFrameDuration = ((uint64_t)nTimerRate * nTimerDiv) * 1000;
                break;
            }

            case OPCODE_INIT_AUDIO_BUFFERS:
            {
                // Skip 2 bytes
                uint16_t flags = LE_16(ChunkPtr+2);
                audio.nSampleRate = LE_16(ChunkPtr+4);
                ChunkPtr += 6;

                uint32_t nBufferBytes = (opcodeVersion == 0) ? LE_16(ChunkPtr) : LE_32(ChunkPtr);
                ChunkPtr += (opcodeVersion == 0) ? 2 : 4;

                audio.nChannels = (flags & 0x1) ? 2 : 1;
                audio.nBitDepth = (flags & 0x2) ? 16 : 8;
                audio.bCompressed = (opcodeVersion > 0 && (flags & 0x4));
                break;
            }

            case OPCODE_START_STOP_AUDIO:
            {
                if (!bAudioStarted)
                {
                    S_StopMusic(true);
                    // start audio playback
                    stream = S_CreateCustomStream(6000, audio.nSampleRate, audio.nChannels, MusicSamples16bit, StreamCallbackFunc, this);
                    bAudioStarted = true;
                }

                ChunkPtr += opcodeSize;
                break;
            }

            case OPCODE_INIT_VIDEO_BUFFERS:
            {
                assert(((opcodeVersion == 0 && opcodeSize >= 4) ||
                    (opcodeVersion == 1 && opcodeSize >= 6) ||
                    (opcodeVersion == 2 && opcodeSize >= 8)) &&
                    opcodeSize <= 8);

                nWidth  = LE_16(ChunkPtr) * 8;
                nHeight = LE_16(ChunkPtr+2) * 8;

                int count, truecolour;
                if (opcodeVersion > 0)
                {
                    count = LE_16(ChunkPtr+4);
                    if (opcodeVersion > 1)
                    {
                        truecolour = LE_16(ChunkPtr+6);
                        assert(truecolour == 0);
                    }
                }
                ChunkPtr += opcodeSize;

                pVideoBuffers[0] = new uint8_t[nWidth * nHeight];
                pVideoBuffers[1] = new uint8_t[nWidth * nHeight];

                videoStride = nWidth;

                animtex.SetSize(AnimTexture::Paletted, nWidth, nHeight);
                break;
            }

            case OPCODE_UNKNOWN_06:
            case OPCODE_UNKNOWN_0E:
            case OPCODE_UNKNOWN_10:
            case OPCODE_UNKNOWN_12:
            case OPCODE_UNKNOWN_13:
            case OPCODE_UNKNOWN_14:
            case OPCODE_UNKNOWN_15:
            {
                ChunkPtr += opcodeSize;
                break;
            }

            case OPCODE_SEND_BUFFER:
            {
                int nPalStart = LE_16(ChunkPtr);
                int nPalCount = LE_16(ChunkPtr+2);
                ChunkPtr += opcodeSize;

                animtex.SetFrame(&palette[0].r , GetCurrentFrame());

                nFrame++;
                SwapFrames();
                break;
            }

            case OPCODE_AUDIO_FRAME:
            {
                auto pStart = ChunkPtr;
                uint16_t seqIndex   = LE_16(ChunkPtr);
                uint16_t streamMask = LE_16(ChunkPtr+2);
                uint16_t nSamples   = LE_16(ChunkPtr+4); // number of samples this chunk(?)
                ChunkPtr += 6;

                // We only bother with stream 0
                if (!(streamMask & 1))
                {
                    ChunkPtr += opcodeSize - 6;
                    break;
                }

                nSamples = opcodeSize - 6;
                if (audio.bCompressed)
                {
                    int predictor[2];

                    for (int ch = 0; ch < audio.nChannels; ch++)
                    {
                        predictor[ch] = (int16_t)LE_16(ChunkPtr);
                        ChunkPtr += 2;

                        audio.samples[audio.nWrite++] = predictor[ch];
                        if (audio.nWrite >= (int)countof(audio.samples)) audio.nWrite = 0;
                    }

                    bool stereo = audio.nChannels == 2;
                    nSamples -= 2*audio.nChannels;
                    nSamples &= ~(int)stereo;

                    int ch = 0;
                    for (int i = 0; i < nSamples;)
                    {
                        int todo = std::min(nSamples-i, (int)std::size(audio.samples)-audio.nWrite);
                        auto end = ChunkPtr + todo;
                        while(ChunkPtr != end)
                        {
                            predictor[ch] += delta_table[*ChunkPtr++];
                            predictor[ch] = clamp(predictor[ch], -32768, 32767);

                            audio.samples[audio.nWrite++] = predictor[ch];

                            // toggle channel
                            ch ^= stereo;
                        }
                        if (audio.nWrite >= (int)countof(audio.samples)) audio.nWrite = 0;
                        i += todo;
                    }
                }
                else if (audio.nBitDepth == 8)
                {
                    for (int i = 0; i < nSamples;)
                    {
                        int todo = std::min(nSamples-i, (int)std::size(audio.samples)-audio.nWrite);
                        auto end = ChunkPtr + todo;
                        while(ChunkPtr != end)
                            audio.samples[audio.nWrite++] = ((*ChunkPtr++)-128) << 8;
                        if (audio.nWrite >= (int)countof(audio.samples)) audio.nWrite = 0;
                        i += todo;
                    }
                }
                else
                {
                    nSamples /= 2;
                    for (int i = 0; i < nSamples;)
                    {
                        int todo = std::min(nSamples-i, (int)std::size(audio.samples)-audio.nWrite);
                        auto end = ChunkPtr + todo*2;
                        while(ChunkPtr != end)
                        {
                            audio.samples[audio.nWrite++] = (int16_t)LE_16(ChunkPtr);
                            ChunkPtr += 2;
                        }
                        if (audio.nWrite >= (int)countof(audio.samples)) audio.nWrite = 0;
                        i += todo;
                    }
                }

                auto pEnd = ChunkPtr;
                int nRead = (int)(pEnd - pStart);
                assert(opcodeSize == nRead);
                break;
            }

            case OPCODE_SILENCE_FRAME:
            {
                uint16_t seqIndex = LE_16(ChunkPtr);
                uint16_t streamMask = LE_16(ChunkPtr+2);
                uint16_t nStreamLen = LE_16(ChunkPtr+4);
                ChunkPtr += 6;

                if (streamMask & 1)
                {
                    nStreamLen = (opcodeSize-6) * 8 / audio.nBitDepth;

                    for (int i = 0; i < nStreamLen;)
                    {
                        int todo = std::min(nStreamLen-i, (int)std::size(audio.samples)-audio.nWrite);
                        memset(&audio.samples[audio.nWrite], 0, todo*2);
                        audio.nWrite += todo;
                        if (audio.nWrite >= (int)countof(audio.samples)) audio.nWrite = 0;
                        i += todo;
                    }
                }

                break;
            }

            case OPCODE_INIT_VIDEO_MODE:
            {
                ChunkPtr += opcodeSize;
                break;
            }

            case OPCODE_CREATE_GRADIENT:
            {
                ChunkPtr += opcodeSize;
                Printf("InterplayDecoder: Create gradient not supported.\n");
                break;
            }

            case OPCODE_SET_PALETTE:
            {
                if (opcodeSize > 0x304 || opcodeSize < 4) {
                    Printf("set_palette opcode with invalid size\n");
                    chunkType = CHUNK_BAD;
                    break;
                }

                int nPalStart = LE_16(ChunkPtr);
                int nPalEnd = nPalStart + LE_16(ChunkPtr+2) - 1;
                ChunkPtr += 4;
                if (nPalStart > 255 || nPalEnd > 255) {
                    Printf("set_palette indices out of range (%d -> %d)\n", nPalStart, nPalEnd);
                    chunkType = CHUNK_BAD;
                    break;
                }
                for (int i = nPalStart; i <= nPalEnd; i++)
                {
                    palette[i].r = (*ChunkPtr++) << 2;
                    palette[i].g = (*ChunkPtr++) << 2;
                    palette[i].b = (*ChunkPtr++) << 2;
                    palette[i].r |= palette[i].r >> 6;
                    palette[i].g |= palette[i].g >> 6;
                    palette[i].b |= palette[i].b >> 6;
                }
                break;
            }

            case OPCODE_SET_PALETTE_COMPRESSED:
            {
                ChunkPtr += opcodeSize;
                Printf("InterplayDecoder: Set palette compressed not supported.\n");
                break;
            }

            case OPCODE_SET_DECODING_MAP:
            {
                if (!decodeMap.pData)
                {
                    decodeMap.pData = new uint8_t[opcodeSize];
                    decodeMap.nSize = opcodeSize;
                }
                else
                {
                    if (opcodeSize != (int)decodeMap.nSize) {
                        delete[] decodeMap.pData;
                        decodeMap.pData = new uint8_t[opcodeSize];
                        decodeMap.nSize = opcodeSize;
                    }
                }

                memcpy(decodeMap.pData, ChunkPtr, opcodeSize);
                ChunkPtr += opcodeSize;
                break;
            }

            case OPCODE_VIDEO_DATA:
            {
                auto pStart = ChunkPtr;

                // need to skip 14 bytes
                ChunkPtr += 14;

                if (decodeMap.nSize)
                {
                    int i = 0;

                    for (uint32_t y = 0; y < nHeight; y += 8)
                    {
                        for (uint32_t x = 0; x < nWidth; x += 8)
                        {
                            uint32_t opcode;

                            // alternate between getting low and high 4 bits
                            if (i & 1) {
                                opcode = decodeMap.pData[i >> 1] >> 4;
                            }
                            else {
                                opcode = decodeMap.pData[i >> 1] & 0x0F;
                            }
                            i++;

                            int32_t offset = x + (y * videoStride);

                            switch (opcode)
                            {
                                default:
                                    break;
                                case 0:
                                    DecodeBlock0(offset);
                                    break;
                                case 1:
                                    DecodeBlock1(offset);
                                    break;
                                case 2:
                                    DecodeBlock2(offset);
                                    break;
                                case 3:
                                    DecodeBlock3(offset);
                                    break;
                                case 4:
                                    DecodeBlock4(offset);
                                    break;
                                case 5:
                                    DecodeBlock5(offset);
                                    break;
                                case 7:
                                    DecodeBlock7(offset);
                                    break;
                                case 8:
                                    DecodeBlock8(offset);
                                    break;
                                case 9:
                                    DecodeBlock9(offset);
                                    break;
                                case 10:
                                    DecodeBlock10(offset);
                                    break;
                                case 11:
                                    DecodeBlock11(offset);
                                    break;
                                case 12:
                                    DecodeBlock12(offset);
                                    break;
                                case 13:
                                    DecodeBlock13(offset);
                                    break;
                                case 14:
                                    DecodeBlock14(offset);
                                    break;
                                case 15:
                                    DecodeBlock15(offset);
                                    break;
                            }
                        }
                    }
                }

                auto pEnd = ChunkPtr;
                int nSkipBytes = opcodeSize - (int)(pEnd - pStart); // we can end up with 1 byte left we need to skip
                assert(nSkipBytes <= 1);

                ChunkPtr += nSkipBytes;
                break;
            }

            default:
                break;
            }
        }

    }
    while (chunkType < CHUNK_VIDEO && bIsPlaying);
    return chunkType != CHUNK_END;
}

void InterplayDecoder::CopyBlock(uint8_t* pDest, uint8_t* pSrc)
{
    for (int y = 0; y < 8; y++)
    {
        memcpy(pDest, pSrc, 8);
        pSrc  += (intptr_t)videoStride;
        pDest += (intptr_t)videoStride;
    }
}

void InterplayDecoder::DecodeBlock0(int32_t offset)
{
    // copy from the same offset but from the previous frame
    uint8_t* pDest = GetCurrentFrame() + (intptr_t)offset;
    uint8_t* pSrc = GetPreviousFrame() + (intptr_t)offset;

    CopyBlock(pDest, pSrc);
}

void InterplayDecoder::DecodeBlock1(int32_t offset)
{
    // nothing to do for this.
}

void InterplayDecoder::DecodeBlock2(int32_t offset)
{
    // copy block from 2 frames ago using a motion vector; need 1 more byte
    uint8_t B = *ChunkPtr++;

    int x, y;

    if (B < 56) {
        x = 8 + (B % 7);
        y = B / 7;
    }
    else {
        x = -14 + ((B - 56) % 29);
        y = 8 + ((B - 56) / 29);
    }

    uint8_t* pDest = GetCurrentFrame() + (intptr_t)offset;
    uint8_t* pSrc  = GetCurrentFrame() + (intptr_t)(int64_t)offset + (int64_t)x + (int64_t(y) * (int64_t)videoStride);

    CopyBlock(pDest, pSrc);
}

void InterplayDecoder::DecodeBlock3(int32_t offset)
{
    // copy 8x8 block from current frame from an up/left block
    uint8_t B = *ChunkPtr++;

    int x, y;

    // need 1 more byte for motion
    if (B < 56) {
        x = -(8 + (B % 7));
        y = -(B / 7);
    }
    else {
        x = -(-14 + ((B - 56) % 29));
        y = -(8 + ((B - 56) / 29));
    }

    uint8_t* pDest = GetCurrentFrame() + (intptr_t)offset;
    uint8_t* pSrc  = GetCurrentFrame() + (intptr_t)(int64_t)offset + (int64_t)x + (int64_t(y) * (int64_t)videoStride);

    CopyBlock(pDest, pSrc);
}

void InterplayDecoder::DecodeBlock4(int32_t offset)
{
    // copy a block from the previous frame; need 1 more byte
    int x, y;
    uint8_t B, BL, BH;

    B = *ChunkPtr++;

    BL = B & 0x0F;
    BH = (B >> 4) & 0x0F;
    x = -8 + BL;
    y = -8 + BH;

    uint8_t* pDest = GetCurrentFrame() + (intptr_t)offset;
    uint8_t* pSrc = GetPreviousFrame() + (intptr_t)(int64_t)offset + (int64_t)x + (int64_t(y) * (int64_t)videoStride);

    CopyBlock(pDest, pSrc);
}

void InterplayDecoder::DecodeBlock5(int32_t offset)
{
    // copy a block from the previous frame using an expanded range; need 2 more bytes
    int8_t x = *ChunkPtr++;
    int8_t y = *ChunkPtr++;

    uint8_t* pDest = GetCurrentFrame() + (intptr_t)offset;
    uint8_t* pSrc = GetPreviousFrame() + (intptr_t)(int64_t)offset + (int64_t)x + (int64_t(y) * (int64_t)videoStride);

    CopyBlock(pDest, pSrc);
}

// Block6 is unknown and skipped

void InterplayDecoder::DecodeBlock7(int32_t offset)
{
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;
    uint32_t flags = 0;

    uint8_t P[2];
    P[0] = *ChunkPtr++;
    P[1] = *ChunkPtr++;

    // 2-color encoding
    if (P[0] <= P[1])
    {
        // need 8 more bytes from the stream
        for (int y = 0; y < 8; y++)
        {
            flags = (*ChunkPtr++) | 0x100;
            for (; flags != 1; flags >>= 1) {
                *pBuffer++ = P[flags & 1];
            }
            pBuffer += (videoStride - 8);
        }
    }
    else
    {
        // need 2 more bytes from the stream
        flags = LE_16(ChunkPtr);
        ChunkPtr += 2;

        for (int y = 0; y < 8; y += 2)
        {
            for (int x = 0; x < 8; x += 2, flags >>= 1)
            {
                pBuffer[x] =
                    pBuffer[x + 1] =
                    pBuffer[x + videoStride] =
                    pBuffer[x + 1 + videoStride] = P[flags & 1];
            }
            pBuffer += videoStride * 2;
        }
    }
}

void InterplayDecoder::DecodeBlock8(int32_t offset)
{
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;
    uint32_t flags = 0;
    uint8_t P[4];

    // 2-color encoding for each 4x4 quadrant, or 2-color encoding on either top and bottom or left and right halves
    P[0] = *ChunkPtr++;
    P[1] = *ChunkPtr++;

    if (P[0] <= P[1])
    {
        for (int y = 0; y < 16; y++)
        {
            // new values for each 4x4 block
            if (!(y & 3))
            {
                if (y) {
                    P[0] = *ChunkPtr++;
                    P[1] = *ChunkPtr++;
                }
                flags = LE_16(ChunkPtr);
                ChunkPtr += 2;
            }

            for (int x = 0; x < 4; x++, flags >>= 1) {
                *pBuffer++ = P[flags & 1];
            }

            pBuffer += videoStride - 4;
            // switch to right half
            if (y == 7) pBuffer -= 8 * videoStride - 4;
        }
    }
    else
    {
        flags = LE_32(ChunkPtr);
        ChunkPtr += 4;
        P[2] = *ChunkPtr++;
        P[3] = *ChunkPtr++;

        if (P[2] <= P[3])
        {
            // vertical split; left & right halves are 2-color encoded
            for (int y = 0; y < 16; y++)
            {
                for (int x = 0; x < 4; x++, flags >>= 1) {
                    *pBuffer++ = P[flags & 1];
                }

                pBuffer += videoStride - 4;

                // switch to right half
                if (y == 7) {
                    pBuffer -= 8 * videoStride - 4;
                    P[0] = P[2];
                    P[1] = P[3];
                    flags = LE_32(ChunkPtr);
                    ChunkPtr += 4;
                }
            }
        }
        else
        {
            // horizontal split; top & bottom halves are 2-color encoded
            for (int y = 0; y < 8; y++)
            {
                if (y == 4) {
                    P[0] = P[2];
                    P[1] = P[3];
                    flags = LE_32(ChunkPtr);
                    ChunkPtr += 4;
                }

                for (int x = 0; x < 8; x++, flags >>= 1)
                    *pBuffer++ = P[flags & 1];

                pBuffer += (videoStride - 8);
            }
        }
    }
}

void InterplayDecoder::DecodeBlock9(int32_t offset)
{
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;
    uint8_t P[4];

    memcpy(P, ChunkPtr, 4);
    ChunkPtr += 4;

    // 4-color encoding
    if (P[0] <= P[1])
    {
        if (P[2] <= P[3])
        {
            // 1 of 4 colors for each pixel, need 16 more bytes
            for (int y = 0; y < 8; y++)
            {
                // get the next set of 8 2-bit flags
                int flags = LE_16(ChunkPtr);
                ChunkPtr += 2;

                for (int x = 0; x < 8; x++, flags >>= 2) {
                    *pBuffer++ = P[flags & 0x03];
                }

                pBuffer += (videoStride - 8);
            }
        }
        else
        {
            // 1 of 4 colors for each 2x2 block, need 4 more bytes
            uint32_t flags = LE_32(ChunkPtr);
            ChunkPtr += 4;

            for (int y = 0; y < 8; y += 2)
            {
                for (int x = 0; x < 8; x += 2, flags >>= 2)
                {
                    pBuffer[x] =
                        pBuffer[x + 1] =
                        pBuffer[x + videoStride] =
                        pBuffer[x + 1 + videoStride] = P[flags & 0x03];
                }

                pBuffer += videoStride * 2;
            }
        }
    }
    else
    {
        // 1 of 4 colors for each 2x1 or 1x2 block, need 8 more bytes
        uint64_t flags = LE_64(ChunkPtr);
        ChunkPtr += 8;

        if (P[2] <= P[3])
        {
            for (int y = 0; y < 8; y++)
            {
                for (int x = 0; x < 8; x += 2, flags >>= 2)
                {
                    pBuffer[x] =
                        pBuffer[x + 1] = P[flags & 0x03];
                }
                pBuffer += videoStride;
            }
        }
        else
        {
            for (int y = 0; y < 8; y += 2)
            {
                for (int x = 0; x < 8; x++, flags >>= 2)
                {
                    pBuffer[x] =
                        pBuffer[x + videoStride] = P[flags & 0x03];
                }
                pBuffer += videoStride * 2;
            }
        }
    }
}

void InterplayDecoder::DecodeBlock10(int32_t offset)
{
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;
    uint8_t P[8];

    memcpy(P, ChunkPtr, 4);
    ChunkPtr += 4;

    // 4-color encoding for each 4x4 quadrant, or 4-color encoding on either top and bottom or left and right halves
    if (P[0] <= P[1])
    {
        int flags = 0;

        // 4-color encoding for each quadrant; need 32 bytes
        for (int y = 0; y < 16; y++)
        {
            // new values for each 4x4 block
            if (!(y & 3)) {
                if (y) {
                    memcpy(P, ChunkPtr, 4);
                    ChunkPtr += 4;
                }
                flags = LE_32(ChunkPtr);
                ChunkPtr += 4;
            }

            for (int x = 0; x < 4; x++, flags >>= 2) {
                *pBuffer++ = P[flags & 0x03];
            }

            pBuffer += videoStride - 4;
            // switch to right half
            if (y == 7) pBuffer -= 8 * videoStride - 4;
        }
    }
    else
    {
        // vertical split?
        int vert;
        uint64_t flags = LE_64(ChunkPtr);
        ChunkPtr += 8;

        memcpy(P + 4, ChunkPtr, 4);
        ChunkPtr += 4;
        vert = P[4] <= P[5];

        // 4-color encoding for either left and right or top and bottom halves
        for (int y = 0; y < 16; y++)
        {
            for (int x = 0; x < 4; x++, flags >>= 2)
                *pBuffer++ = P[flags & 0x03];

            if (vert)
            {
                pBuffer += videoStride - 4;
                // switch to right half
                if (y == 7) pBuffer -= 8 * videoStride - 4;
            }
            else if (y & 1) pBuffer += (videoStride - 8);

            // load values for second half
            if (y == 7) {
                memcpy(P, P + 4, 4);
                flags = LE_64(ChunkPtr);
                ChunkPtr += 8;
            }
        }
    }
}

void InterplayDecoder::DecodeBlock11(int32_t offset)
{
    // 64-color encoding (each pixel in block is a different color)
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;

    for (int y = 0; y < 8; y++)
    {
        memcpy(pBuffer, ChunkPtr, 8);
        ChunkPtr += 8;
        pBuffer += videoStride;
    }
}

void InterplayDecoder::DecodeBlock12(int32_t offset)
{
    // 16-color block encoding: each 2x2 block is a different color
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;

    for (int y = 0; y < 8; y += 2)
    {
        for (int x = 0; x < 8; x += 2)
        {
            pBuffer[x] =
                pBuffer[x + 1] =
                pBuffer[x + videoStride] =
                pBuffer[x + 1 + videoStride] = *ChunkPtr++;
        }
        pBuffer += videoStride * 2;
    }
}

void InterplayDecoder::DecodeBlock13(int32_t offset)
{
    // 4-color block encoding: each 4x4 block is a different color
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;
    uint8_t P[2] = {};

    for (int y = 0; y < 8; y++)
    {
        if (!(y & 3))
        {
            P[0] = *ChunkPtr++;
            P[1] = *ChunkPtr++;
        }

        memset(pBuffer,     P[0], 4);
        memset(pBuffer + 4, P[1], 4);
        pBuffer += videoStride;
    }
}

void InterplayDecoder::DecodeBlock14(int32_t offset)
{
    // 1-color encoding : the whole block is 1 solid color
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;
    uint8_t pix = *ChunkPtr++;

    for (int y = 0; y < 8; y++)
    {
        memset(pBuffer, pix, 8);
        pBuffer += videoStride;
    }
}

void InterplayDecoder::DecodeBlock15(int32_t offset)
{
    // dithered encoding
    uint8_t* pBuffer = GetCurrentFrame() + (intptr_t)offset;
    uint8_t P[2];

    P[0] = *ChunkPtr++;
    P[1] = *ChunkPtr++;

    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x += 2)
        {
            *pBuffer++ = P[y & 1];
            *pBuffer++ = P[!(y & 1)];
        }
        pBuffer += (videoStride - 8);
    }
}

uint8_t* InterplayDecoder::GetCurrentFrame()
{
    return pVideoBuffers[nCurrentVideoBuffer];
}

uint8_t* InterplayDecoder::GetPreviousFrame()
{
    return pVideoBuffers[nPreviousVideoBuffer];
}
