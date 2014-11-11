/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "VBRISeeker"
#include <utils/Log.h>

#include "include/VBRISeeker.h"

#include "include/avc_utils.h"
#include "include/MP3Extractor.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/Utils.h>

namespace android {

static uint32_t U24_AT(const uint8_t *ptr) {
    return ptr[0] << 16 | ptr[1] << 8 | ptr[2];
}

// static
sp<VBRISeeker> VBRISeeker::CreateFromSource(
        const sp<DataSource> &source, off64_t post_id3_pos) {
    off64_t pos = post_id3_pos;

    uint8_t header[4];
    ssize_t n = source->readAt(pos, header, sizeof(header));
    if (n < (ssize_t)sizeof(header)) {
        return NULL;
    }

    uint32_t tmp = U32_AT(&header[0]);
    size_t frameSize;
    int sampleRate;
    if (!GetMPEGAudioFrameSize(tmp, &frameSize, &sampleRate)) {
        return NULL;
    }

    // VBRI header follows 32 bytes after the header _ends_.
    pos += sizeof(header) + 32;

    uint8_t vbriHeader[26];
    n = source->readAt(pos, vbriHeader, sizeof(vbriHeader));
    if (n < (ssize_t)sizeof(vbriHeader)) {
        return NULL;
    }

    if (memcmp(vbriHeader, "VBRI", 4)) {
        return NULL;
    }

    size_t numFrames = U32_AT(&vbriHeader[14]);

    int64_t durationUs =
        numFrames * 1000000ll * (sampleRate >= 32000 ? 1152 : 576) / sampleRate;

    ALOGV("duration = %.2f secs", durationUs / 1E6);

    size_t numEntries = U16_AT(&vbriHeader[18]);
    size_t entrySize = U16_AT(&vbriHeader[22]);
    size_t scale = U16_AT(&vbriHeader[20]);

    ALOGV("%d entries, scale=%d, size_per_entry=%d",
         numEntries,
         scale,
         entrySize);

    size_t totalEntrySize = numEntries * entrySize;
    uint8_t *buffer = new uint8_t[totalEntrySize];

    n = source->readAt(pos + sizeof(vbriHeader), buffer, totalEntrySize);
    if (n < (ssize_t)totalEntrySize) {
        delete[] buffer;
        buffer = NULL;

        return NULL;
    }

    sp<VBRISeeker> seeker = new VBRISeeker;
    seeker->mBasePos = post_id3_pos + frameSize;
    // only update mDurationUs if the calculated duration is valid (non zero)
    // otherwise, leave duration at -1 so that getDuration() and getOffsetForTime()
    // return false when called, to indicate that this vbri tag does not have the
    // requested information
    if (durationUs) {
        seeker->mDurationUs = durationUs;
    }

    off64_t offset = post_id3_pos;
    for (size_t i = 0; i < numEntries; ++i) {
        uint32_t numBytes;
        switch (entrySize) {
            case 1: numBytes = buffer[i]; break;
            case 2: numBytes = U16_AT(buffer + 2 * i); break;
            case 3: numBytes = U24_AT(buffer + 3 * i); break;
            default:
            {
                CHECK_EQ(entrySize, 4u);
                numBytes = U32_AT(buffer + 4 * i); break;
            }
        }

        numBytes *= scale;

        seeker->mSegments.push(numBytes);

        ALOGV("entry #%d: %d offset 0x%08lx", i, numBytes, offset);
        offset += numBytes;
    }

    delete[] buffer;
    buffer = NULL;

    ALOGI("Found VBRI header.");

    return seeker;
}

VBRISeeker::VBRISeeker()
    : mDurationUs(-1) {
}

bool VBRISeeker::getDuration(int64_t *durationUs) {
    if (mDurationUs < 0) {
        return false;
    }

    *durationUs = mDurationUs;

    return true;
}

bool VBRISeeker::getOffsetForTime(int64_t *timeUs, off64_t *pos) {
    if (mDurationUs < 0) {
        return false;
    }

    int64_t segmentDurationUs = mDurationUs / mSegments.size();

    int64_t nowUs = 0;
    *pos = mBasePos;
    size_t segmentIndex = 0;
    while (segmentIndex < mSegments.size() && nowUs < *timeUs) {
        nowUs += segmentDurationUs;
        *pos += mSegments.itemAt(segmentIndex++);
    }

    ALOGV("getOffsetForTime %lld us => 0x%08lx", *timeUs, *pos);

    *timeUs = nowUs;

    return true;
}
#define PARSE_SIZE  6*256*1024
bool VBRISeeker::getVbrDuration(const sp<DataSource> &source, off64_t post_id3_pos,unsigned *p_bitrate,off64_t *ptstable) 
{
	unsigned char header[8];
	int offset;
	int tsec = -1;
	int size ;
	int flag = 0;
	int layer = 3;
	int bitrate_index;
	int bitrate;
	int sample_index;
	int samplerate = 44100;
	int pad_slot;
	int N;
	unsigned sync_word;	
	int duration;
	unsigned total_frame=0;
	unsigned bitrate_sum = 0;
	unsigned framenum = 0;
	unsigned payload = 0;
	off64_t total_size=0;
       off64_t fileSize;
       unsigned long const bitrate_table[5][15] = {
  /* MPEG-1 */
  { 0,  32000,  64000,  96000, 128000, 160000, 192000, 224000,  /* Layer I   */
       256000, 288000, 320000, 352000, 384000, 416000, 448000 },
  { 0,  32000,  48000,  56000,  64000,  80000,  96000, 112000,  /* Layer II  */
       128000, 160000, 192000, 224000, 256000, 320000, 384000 },
  { 0,  32000,  40000,  48000,  56000,  64000,  80000,  96000,  /* Layer III */
       112000, 128000, 160000, 192000, 224000, 256000, 320000 },

  /* MPEG-2 LSF */
  { 0,  32000,  48000,  56000,  64000,  80000,  96000, 112000,  /* Layer I   */
       128000, 144000, 160000, 176000, 192000, 224000, 256000 },
  { 0,   8000,  16000,  24000,  32000,  40000,  48000,  56000,  /* Layers    */
        64000,  80000,  96000, 112000, 128000, 144000, 160000 } /* II & III  */
};	
#define MAD_NSBSAMPLES(layer, flag)  \
  (layer == 1 ? 12 :  \
   ((layer == 3 &&  \
     (flag & 0x1000)) ? 18 : 36))	   
      unsigned samplerate_table[3] = { 44100, 48000, 32000 };
	if(ptstable == NULL)
		return false;
	if (source->getSize(&fileSize) != OK){
		ALOGI("get file size failed \n");
		return false;   	
	}
	unsigned buffersize = (fileSize<PARSE_SIZE)?fileSize:PARSE_SIZE;
	unsigned char *read_buf = new uint8_t[buffersize];
	if(read_buf == NULL){
		return false;   
	}	
	unsigned read_size = ((fileSize-post_id3_pos)>=buffersize)?buffersize:(fileSize-post_id3_pos);
       ssize_t n = source->readAt(post_id3_pos, read_buf,read_size);
       if (n < read_size) {
		if(read_buf)
			delete[] read_buf;
		return false;       
	}		
	offset = 0;
	ALOGI("getVbrDuration parse data size %d\n",read_size);
	while(offset < read_size-7)
	{
		header[0] = read_buf[offset];
		header[1] = read_buf[offset+1];			
		if((header[0]==0xff) && ((header[1]&0xe0) == 0xe0))
		{
			// decode the header
			header[2] = read_buf[ offset+2];
			header[3] = read_buf[ offset+3];
			sync_word = (header[3]) | (header[2]<<8) | (header[1]<<16) | (header[0]<<24);
			flag = 0;
			// 
			if((sync_word&(1<<20)) == 0){
				flag |= 0x4000;//MAD_FLAG_MPEG_2_5_EXT;
				flag |= 0x1000;//MAD_FLAG_LSF_EXT;				
			}
			else{
				if((sync_word&(1<<19)) == 0)
					flag |= 0x1000;//MAD_FLAG_LSF_EXT;				
			}

			// layer
			layer =4 - ((sync_word >> 17) & 3);
			if(layer == 4)
			{
				offset ++;
				continue;
			}
			// bitrate
			bitrate_index = (sync_word >> 12) & 0xf;
			if((bitrate_index == 15) || (bitrate_index==0))
			{
				offset++;
				continue;
			}

			if (flag & 0x1000/*MAD_FLAG_LSF_EXT*/)
				bitrate = bitrate_table[3 + (layer >> 1)][bitrate_index];
			else
				bitrate = bitrate_table[layer - 1][bitrate_index];
			// samplerate
			sample_index = (sync_word >> 10) & 3;
			if(sample_index == 3)
			{
				offset ++;
				continue;
			}
			samplerate = samplerate_table[sample_index];
			if (flag & 0x1000/*MAD_FLAG_LSF_EXT*/) 
			{
				samplerate /= 2;
			if (flag & 0x4000/*MAD_FLAG_MPEG_2_5_EXT*/)
				samplerate /= 2;
			}
			pad_slot = (sync_word >> 9) & 1;
			if (layer == 1)
			{
				N = ((12 * bitrate / samplerate) + pad_slot) * 4;
			}
			else 
			{
				unsigned int slots_per_frame;
				slots_per_frame = (layer == 3 &&(flag & 0x1000/*MAD_FLAG_LSF_EXT*/)) ? 72 : 144;
				N = (slots_per_frame * bitrate / samplerate) + pad_slot;
			}
			total_frame += 32*MAD_NSBSAMPLES(layer, flag);
			if(tsec < (total_frame/samplerate)){
				tsec = total_frame/samplerate;
				ptstable[tsec] = offset;
			}
			payload += N;
			offset +=  N;
			framenum += 1;
			bitrate_sum += bitrate/1000;
		}
		else
		{
			offset++;
			continue;
		}

	}
	if(p_bitrate)
		*p_bitrate =bitrate_sum/framenum;
	if(read_buf)
		delete[] read_buf;	
	return true;	
}
   

}  // namespace android

