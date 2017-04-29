/*
 *  Copyright (C) Jory Stone (jcsston at toughguy net) - 2003-2004
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

/*!
    \file matroska_parser.h
		\version $Id$
    \brief An audio slated Matroska Reader+Parser
		\author Jory Stone     <jcsston @ toughguy.net>

    This was originally part of the foobar2000 Matroska plugin.
*/

#ifndef _MATROSKA_PARSER_H_
#define _MATROSKA_PARSER_H_

#define MULTITRACK 1

#include <map>
#include <set>
#include <list>
#include <cstring>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/ptr_container/ptr_deque.hpp>

// libebml includes
#include "ebml/StdIOCallback.h"
#include "ebml/EbmlTypes.h"
#include "ebml/EbmlHead.h"
#include "ebml/EbmlVoid.h"
#include "ebml/EbmlCrc32.h"
#include "ebml/EbmlSubHead.h"
#include "ebml/EbmlStream.h"
#include "ebml/EbmlBinary.h"
#include "ebml/EbmlString.h"
#include "ebml/EbmlUnicodeString.h"
#include "ebml/EbmlContexts.h"
#include "ebml/EbmlVersion.h"

// libmatroska includes
#include "matroska/KaxConfig.h"
#include "matroska/KaxBlock.h"
#include "matroska/KaxSegment.h"
#include "matroska/KaxContexts.h"
#include "matroska/KaxSeekHead.h"
#include "matroska/KaxTracks.h"
#include "matroska/KaxInfo.h"
#include "matroska/KaxInfoData.h"
#include "matroska/KaxTags.h"
#include "matroska/KaxTag.h"
// #include "matroska/KaxTagMulti.h"    TO_DO: obsolete?
#include "matroska/KaxCluster.h"
#include "matroska/KaxClusterData.h"
#include "matroska/KaxTrackAudio.h"
#include "matroska/KaxTrackVideo.h"
#include "matroska/KaxAttachments.h"
#include "matroska/KaxAttached.h"
#include "matroska/KaxChapters.h"
#include "matroska/KaxVersion.h"


#define TIMECODE_SCALE  1000000
#define MAX_UINT64 0xFFFFFFFFFFFFFFFF
#define _DELETE(__x) if (__x) { delete __x; __x = NULL; }

//Memory Leak Debuging define
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC 
#include <stdlib.h>
#include <crtdbg.h>
#endif

#ifdef _DEBUG
   #define DEBUG_CLIENTBLOCK   new( _CLIENT_BLOCK, __FILE__, __LINE__)
#else
   #define DEBUG_CLIENTBLOCK
#endif // _DEBUG

#ifdef _DEBUG
#define new DEBUG_CLIENTBLOCK
#endif

#ifdef _DEBUG
#endif

using namespace LIBEBML_NAMESPACE;
using namespace LIBMATROSKA_NAMESPACE;

typedef std::vector<uint8> ByteArray;
typedef boost::shared_ptr<EbmlElement> ElementPtr;

class MatroskaVersion {
public:
    static const char * lib_ebml() { return EbmlCodeVersion.c_str(); };
    static const char * lib_matroska() { return KaxCodeVersion.c_str(); };
};

class MatroskaAttachment {
public:
	MatroskaAttachment();

	UTFstring FileName;
	std::string MimeType;
	UTFstring Description;

	// Details on where the attachment resides.
	UTFstring SourceFilename;
	uint64 SourceStartPos;
	uint64 SourceDataLength;
};

class MatroskaFrame {
public:
	MatroskaFrame();
	void Reset();
    double get_duration() const
    {
        return static_cast<double>(duration) / 1000000000.0;
    }

    double get_time() const
    {
        return static_cast<double>(timecode) / 1000000000.0;
    }

    template< typename DestType > void get_payload( DestType &dest ) const
    {
        size_t total_size = 0;
        for (std::vector<ByteArray>::const_iterator i = dataBuffer.begin();
            i != dataBuffer.end(); ++i)
        {
            total_size += i->size();
        }

        dest.reserve( dest.size() + total_size );
        for (std::vector<ByteArray>::const_iterator i = dataBuffer.begin();
            i != dataBuffer.end(); ++i)
        {
            dest.insert( dest.end(), i->begin(), i->end() );
        }
    }

	uint64 timecode;
	uint64 duration;
	std::vector<ByteArray> dataBuffer;
	/// Linked-list for laced frames
    uint64 add_id;
    ByteArray additional_data_buffer;
};


struct MatroskaMetaSeekClusterEntry {
	uint32 clusterNo;
	uint64 filePos;
	uint64 timecode;
};

class MatroskaSimpleTag {
public:
	MatroskaSimpleTag();

	UTFstring name;
	UTFstring value;
	uint32 defaultFlag;
	std::string language;

	bool hidden;
	bool removalPending;
};

class MatroskaTagInfo {
public:
	MatroskaTagInfo();
	void SetTagValue(const char *name, const char *value, int index = 0);
	void MarkAllAsRemovalPending();
	void RemoveMarkedTags();
	
	uint64 targetTrackUID;
	uint64 targetEditionUID;
	uint64 targetChapterUID;
	uint64 targetAttachmentUID;
	uint32 targetTypeValue;
	std::string targetType;

	std::vector<MatroskaSimpleTag> tags;
};

struct MatroskaChapterDisplayInfo {
	MatroskaChapterDisplayInfo();

	UTFstring string;
	std::string lang;
	std::string country;
};

class MatroskaChapterInfo {
public:
	MatroskaChapterInfo();

	uint64 chapterUID;
	uint64 timeStart;
	uint64 timeEnd;
	/// Vector of all the tracks this chapter applies to
	/// if it's empty then this chapter applies to all tracks
	std::vector<uint64> tracks;
	/// Vector of strings we can display for chapter
	std::vector<MatroskaChapterDisplayInfo> display;
	std::vector<MatroskaChapterInfo> subChapters;
};

class MatroskaEditionInfo {
public:
	MatroskaEditionInfo();

	uint64 editionUID;
	/// Vector of all the tracks this edition applies to
	/// if it's empty then this edition applies to all tracks
	std::vector<uint64> tracks;
};

class MatroskaTrackInfo {
	public:
		/// Initializes the class
		MatroskaTrackInfo();
		/// Initializes the class with a UID
		/// \param trackUID The UID to use for the new MatroskaTrackInfo
		//MatroskaTrackInfo(uint64 trackUID);
		/// Destroys the class
		//~MatroskaTrackInfo();


		track_type trackType;
		uint16 trackNumber;
		uint64 trackUID;		
		std::string codecID;
		std::vector<uint8_t> codecPrivate;
		bool codecPrivateReady;
		
		UTFstring name;
		std::string language;
		double duration;

        uint8 channels; 
        double samplesPerSec; 
        double samplesOutputPerSec;     
        uint8 bitsPerSample;
        uint32 avgBytesPerSec; 
        uint64 defaultDuration;
};

typedef boost::shared_ptr<MatroskaMetaSeekClusterEntry> cluster_entry_ptr;


class MatroskaParser {
public:
	explicit MatroskaParser(const char *filename /*, abort_callback & p_abort */ );
	~MatroskaParser();

	/// The main header parsing function
	/// \return 0 File parsed ok
	/// \return 1 Failed
	int Parse(bool bInfoOnly = false, bool bBreakAtClusters = true);

	MatroskaTrackInfo &GetTrack(uint16 trackNo) { return m_Tracks.at(trackNo); };
	uint16 FindTrack(uint16 trackNum) const;
	uint64 GetTimecodeScale() { return m_TimecodeScale; };

	/// Returns an adjusted duration of the file
	double GetDuration();

	/// Returns an adjusted duration of the track
	double GetTrackDuration( uint32 trackIdx ) const;

	/// Returns the track index of the first decodable track
	int32 GetFirstTrack( track_type type ) const;

	double TimecodeToSeconds(uint64 code,unsigned samplerate_hint = 44100);
	uint64 SecondsToTimecode(double seconds);

	/// Enable reading data from a given track.
	void EnableTrack(uint32 newTrackIdx);

	/// Set the subsong to play, this adjusts all the duration/timecodes 
	/// reported in public functions. So only use this if you are expecting that to happen
	/// \param subsong This should be within the range of the chapters vector
	void SetSubSong(int subsong);

    /// Limits the number of frames that will be read on any track, while queuing
    /// up frames for another.  Note: this is not a hard limit.
    /// \param depth number of frames; 0 disables.
    void SetMaxQueueDepth( unsigned int depth );

	std::vector<MatroskaEditionInfo> &GetEditions() { return m_Editions; };
	std::vector<MatroskaChapterInfo> &GetChapters() { return m_Chapters; };
	std::vector<MatroskaTrackInfo> &GetTracks() { return m_Tracks; };
	uint32 GetTrackCount() const;
	/// Returns the number of tracks of a given type.
	uint32 GetTrackCount( track_type type ) const;
	/// Returns the absolute index for the Nth track of a specified type.
	int32 GetTrackIndex( track_type type, uint32 index) const;

	int32 GetAvgBitrate();

	/// Seek to a position
	/// \param seconds The absolute position to seek to, in seconds			
	/// \return Current postion offset from where it was requested
	/// If you request to seek to 2.0 and we can only seek to 1.9
	/// the return value would be 100 * m_TimcodeScale

	bool skip_frames_until(double destination, unsigned hint_samplerate);
	bool Seek(double seconds, unsigned samplerate_hint);

	MatroskaFrame * ReadSingleFrame( uint16 trackIdx);

    /// Seeks to the beginning of the stream.
    bool Restart(); // Untested & might not work.

	UTFstring GetSegmentFileName() { return m_SegmentFilename; }
    typedef std::list<MatroskaAttachment> attachment_list;
	const attachment_list &GetAttachmentList() const;
    ByteArray ReadAttachment( attachment_list::const_iterator attachment );

    /// Indicates whether the end of the file has been reached.
    /// When reading multiple tracks, use this to decide when to stop reading.
    bool IsEof() const;

protected:
	void Parse_MetaSeek(ElementPtr metaSeekElement, bool bInfoOnly);
	void Parse_Chapters(KaxChapters *chaptersElement);
	void Parse_Chapter_Atom(KaxChapterAtom *ChapterAtom);
	void Parse_Chapter_Atom(KaxChapterAtom *ChapterAtom, std::vector<MatroskaChapterInfo> &p_chapters);
	void Parse_Tags(KaxTags *tagsElement);

	/// Reads frames from file.
	/// \return -1 If another queue is full.
	/// \return 0 If read ok	
	/// \return 1 End of file
	/// \return 2 If no cluster at current timecode
	int FillQueue();
	uint64 GetClusterTimecode(uint64 filePos);
	cluster_entry_ptr FindCluster(uint64 timecode);
	void CountClusters();
	void FixChapterEndTimes();
	// See if the edition uid is already in our vector
	// \return true Yes, we already have this uid
	// \return false Nope
	bool FindEditionUID(uint64 uid);
	// See if the chapter uid is already in our vector
	// \return true Yes, we already have this uid
	// \return false Nope
	bool FindChapterUID(uint64 uid);
	
	MatroskaTagInfo *FindTagWithTrackUID(uint64 trackUID);
	MatroskaTagInfo *FindTagWithEditionUID(uint64 editionUID, uint64 trackUID = 0);
	MatroskaTagInfo *FindTagWithChapterUID(uint64 chapterUID, uint64 trackUID = 0);

    bool TrackNumIsEnabled( uint16 trackNum ) const;
    bool IsAnyQueueFull() const;

    std::string m_filename;
	boost::scoped_ptr<IOCallback> m_IOCallback;
	EbmlStream m_InputStream;
	/// The main/base/master element, should be the segment
	ElementPtr m_ElementLevel0;

	MatroskaChapterInfo *m_CurrentChapter;
	uint32 m_CurrentTrackNo;
	std::set< uint16 > m_EnabledTrackNumbers;
	std::vector<MatroskaTrackInfo> m_Tracks;
	std::vector<MatroskaEditionInfo> m_Editions;
	std::vector<MatroskaChapterInfo> m_Chapters;
	std::vector<MatroskaTagInfo> m_Tags;
	
	/// This is the queue of buffered frames to deliver
    typedef boost::ptr_deque< MatroskaFrame > FrameQueue;
    typedef std::map<uint32, FrameQueue> FrameQueueMap;
	FrameQueueMap m_FrameQueues;

	/// This is the index of clusters in the file, it's used to seek in the file
	// std::vector<MatroskaMetaSeekClusterEntry> m_ClusterIndex;
    std::vector<cluster_entry_ptr> m_ClusterIndex;

    attachment_list m_AttachmentList;

	uint64 m_CurrentTimecode;
	double m_Duration;
	uint64 m_TimecodeScale;
	UTFstring m_WritingApp;
	UTFstring m_MuxingApp;
	UTFstring m_FileTitle;
	int64 m_FileDate;
	UTFstring m_SegmentFilename;
	uint32    m_MaxQueueDepth;

	uint64 m_FileSize;
	bool   m_Eof;
	uint64 m_TagPos;
	uint32 m_TagSize;
	uint32 m_TagScanRange;

	//int UpperElementLevel;
};

typedef boost::shared_ptr<MatroskaParser> matroska_parser_ptr;

void PrintChapters(std::vector<MatroskaChapterInfo> &theChapters);

class MatroskaSearch
{
private:
	static const int SEARCH_SOURCE_SIZE = 1024*64;
	static const int SEARCH_TABLE_SIZE = SEARCH_SOURCE_SIZE;
	static const int SEARCH_PATTERN_SIZE = 3;
    binary * source, * pattern;
	int pos;
    int  next[SEARCH_TABLE_SIZE], skip[SEARCH_TABLE_SIZE];
	bool Skip();
	bool Next();
public:
	MatroskaSearch(binary * p_source, binary * p_pattern)
	{
		memset(next, 0, sizeof(next));
		memset(skip, 0, sizeof(skip));
		source = p_source;
		pattern = p_pattern;
		Skip();
		Next();
	}
	~MatroskaSearch() {};
	int Match(unsigned int start = 0);
};

#endif // _MATROSKA_PARSER_H_
