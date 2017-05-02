/*
 *  Formerly part of the foobar2000 Matroska plugin
 *
 *  Copyright (C) Jory 'jcsston' Stone (jcsston at toughguy net) - 2003-2004
 *  Copyright (C) Matt Gruenke (github.com/mattgruenke) - 2017
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
    \file matroska_parser.cpp
		\version $Id$
    \brief An audio slated Matroska Reader+Parser
		\author Jory Stone     <jcsston @ toughguy.net>
*/

#include "mkvreader/matroska_parser.h"

#include <cmath>
#include <limits>
#include <string>
#include <typeinfo>
#include <iostream>
#include <algorithm>

#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/case_conv.hpp>

using std::string;

using namespace LIBEBML_NAMESPACE;	
using namespace LIBMATROSKA_NAMESPACE;


#define MAX_UINT64      std::numeric_limits<uint64>::max()
#define _DELETE(x)      if (x) { delete (x); (x) = NULL; }


#ifndef LOG_LEVEL
#   define LOG_LEVEL 2
#endif

#define LOG_FORMATTED( ... )    { fprintf( stderr, __VA_ARGS__ ); fprintf( stderr, "\n" ); fflush( stderr ); }
#define LOG_STREAM( s )         (std::cerr << s << std::endl)
#define LOG_DISABLED( ... )     static_cast< void >( 0 )

#if LOG_LEVEL >= 1
#   define LOG_ERROR( ... )     LOG_FORMATTED( __VA_ARGS__ )
#   define LOG_ERROR_S( s )     LOG_STREAM( s )
#else
#   define LOG_ERROR( ... )     LOG_DISABLED( __VA_ARGS__ )
#   define LOG_ERROR_S( s )     LOG_DISABLED( s )
#endif

#if LOG_LEVEL >= 2
#   define LOG_WARN(  ... )     LOG_FORMATTED( __VA_ARGS__ )
#   define LOG_WARN_S(  s )     LOG_STREAM( s )
#else
#   define LOG_WARN( ... )      LOG_DISABLED( __VA_ARGS__ )
#   define LOG_WARN_S( s )      LOG_DISABLED( s )
#endif

#if LOG_LEVEL >= 3
#   define LOG_INFO(  ... )     LOG_FORMATTED( __VA_ARGS__ )
#   define LOG_INFO_S(  s )     LOG_STREAM( s )
#else
#   define LOG_INFO( ... )      LOG_DISABLED( __VA_ARGS__ )
#   define LOG_INFO_S( s )      LOG_DISABLED( s )
#endif

#if LOG_LEVEL >= 4
#   define LOG_DEBUG( ... )     LOG_FORMATTED( __VA_ARGS__ )
#   define LOG_DEBUG_S( s )     LOG_STREAM( s )
#else
#   define LOG_DEBUG( ... )     LOG_DISABLED( __VA_ARGS__ )
#   define LOG_DEBUG_S( s )     LOG_DISABLED( s )
#endif


namespace mkvreader {


static bool IsSeekable(const IOCallback &)
{
#if 0
    return true;    // since we're using StdIOCallback, it's always seekable.
#else
    return false;   // except seekable mode seems (more) broken...
#endif
}

uint64 MatroskaParser::SecondsToTimecode(double seconds)
{
	return (uint64)floor(seconds * 1000000000);
};

double MatroskaParser::TimecodeToSeconds(uint64 code, unsigned /*samplerate_hint*/ )
{
	return ((double)(int64)code / 1000000000);
};


MatroskaFrame::MatroskaFrame() 
: timecode( 0 ),
  duration( 0 ),
  add_id( 0 )
{
}

MatroskaAttachment::MatroskaAttachment()
{
	FileName = L"";
	MimeType = "";
	Description = L"";
	SourceFilename = L"";
	SourceStartPos = 0;
	SourceDataLength = 0;
};

void MatroskaFrame::Reset()
{
	timecode = 0;
	duration = 0;
    add_id = 0;
};

MatroskaSimpleTag::MatroskaSimpleTag()
{
	name = L"";
	value = L"";
	language = "und";
	defaultFlag = 1;
	hidden = false;
	removalPending = false;
};

MatroskaTagInfo::MatroskaTagInfo()
{
	targetTrackUID = 0;
	targetEditionUID = 0;
	targetChapterUID = 0;
	targetAttachmentUID = 0;
	targetTypeValue = 0;
};

void MatroskaTagInfo::SetTagValue(const char *name, const char *value, int index)
{
	for (size_t s = 0; s < tags.size(); s++)
	{
		MatroskaSimpleTag &currentSimpleTag = tags.at(s);
		if (strcasecmp(currentSimpleTag.name.GetUTF8().c_str(), name) == 0)			
		{
			if(index == 0)
			{
				currentSimpleTag.value.SetUTF8(value);
				currentSimpleTag.removalPending = false;
				return;
			}
			index--;
		}
	}

	// If we are here then we didn't find this tag in the vector already
	MatroskaSimpleTag newSimpleTag;
	newSimpleTag.name.SetUTF8(name);
	newSimpleTag.value.SetUTF8(value);	
	newSimpleTag.removalPending = false;
	tags.push_back(newSimpleTag);
};

void MatroskaTagInfo::RemoveMarkedTags()
{
	for (int i = int( tags.size() )-1; i >= 0; i--)
	{
		MatroskaSimpleTag &simpleTag = tags.at(i);
		if(simpleTag.removalPending == true)
		{
			tags.erase(tags.begin() + i);
		}
	}
}

void MatroskaTagInfo::MarkAllAsRemovalPending()
{
	for (int i = int( tags.size() )-1; i >= 0; i--)
	{
		MatroskaSimpleTag &simpleTag = tags.at(i);
		simpleTag.removalPending = true;
	}
}

MatroskaChapterDisplayInfo::MatroskaChapterDisplayInfo()  {
	string = L"";
};

MatroskaChapterInfo::MatroskaChapterInfo() {
	chapterUID = 0;
	timeStart = 0;
	timeEnd = 0;
}

MatroskaEditionInfo::MatroskaEditionInfo() {
	editionUID = 0;
}

MatroskaTrackInfo::MatroskaTrackInfo() {
	trackType = (track_type) 0;
	trackNumber = 0;
	trackUID = 0;		
	duration = 0;

	channels = 1;
	samplesPerSec = 0;
	samplesOutputPerSec = 0;
	bitsPerSample = 0;
	avgBytesPerSec = 0;
	defaultDuration = 0;

	name = L"";

	codecPrivateReady = false;
};

MatroskaParser::MatroskaParser(const char *filename ) 
	:
		m_filename(filename),
		m_IOCallback(new StdIOCallback(filename, MODE_READ)), // TO_DO: revisit mode
		m_InputStream(*m_IOCallback),
		m_MaxQueueDepth( 0 ),
		m_Eof( false )
{
	m_TimecodeScale = mkvreader::DefaultTimecodeScale;
	m_FileDate = 0;
	m_Duration = 0;
	m_CurrentTimecode = 0;
	//m_ElementLevel0 = NULL;
	//UpperElementLevel = 0;
	m_CurrentChapter = 0;
	m_FileSize = boost::filesystem::file_size(filename); // TO_DO: throws
	m_TagPos = 0;
	m_TagSize = 0;
	m_TagScanRange = 1024 * 64;
	m_CurrentTrackNo = 0;
};

MatroskaParser::~MatroskaParser() {
	//if (m_ElementLevel0 != NULL)
	//	_DELETE(m_ElementLevel0);
		//delete m_ElementLevel0;
};

int MatroskaParser::Parse(bool bInfoOnly, bool bBreakAtClusters) 
{
	try {
		int UpperElementLevel = 0;
		bool bAllowDummy = false;
		// Elements for different levels
		ElementPtr ElementLevel1;
		ElementPtr ElementLevel2;
		ElementPtr ElementLevel3;
		ElementPtr ElementLevel4;
		ElementPtr ElementLevel5;
		ElementPtr NullElement;

		// Be sure we are at the beginning of the file
		m_IOCallback->setFilePointer(0);
		// Find the EbmlHead element. Must be the first one.
		m_ElementLevel0 = ElementPtr(m_InputStream.FindNextID(EbmlHead::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (m_ElementLevel0 == NullElement) {
			LOG_ERROR_S( "No EbmlHead/level 0 element found." );
			return 1;
		}
		//We must have found the EBML head :)
		m_ElementLevel0->SkipData(m_InputStream, m_ElementLevel0->Generic().Context);
		//delete m_ElementLevel0;
		//_DELETE(m_ElementLevel0);

		// Next element must be a segment
		m_ElementLevel0 = ElementPtr(m_InputStream.FindNextID(KaxSegment::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (m_ElementLevel0 == NullElement) {
			LOG_ERROR_S( "No segment/level 0 element found." );
			return 1;
		}
		if (!(EbmlId(*m_ElementLevel0) == KaxSegment::ClassInfos.GlobalId)) {
			//delete m_ElementLevel0;
			//m_ElementLevel0 = NULL;
			//_DELETE(m_ElementLevel0);
			LOG_ERROR_S( "MatroskaParser::Parse() ElementLevel0 != Segment." );
			return 1;
		}

		UpperElementLevel = 0;
		// We've got our segment, so let's find the tracks
		ElementLevel1 = ElementPtr(m_InputStream.FindNextElement(m_ElementLevel0->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, true, 1));
		while (ElementLevel1 != NullElement) {
			if (UpperElementLevel > 0) {
                LOG_DEBUG_S( "MatroskaParser::Parse(): UpperElementLevel = " << UpperElementLevel );
				break;
			}
			if (UpperElementLevel < 0) {
				UpperElementLevel = 0;
			}

			if (EbmlId(*ElementLevel1) == KaxSeekHead::ClassInfos.GlobalId) {
				if (IsSeekable(*m_IOCallback)) {
					Parse_MetaSeek(ElementLevel1, bInfoOnly);
					if (m_TagPos == 0) {
						// Search for them at the end of the file
						if (m_TagScanRange > 0)
						{
							m_IOCallback->setFilePointer(m_FileSize - m_TagScanRange);
							uint64 init_pos = m_IOCallback->getFilePointer();
							/*
								BM Search
							*/
							binary buf[1024*64];
							binary pat[3]; pat[0] = 0x54; pat[1] = 0xc3; pat[2] = 0x67;
							uint64 s_pos = m_IOCallback->getFilePointer();
							m_IOCallback->read(buf, m_TagScanRange);
							MatroskaSearch search(buf, pat);
							int pos = search.Match();
							if (pos != -1) {
								do {
									m_IOCallback->setFilePointer(s_pos+pos+3);
									uint64 startPos = m_IOCallback->getFilePointer();
									m_IOCallback->setFilePointer(-4, seek_current);
									ElementPtr levelUnknown = ElementPtr(m_InputStream.FindNextID(KaxTags::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
									if ((levelUnknown != NullElement) 
										&& (m_FileSize >= startPos + levelUnknown->GetSize()) 
										&& (EbmlId(*levelUnknown) == KaxTags::ClassInfos.GlobalId))
									{
										Parse_Tags(static_cast<KaxTags *>(levelUnknown.get()));
										break;
									}
									m_IOCallback->setFilePointer(s_pos);
									pos = search.Match(pos+1);
								} while (pos != -1);
								/*
									~BM Search
								*/
							} else {
								m_IOCallback->setFilePointer(init_pos);
								binary Buffer[4];
								while (m_IOCallback->read(Buffer, 3) >= 3)
								{//0x18
									if ((Buffer[0] == 0x54) && (Buffer[1] == 0xc3) && (Buffer[2] == 0x67))
									{
										uint64 startPos = m_IOCallback->getFilePointer();

										//seek back 3 bytes, so libmatroska can find the Tags element Ebml ID
										m_IOCallback->setFilePointer(-4, seek_current);

										ElementPtr levelUnknown = ElementPtr(m_InputStream.FindNextID(KaxTags::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
										if ((levelUnknown != NullElement) 
											&& (m_FileSize >= startPos + levelUnknown->GetSize()) 
											&& (EbmlId(*levelUnknown) == KaxTags::ClassInfos.GlobalId))
										{
											Parse_Tags(static_cast<KaxTags *>(levelUnknown.get()));
											//_DELETE(levelUnknown);
											break;
										}
										//_DELETE(levelUnknown);

										//Restore the file pos
										m_IOCallback->setFilePointer(startPos);
									}
									//seek back 2 bytes
									m_IOCallback->setFilePointer(-2, seek_current);
								}
							}
						} else {
							//m_TagPos = m_FileSize;
						}
					}
				}
				else LOG_INFO_S( "MatroskaParser::Parse(): IsSeekable() returned false." );
			}else if (EbmlId(*ElementLevel1) == KaxInfo::ClassInfos.GlobalId) {
				// General info about this Matroska file
				ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, bAllowDummy));
				while (ElementLevel2 != NullElement) {
					if (UpperElementLevel > 0) {
						break;
					}
					if (UpperElementLevel < 0) {
						UpperElementLevel = 0;
					}

					if (EbmlId(*ElementLevel2) == KaxTimecodeScale::ClassInfos.GlobalId) {
						KaxTimecodeScale &TimeScale = *static_cast<KaxTimecodeScale *>(ElementLevel2.get());
						TimeScale.ReadData(m_InputStream.I_O());

						//matroskaGlobalTrack->SetTimecodeScale(uint64(TimeScale));
						m_TimecodeScale = uint64(TimeScale);
					} else if (EbmlId(*ElementLevel2) == KaxDuration::ClassInfos.GlobalId) {
						KaxDuration &duration = *static_cast<KaxDuration *>(ElementLevel2.get());
						duration.ReadData(m_InputStream.I_O());

						// it's in milliseconds? -- in nanoseconds.
						m_Duration = double(duration) * double(m_TimecodeScale);

					} else if (EbmlId(*ElementLevel2) == KaxDateUTC::ClassInfos.GlobalId) {
						KaxDateUTC & DateUTC = *static_cast<KaxDateUTC *>(ElementLevel2.get());
						DateUTC.ReadData(m_InputStream.I_O());
						
						m_FileDate = DateUTC.GetEpochDate();

					} else if (EbmlId(*ElementLevel2) == KaxSegmentFilename::ClassInfos.GlobalId) {
						KaxSegmentFilename &tag_SegmentFilename = *static_cast<KaxSegmentFilename *>(ElementLevel2.get());
						tag_SegmentFilename.ReadData(m_InputStream.I_O());

						m_SegmentFilename = *static_cast<EbmlUnicodeString *>(&tag_SegmentFilename);

					} else if (EbmlId(*ElementLevel2) == KaxMuxingApp::ClassInfos.GlobalId)	{
						KaxMuxingApp &tag_MuxingApp = *static_cast<KaxMuxingApp *>(ElementLevel2.get());
						tag_MuxingApp.ReadData(m_InputStream.I_O());

						m_MuxingApp = *static_cast<EbmlUnicodeString *>(&tag_MuxingApp);

					} else if (EbmlId(*ElementLevel2) == KaxWritingApp::ClassInfos.GlobalId) {
						KaxWritingApp &tag_WritingApp = *static_cast<KaxWritingApp *>(ElementLevel2.get());
						tag_WritingApp.ReadData(m_InputStream.I_O());
						
						m_WritingApp = *static_cast<EbmlUnicodeString *>(&tag_WritingApp);

					} else if (EbmlId(*ElementLevel2) == KaxTitle::ClassInfos.GlobalId) {
						KaxTitle &Title = *static_cast<KaxTitle*>(ElementLevel2.get());
						Title.ReadData(m_InputStream.I_O());
						m_FileTitle = UTFstring(Title).c_str();
					}

					if (UpperElementLevel > 0) {	// we're coming from ElementLevel3
						UpperElementLevel--;
						//delete ElementLevel2;
						ElementLevel2 = ElementLevel3;
						if (UpperElementLevel > 0)
							break;
					} else {
						ElementLevel2->SkipData(m_InputStream, ElementLevel2->Generic().Context);
						//delete ElementLevel2;
						//_DELETE(ElementLevel2);
						ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, bAllowDummy));
					}
				}
			}else if (EbmlId(*ElementLevel1) == KaxChapters::ClassInfos.GlobalId) {
				Parse_Chapters(static_cast<KaxChapters *>(ElementLevel1.get()));
			}else if (EbmlId(*ElementLevel1) == KaxTags::ClassInfos.GlobalId) {
				Parse_Tags(static_cast<KaxTags *>(ElementLevel1.get()));
			} else if (EbmlId(*ElementLevel1) == KaxTracks::ClassInfos.GlobalId) {
				// Yep, we've found our KaxTracks element. Now find all tracks
				// contained in this segment. 
				KaxTracks *Tracks = static_cast<KaxTracks *>(ElementLevel1.get());
				EbmlElement* tmpElement = ElementLevel2.get();
				Tracks->Read(m_InputStream, KaxTracks::ClassInfos.Context, UpperElementLevel, tmpElement, bAllowDummy);

				for (uint32 Index0 = 0; Index0 < Tracks->ListSize(); Index0++) {
					if ((*Tracks)[Index0]->Generic().GlobalId == KaxTrackEntry::ClassInfos.GlobalId) {
						KaxTrackEntry &TrackEntry = *static_cast<KaxTrackEntry *>((*Tracks)[Index0]);
						// Create a new MatroskaTrack
						MatroskaTrackInfo newTrack;
						
						for (uint32 Index1 = 0; Index1 < TrackEntry.ListSize(); Index1++) {
							if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackNumber::ClassInfos.GlobalId) {
								KaxTrackNumber &TrackNumber = *static_cast<KaxTrackNumber*>(TrackEntry[Index1]);
								newTrack.trackNumber = TrackNumber;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackUID::ClassInfos.GlobalId) {
								KaxTrackUID &TrackUID = *static_cast<KaxTrackUID*>(TrackEntry[Index1]);
								newTrack.trackUID = TrackUID;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackType::ClassInfos.GlobalId) {
								KaxTrackType &TrackType = *static_cast<KaxTrackType*>(TrackEntry[Index1]);
								newTrack.trackType = (track_type) (uint8) TrackType;    // TO_DO: check that this is a supported track type.

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackTimecodeScale::ClassInfos.GlobalId) {
								//KaxTrackTimecodeScale &TrackTimecodeScale = *static_cast<KaxTrackTimecodeScale*>(TrackEntry[Index1]);
								// TODO: Support Tracks with different timecode scales?
								//newTrack->TrackTimecodeScale = TrackTimecodeScale;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackDefaultDuration::ClassInfos.GlobalId) {
								KaxTrackDefaultDuration &TrackDefaultDuration = *static_cast<KaxTrackDefaultDuration*>(TrackEntry[Index1]);
								newTrack.defaultDuration = uint64(TrackDefaultDuration);

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxCodecID::ClassInfos.GlobalId) {
								KaxCodecID &CodecID = *static_cast<KaxCodecID*>(TrackEntry[Index1]);
								newTrack.codecID = std::string(CodecID);

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxCodecPrivate::ClassInfos.GlobalId) {
								KaxCodecPrivate &CodecPrivate = *static_cast<KaxCodecPrivate*>(TrackEntry[Index1]);
								newTrack.codecPrivate.resize(CodecPrivate.GetSize());								
								memcpy(&newTrack.codecPrivate[0], CodecPrivate.GetBuffer(), CodecPrivate.GetSize());

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackFlagDefault::ClassInfos.GlobalId) {
								//KaxTrackFlagDefault &TrackFlagDefault = *static_cast<KaxTrackFlagDefault*>(TrackEntry[Index1]);
								//newTrack->FlagDefault = TrackFlagDefault;
							/* Matroska2
							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackFlagEnabled::ClassInfos.GlobalId) {
								KaxTrackFlagEnabled &TrackFlagEnabled = *static_cast<KaxTrackFlagEnabled*>(TrackEntry[Index1]);
								//newTrack->FlagEnabled = TrackFlagEnabled;
							*/
							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackFlagLacing::ClassInfos.GlobalId) {
								//KaxTrackFlagLacing &TrackFlagLacing = *static_cast<KaxTrackFlagLacing*>(TrackEntry[Index1]);
								//newTrack->FlagLacing = TrackFlagLacing;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackLanguage::ClassInfos.GlobalId) {
								KaxTrackLanguage &TrackLanguage = *static_cast<KaxTrackLanguage*>(TrackEntry[Index1]);
								newTrack.language = std::string(TrackLanguage);

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackMaxCache::ClassInfos.GlobalId) {
								//KaxTrackMaxCache &TrackMaxCache = *static_cast<KaxTrackMaxCache*>(TrackEntry[Index1]);
								//newTrack->MaxCache = TrackMaxCache;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackMinCache::ClassInfos.GlobalId) {
								//KaxTrackMinCache &TrackMinCache = *static_cast<KaxTrackMinCache*>(TrackEntry[Index1]);
								//newTrack->MinCache = TrackMinCache;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackName::ClassInfos.GlobalId) {
								KaxTrackName &TrackName = *static_cast<KaxTrackName*>(TrackEntry[Index1]);
								newTrack.name = TrackName;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackAudio::ClassInfos.GlobalId) {
								KaxTrackAudio &TrackAudio = *static_cast<KaxTrackAudio*>(TrackEntry[Index1]);

								for (uint32 Index2 = 0; Index2 < TrackAudio.ListSize(); Index2++) {
									if (TrackAudio[Index2]->Generic().GlobalId == KaxAudioBitDepth::ClassInfos.GlobalId) {
										KaxAudioBitDepth &AudioBitDepth = *static_cast<KaxAudioBitDepth*>(TrackAudio[Index2]);
										newTrack.bitsPerSample = AudioBitDepth;
									/* Matroska2
									} else if (TrackAudio[Index2]->Generic().GlobalId == KaxAudioPosition::ClassInfos.GlobalId) {
										KaxAudioPosition &AudioPosition = *static_cast<KaxAudioPosition*>(TrackAudio[Index2]);

										// TODO: Support multi-channel?
										//newTrack->audio->ChannelPositionSize = AudioPosition.GetSize();
										//newTrack->audio->ChannelPosition = new binary[AudioPosition.GetSize()+1];
										//memcpy(newTrack->audio->ChannelPosition, AudioPosition.GetBuffer(), AudioPosition.GetSize());
									*/
									} else if (TrackAudio[Index2]->Generic().GlobalId == KaxAudioChannels::ClassInfos.GlobalId) {
										KaxAudioChannels &AudioChannels = *static_cast<KaxAudioChannels*>(TrackAudio[Index2]);
										newTrack.channels = AudioChannels;

									} else if (TrackAudio[Index2]->Generic().GlobalId == KaxAudioOutputSamplingFreq::ClassInfos.GlobalId) {
										KaxAudioOutputSamplingFreq &AudioOutputSamplingFreq = *static_cast<KaxAudioOutputSamplingFreq*>(TrackAudio[Index2]);
										newTrack.samplesOutputPerSec = AudioOutputSamplingFreq;

									} else if (TrackAudio[Index2]->Generic().GlobalId == KaxAudioSamplingFreq::ClassInfos.GlobalId) {
										KaxAudioSamplingFreq &AudioSamplingFreq = *static_cast<KaxAudioSamplingFreq*>(TrackAudio[Index2]);
										newTrack.samplesPerSec = AudioSamplingFreq;
									}
								}
							}
						}
						if (newTrack.trackNumber != 0xFFFF)
							m_Tracks.push_back(newTrack);
					}
				}
			} else if (EbmlId(*ElementLevel1) == KaxCluster::ClassInfos.GlobalId) {
#if 0
                cluster_entry_ptr newCluster(new MatroskaMetaSeekClusterEntry());
                newCluster->timecode = MAX_UINT64;
                newCluster->filePos = ElementLevel1->GetElementPosition();
                newCluster->clusterNo = m_ClusterIndex.size();
                m_ClusterIndex.push_back(newCluster);
                LOG_INFO_S( "MatroskaParser::Parse(): Got cluster @ " << (uint64) newCluster->filePos );
#endif
				if (bBreakAtClusters) {
					m_IOCallback->setFilePointer(ElementLevel1->GetElementPosition());
					//delete ElementLevel1;
					//ElementLevel1 = NULL;
					//_DELETE(ElementLevel1);
					break;
				}
			} else if (EbmlId(*ElementLevel1) == KaxAttachments::ClassInfos.GlobalId) {
				// Yep, we've found our KaxAttachment element. Now find all attached files
				// contained in this segment.
#if 1
				ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, 0xFFFFFFFFL, true, 1));
				while (ElementLevel2 != NullElement) {
					if (UpperElementLevel > 0) {
						break;
					}
					if (UpperElementLevel < 0) {
						UpperElementLevel = 0;
					}
					if (EbmlId(*ElementLevel2) == KaxAttached::ClassInfos.GlobalId) {
						// We actually found a attached file entry :D
						MatroskaAttachment newAttachment;

						ElementLevel3 = ElementPtr(m_InputStream.FindNextElement(ElementLevel2->Generic().Context, UpperElementLevel, 0xFFFFFFFFL, true, 1));
						while (ElementLevel3 != NullElement) {
							if (UpperElementLevel > 0) {
								break;
							}
							if (UpperElementLevel < 0) {
								UpperElementLevel = 0;
							}

							// Now evaluate the data belonging to this track
							if (EbmlId(*ElementLevel3) == KaxFileName::ClassInfos.GlobalId) {
								KaxFileName &attached_filename = *static_cast<KaxFileName *>(ElementLevel3.get());
								attached_filename.ReadData(m_InputStream.I_O());
								newAttachment.FileName = UTFstring(attached_filename);

							} else if (EbmlId(*ElementLevel3) == KaxMimeType::ClassInfos.GlobalId) {
								KaxMimeType &attached_mime_type = *static_cast<KaxMimeType *>(ElementLevel3.get());
								attached_mime_type.ReadData(m_InputStream.I_O());
								newAttachment.MimeType = std::string(attached_mime_type);

							} else if (EbmlId(*ElementLevel3) == KaxFileDescription::ClassInfos.GlobalId) {
								KaxFileDescription &attached_description = *static_cast<KaxFileDescription *>(ElementLevel3.get());
								attached_description.ReadData(m_InputStream.I_O());
								newAttachment.Description = UTFstring(attached_description);

							} else if (EbmlId(*ElementLevel3) == KaxFileData::ClassInfos.GlobalId) {
								KaxFileData &attached_data = *static_cast<KaxFileData *>(ElementLevel3.get());

								//We don't what to read the data into memory because it could be very large
								//attached_data.ReadData(m_InputStream.I_O());

								//Instead we store the Matroska filename, the start of the data and the length, so we can read it
								//later at the users request. IMHO This will save a lot of memory
								newAttachment.SourceStartPos = attached_data.GetElementPosition() + attached_data.HeadSize();
								newAttachment.SourceDataLength = attached_data.GetSize();
							}

							if (UpperElementLevel > 0) {	// we're coming from ElementLevel4
								UpperElementLevel--;
								ElementLevel3 = ElementLevel4;
								if (UpperElementLevel > 0)
									break;
							} else {
								ElementLevel3->SkipData(m_InputStream, ElementLevel3->Generic().Context);
								ElementLevel3 = ElementPtr(m_InputStream.FindNextElement(ElementLevel2->Generic().Context, UpperElementLevel, 0xFFFFFFFFL, true, 1));
							}					
						} // while (ElementLevel3 != NULL)
						m_AttachmentList.push_back(newAttachment);
					}

					if (UpperElementLevel > 0) {	// we're coming from ElementLevel3
						UpperElementLevel--;
						ElementLevel2 = ElementLevel3;
						if (UpperElementLevel > 0)
							break;
					} else {
						ElementLevel2->SkipData(m_InputStream, ElementLevel2->Generic().Context);
						ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, 0xFFFFFFFFL, true, 1));
					}
				} // while (ElementLevel2 != NULL)
#endif
			}
			
			if (UpperElementLevel > 0) {		// we're coming from ElementLevel2
				UpperElementLevel--;
				//delete ElementLevel1;
				//_DELETE(ElementLevel1);
				ElementLevel1 = ElementLevel2;
				if (UpperElementLevel > 0)
					break;
			} else {
				ElementLevel1->SkipData(m_InputStream, ElementLevel1->Generic().Context);
				//delete ElementLevel1;
				//ElementLevel1 = NULL;
				//_DELETE(ElementLevel1);

				ElementLevel1 = ElementPtr(m_InputStream.FindNextElement(m_ElementLevel0->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, true, 1));
			}
		} // while (ElementLevel1 != NULL)
		//_DELETE(ElementLevel3);
		//_DELETE(ElementLevel2);
		//_DELETE(ElementLevel1);
	} catch (std::exception &e) {
        LOG_ERROR_S( "MatroskaParser::Parse() got exception (" << typeid( e ).name() << "): " << e.what() );
		return 1;
	} catch (...) {
        LOG_ERROR_S( "MatroskaParser::Parse() got unknown exception." );
		return 1;
	}

	CountClusters();
	return 0;
};


MatroskaTagInfo *MatroskaParser::FindTagWithTrackUID(uint64 trackUID) 
{
	MatroskaTagInfo *foundTag = NULL;

	for (size_t t = 0; t < m_Tags.size(); t++)
	{
		MatroskaTagInfo& currentTag = m_Tags.at(t);
		if (currentTag.targetTrackUID == trackUID &&
			currentTag.targetEditionUID == 0 &&
			currentTag.targetChapterUID == 0 &&
			currentTag.targetAttachmentUID == 0)
		{
			foundTag = &currentTag;
			break;
		}
	}

	return foundTag;
};

MatroskaTagInfo *MatroskaParser::FindTagWithEditionUID(uint64 editionUID, uint64 trackUID)
{
	MatroskaTagInfo *foundTag = NULL;

	for (size_t t = 0; t < m_Tags.size(); t++)
	{
		MatroskaTagInfo &currentTag = m_Tags.at(t);
		if (currentTag.targetEditionUID == editionUID &&
			(trackUID == 0 || (currentTag.targetTrackUID == trackUID)))
		{
			foundTag = &currentTag;
			break;
		}
	}

	return foundTag;
};

MatroskaTagInfo *MatroskaParser::FindTagWithChapterUID(uint64 chapterUID, uint64 trackUID)
{
	MatroskaTagInfo *foundTag = NULL;

	for (size_t t = 0; t < m_Tags.size(); t++)
	{
		MatroskaTagInfo &currentTag = m_Tags.at(t);
		if (currentTag.targetChapterUID == chapterUID &&
			(trackUID == 0 || (currentTag.targetTrackUID == trackUID)))
		{
			foundTag = &currentTag;
			break;
		}
	}

	return foundTag;
};

double MatroskaParser::GetDuration()
{ 
	if (m_CurrentChapter != NULL) {
		return TimecodeToSeconds(m_CurrentChapter->timeEnd - m_CurrentChapter->timeStart);
	}
	return m_Duration / 1000000000.0;
}

double MatroskaParser::GetTrackDuration( uint32 trackIdx ) const
{
    return double( m_Tracks.at(trackIdx).defaultDuration ) * double( m_TimecodeScale );
}

int32 MatroskaParser::GetFirstTrack( track_type type ) const
{
    for (uint16 t = 0; t < m_Tracks.size(); t++)
    {
        const MatroskaTrackInfo &currentTrack = m_Tracks.at(t);
        if (currentTrack.trackType == type) return t;
    }
    return -1;
}

uint32 MatroskaParser::GetTrackCount() const
{
    return (uint32) m_Tracks.size();
}

uint32 MatroskaParser::GetTrackCount( track_type type ) const
{
    uint32 count = 0;
    for (uint16 t = 0; t < m_Tracks.size(); t++)
    {
        const MatroskaTrackInfo &currentTrack = m_Tracks.at(t);
        if (currentTrack.trackType == type) count++;
    }
    return count;
}

int32 MatroskaParser::GetTrackIndex(track_type type, uint32 index) const
{
    uint32 idx = 0;
    for (uint16 t = 0; t < m_Tracks.size(); t++)
    {
        const MatroskaTrackInfo &currentTrack = m_Tracks.at(t);
        if (currentTrack.trackType == type)
        {
            if(idx == index) return t;
            else idx++;
        }
    }
    return -1;
}


void MatroskaParser::EnableTrack(uint32 newTrackIdx)
{
        // This is only kept for the sake of the legacy hack in the seekable path,
        //  which I'm not currently using.  That should be tidied up and this removed.
	m_CurrentTrackNo = newTrackIdx;

    m_EnabledTrackNumbers.insert( m_Tracks.at(newTrackIdx).trackNumber );
    m_FrameQueues.insert( std::pair< uint32, FrameQueue >( newTrackIdx, FrameQueue() ) );
}


bool MatroskaParser::TrackNumIsEnabled( uint16 trackNum ) const
{
    return m_EnabledTrackNumbers.find( trackNum ) != m_EnabledTrackNumbers.end();
}


bool MatroskaParser::IsAnyQueueFull() const
{
    if (m_MaxQueueDepth == 0) return false;

    for(FrameQueueMap::const_iterator track = m_FrameQueues.begin(); track != m_FrameQueues.end(); ++track)
    {
        if (track->second.size() >= (size_t) m_MaxQueueDepth) return true;
    }

    return false;
}


uint16 MatroskaParser::FindTrack( uint16 trackNum ) const
{
    for (size_t i = 0; i != m_Tracks.size(); i++)
    {
        if (m_Tracks[i].trackNumber == trackNum) return (uint16) i;
    }
    return 0xffff;  // we shouldn't get here, since any access to this function should be guarded by other checks.
}


void MatroskaParser::SetSubSong(int subsong)
{
	// As we don't (yet?) use several Editions, select the first (default) one as the current one.
	m_CurrentChapter = NULL;
	if (subsong < 0 || m_Chapters.size() > (size_t) subsong)
		m_CurrentChapter = &m_Chapters.at((size_t) subsong);
	
};


void MatroskaParser::SetMaxQueueDepth( unsigned int depth )
{
    m_MaxQueueDepth = depth;
}


int32 MatroskaParser::GetAvgBitrate() 
{ 
	double ret = 0;
	ret = static_cast<double>(int64(m_FileSize)) / 1024;
	ret = ret / (m_Duration / 1000000000.0);
	ret = ret * 8;
	return static_cast<int32>(ret);
};

bool MatroskaParser::skip_frames_until(double destination, unsigned hint_samplerate)
{
    bool have_data = false;
    while (!have_data)
    {
        for(FrameQueueMap::iterator track = m_FrameQueues.begin(); track != m_FrameQueues.end(); ++track)
        {
            FrameQueue &track_queue = track->second;
            while (!track_queue.empty() && TimecodeToSeconds( track_queue.front().timecode, hint_samplerate ) < destination) track_queue.pop_front();

            if (!track_queue.empty()) have_data = true;
        }

        if (!have_data && (FillQueue() > 0)) return false;
    }

    return true;
}

bool MatroskaParser::Seek(double seconds, unsigned samplerate_hint)
{
	if (m_CurrentChapter != NULL) {
		seconds += (double)(int64)m_CurrentChapter->timeStart / 1000000000; // ns -> seconds
	}

	uint64 seekToTimecode = SecondsToTimecode(seconds);
	
	m_CurrentTimecode = seekToTimecode;

	if (!skip_frames_until(seconds, samplerate_hint)) return false;

	m_CurrentTimecode = seekToTimecode;
	return (FillQueue() > 0);
};

MatroskaFrame * MatroskaParser::ReadSingleFrame( uint16 trackIdx )
{
    FrameQueueMap::iterator track = m_FrameQueues.find( trackIdx );
    if (track == m_FrameQueues.end()) return NULL;

    FrameQueue &track_queue = track->second;
    while (track_queue.empty())
    {
        if (FillQueue() != 0) return NULL;
    }

    if (track_queue.empty()) return NULL;

    return track_queue.pop_front().release();
};

bool MatroskaParser::Restart()
{
    m_Eof = false;
    m_CurrentChapter = NULL;
    for (FrameQueueMap::iterator track = m_FrameQueues.begin(); track != m_FrameQueues.end(); ++track)
    {
        track->second.clear();
    }

    unsigned int samplerate_hint = 0;   // this value is unused & should probably be removed.
    return Seek( 0.0, samplerate_hint );
};

const MatroskaParser::attachment_list &MatroskaParser::GetAttachmentList() const
{
    return m_AttachmentList;
}


ByteArray MatroskaParser::ReadAttachment( attachment_list::const_iterator attachment )
{
    ByteArray result( attachment->SourceDataLength );

    uint64 oldpos = m_IOCallback->getFilePointer();
    m_IOCallback->setFilePointer( attachment->SourceStartPos );

    uint32 num_read = m_IOCallback->read( &result.front(), attachment->SourceDataLength );
    if (num_read != attachment->SourceDataLength) throw std::runtime_error(
        boost::str( boost::format( "MatroskaParser::ReadAttachment() got %d bytes instead of %d" )
            % num_read % attachment->SourceDataLength ) );

    m_IOCallback->setFilePointer( oldpos );

    return result;
}


bool MatroskaParser::IsEof() const
{
    return m_Eof;
}


typedef boost::shared_ptr<EbmlId> EbmlIdPtr;

struct EbmlIdPrinter
{
    const binary *bytes;
    const uint16 len;

    EbmlIdPrinter( const binary *bytes, uint16 len )
    : bytes( bytes ), len( len )
    {
    }
};

std::ostream &operator<<( std::ostream &os, const EbmlIdPrinter &idp )
{
    const char *sep = "";
    os << "[";
    for (uint16 i = 0; i < idp.len; i++)
    {
        os << sep << std::hex << (int) idp.bytes[i];
        sep = " ";
    }
    os << "]";
    return os;
}

void MatroskaParser::Parse_MetaSeek(ElementPtr metaSeekElement, bool bInfoOnly) 
{
//    TIMER;
	uint64 lastSeekPos = 0;
	uint64 endSeekPos = 0;
	ElementPtr l2;
	ElementPtr l3;
	ElementPtr NullElement;
	int UpperElementLevel = 0;

	if (metaSeekElement == NullElement)
		return;

	l2 = ElementPtr(m_InputStream.FindNextElement(metaSeekElement->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, true, 1));
	while (l2 != NullElement) {
		if (UpperElementLevel > 0) {
            LOG_DEBUG_S( "MatroskaParser::Parse_MetaSeek(): UpperElementLevel = " << UpperElementLevel );
			break;
		}
		if (UpperElementLevel < 0) {
			UpperElementLevel = 0;
		}
        if (bInfoOnly) {
            if (m_ClusterIndex.size() >= 1) break;
        }

		if (EbmlId(*l2) == KaxSeek::ClassInfos.GlobalId) {
			//Wow we found the SeekEntries, time to speed up reading ;)
			l3 = ElementPtr(m_InputStream.FindNextElement(l2->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, true, 1));

			EbmlIdPtr id;
			while (l3 != NullElement) {
				if (UpperElementLevel > 0) {
					break;
				}
				if (UpperElementLevel < 0) {
					UpperElementLevel = 0;
				}
                if (bInfoOnly) {
                    if (m_ClusterIndex.size() >= 1) break;
                }

				if (EbmlId(*l3) == KaxSeekID::ClassInfos.GlobalId) {
					binary *b = NULL;
					uint16 s = 0;
					KaxSeekID &seek_id = static_cast<KaxSeekID &>(*l3);
					seek_id.ReadData(m_InputStream.I_O(), SCOPE_ALL_DATA);
					b = seek_id.GetBuffer();
					s = (uint16)seek_id.GetSize();
                    id.reset();
					id = EbmlIdPtr(new EbmlId(b, s));
                    LOG_INFO_S( "MatroskaParser::Parse_MetaSeek(): Found position for element" << EbmlIdPrinter( b, s ) );

				} else if (EbmlId(*l3) == KaxSeekPosition::ClassInfos.GlobalId) {
					KaxSeekPosition &seek_pos = static_cast<KaxSeekPosition &>(*l3);
					seek_pos.ReadData(m_InputStream.I_O());				
					lastSeekPos = uint64(seek_pos);
					if (endSeekPos < lastSeekPos)
						endSeekPos = uint64(seek_pos);

					if (*id == KaxCluster::ClassInfos.GlobalId) {
						//NOTE1("Found Cluster Seek Entry Postion: %u", (unsigned long)lastSeekPos);
						//uint64 orig_pos = inputFile.getFilePointer();
						//MatroskaMetaSeekClusterEntry newCluster;
                        cluster_entry_ptr newCluster(new MatroskaMetaSeekClusterEntry());
						newCluster->timecode = MAX_UINT64;
						newCluster->filePos = static_cast<KaxSegment *>(m_ElementLevel0.get())->GetGlobalPosition(lastSeekPos);
						m_ClusterIndex.push_back(newCluster);
                        LOG_INFO_S( "MatroskaParser::Parse_MetaSeek(): Got cluster @ " << (uint64) newCluster->filePos );

					} else if (*id == KaxSeekHead::ClassInfos.GlobalId) {
						LOG_INFO_S("Found MetaSeek Seek Entry Postion: " << lastSeekPos);
						uint64 orig_pos = m_IOCallback->getFilePointer();
						m_IOCallback->setFilePointer(static_cast<KaxSegment *>(m_ElementLevel0.get())->GetGlobalPosition(lastSeekPos));
						
						ElementPtr levelUnknown = ElementPtr(m_InputStream.FindNextID(KaxSeekHead::ClassInfos, 0xFFFFFFFFFFFFFFFFL));										
						Parse_MetaSeek(levelUnknown, bInfoOnly);

						m_IOCallback->setFilePointer(orig_pos);
					}

				} else {

				}
				l3->SkipData(m_InputStream, l3->Generic().Context);
				l3 = ElementPtr(m_InputStream.FindNextElement(l2->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, true, 1));
			}
		} else {

		}

		if (UpperElementLevel > 0) {    // we're coming from l3
			UpperElementLevel--;
			l2 = l3;
			if (UpperElementLevel > 0)
				break;

		} else {
			l2->SkipData(m_InputStream, l2->Generic().Context);
			l2 = ElementPtr(m_InputStream.FindNextElement(metaSeekElement->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, true, 1));
		}
	}
//    _TIMER("Parse_MetaSeek");
}

#define IS_ELEMENT_ID(__x__) (Element->Generic().GlobalId == __x__::ClassInfos.GlobalId)

void MatroskaParser::Parse_Chapter_Atom(KaxChapterAtom *ChapterAtom)
{
	Parse_Chapter_Atom(ChapterAtom, m_Chapters);
}

void MatroskaParser::Parse_Chapter_Atom(KaxChapterAtom *ChapterAtom, std::vector<MatroskaChapterInfo> &p_chapters)
{
	EbmlElement *Element = NULL;
	MatroskaChapterInfo newChapter;

	LOG_INFO_S("New chapter");

	for (uint32 i = 0; i < ChapterAtom->ListSize(); i++)
	{	
		Element = (*ChapterAtom)[i];
				
		if (IS_ELEMENT_ID(KaxChapterUID))
		{
			newChapter.chapterUID = uint64(*static_cast<EbmlUInteger *>(Element));
			LOG_INFO_S("- UID : " << newChapter.chapterUID);
		}
		else if(IS_ELEMENT_ID(KaxChapterTimeStart))
		{							
			newChapter.timeStart = uint64(*static_cast<EbmlUInteger *>(Element)); // it's in ns
			LOG_INFO_S("- TimeStart : " << newChapter.timeStart);
		}
		else if(IS_ELEMENT_ID(KaxChapterTimeEnd))
		{
			newChapter.timeEnd = uint64(*static_cast<EbmlUInteger *>(Element)); // it's in ns
			LOG_INFO_S("- TimeEnd : " << newChapter.timeEnd);
		}
		else if(IS_ELEMENT_ID(KaxChapterTrack))
		{
			KaxChapterTrack *ChapterTrack = (KaxChapterTrack *)Element;
			
			for (uint32 j = 0; j < ChapterTrack->ListSize(); j++)
			{
				Element = (*ChapterTrack)[j];
				if(IS_ELEMENT_ID(KaxChapterTrackNumber))
				{
					uint64 chapTrackNo = uint64(*static_cast<EbmlUInteger *>(Element));
					newChapter.tracks.push_back(chapTrackNo);
					LOG_INFO_S("- TrackNumber : " << chapTrackNo);
				}
				else if(IS_ELEMENT_ID(KaxChapterAtom))
				{									
					// Ignore sub-chapter
					LOG_INFO("ignore sub-chapter");
					//Parse_Chapter_Atom((KaxChapterAtom *)Element);
				}
			}
		}
		else if(IS_ELEMENT_ID(KaxChapterDisplay))
		{
			// A new chapter display string+lang+country
			MatroskaChapterDisplayInfo newChapterDisplay;							
			KaxChapterDisplay *ChapterDisplay = (KaxChapterDisplay *)Element;
			
			for (uint32 j = 0; j < ChapterDisplay->ListSize(); j++) {
				Element = (*ChapterDisplay)[j];
				if(IS_ELEMENT_ID(KaxChapterString))
				{
					newChapterDisplay.string = UTFstring(*static_cast <EbmlUnicodeString *>(Element)).c_str();
					LOG_INFO_S("- String : " << newChapterDisplay.string.GetUTF8());
				}
				else if(IS_ELEMENT_ID(KaxChapterAtom))
				{									
					// Ignore sub-chapter
					LOG_INFO("ignore sub-chapter");
					//Parse_Chapter_Atom((KaxChapterAtom *)Element);
				}
			}
			// A emtpy string in a chapter display string is usless
			if (newChapterDisplay.string.length() > 0)
				newChapter.display.push_back(newChapterDisplay);
		}
		else if(IS_ELEMENT_ID(KaxChapterAtom))
		{
			/*
			// Ignore sub-chapter
			NOTE("ignore sub-chapter");
			//Parse_Chapter_Atom((KaxChapterAtom *)Element);
			*/
			Parse_Chapter_Atom((KaxChapterAtom *)Element, newChapter.subChapters);
		}
	}
	if ((newChapter.chapterUID != 0) && !FindChapterUID(newChapter.chapterUID))
		p_chapters.push_back(newChapter);
}

void MatroskaParser::Parse_Chapters(KaxChapters *chaptersElement)
{
	EbmlElement *Element = NULL;	
	int UpperEltFound = 0;

	LOG_INFO("New edition");

	if (chaptersElement == NULL)
		return;

	chaptersElement->Read(m_InputStream, KaxChapters::ClassInfos.Context,
		UpperEltFound, Element, true);

	for (uint32 i = 0; i < chaptersElement->ListSize(); i++)
	{
		Element = (*chaptersElement)[i];
		if(IS_ELEMENT_ID(KaxEditionEntry))
		{
			MatroskaEditionInfo newEdition;
			KaxEditionEntry *edition = (KaxEditionEntry *)Element;
			for (uint32 j = 0; j < edition->ListSize(); j++)
			{
				Element = (*edition)[j];
				if(IS_ELEMENT_ID(KaxEditionUID))
				{
					// A new edition :)
					newEdition.editionUID = uint64(*static_cast<EbmlUInteger *>(Element));
					LOG_INFO_S("- UID : " << newEdition.editionUID);
				}
				else if(IS_ELEMENT_ID(KaxChapterAtom))
				{
					// A new chapter :)
					Parse_Chapter_Atom((KaxChapterAtom *)Element);
				}
			}
			if ((newEdition.editionUID != 0) && !FindEditionUID(newEdition.editionUID))
				m_Editions.push_back(newEdition);
		}
	}
	FixChapterEndTimes();
}

void MatroskaParser::Parse_Tags(KaxTags *tagsElement)
{
	EbmlElement *Element = NULL;
	int UpperEltFound = 0;

	if (tagsElement == NULL)
		return;

	m_TagPos = tagsElement->GetElementPosition();
	m_TagSize = (uint32) tagsElement->GetSize();

	tagsElement->Read(m_InputStream, KaxTags::ClassInfos.Context, UpperEltFound, Element, true);

	for (uint32 i = 0; i < tagsElement->ListSize(); i++)
	{
		Element = (*tagsElement)[i];
		if(IS_ELEMENT_ID(KaxTag))
		{
			MatroskaTagInfo newTag;
			newTag.targetTypeValue = 50;
			KaxTag *tagElement = (KaxTag*)Element;
			LOG_INFO("New Tag");
			for (uint32 j = 0; j < tagElement->ListSize(); j++)
			{
				Element = (*tagElement)[j];
				if(IS_ELEMENT_ID(KaxTagTargets))
				{
					KaxTagTargets *tagTargetsElement = (KaxTagTargets*)Element;					
					for (uint32 k = 0; k < tagTargetsElement->ListSize(); k++)
					{
						Element = (*tagTargetsElement)[k];
						if(IS_ELEMENT_ID(KaxTagTrackUID))
						{
							newTag.targetTrackUID = uint64(*static_cast<EbmlUInteger *>(Element));
							LOG_INFO_S("- TargetTrackUID : " << newTag.targetTrackUID);
						}
						else if(IS_ELEMENT_ID(KaxTagEditionUID))
						{
							newTag.targetEditionUID = uint64(*static_cast<EbmlUInteger *>(Element));
							LOG_INFO_S("- TargetEditionUID : " << newTag.targetEditionUID);
						}
						else if(IS_ELEMENT_ID(KaxTagChapterUID))
						{
							newTag.targetChapterUID = uint64(*static_cast<EbmlUInteger *>(Element));
							LOG_INFO_S("- TargetChapterUIDUID : " << newTag.targetChapterUID);
						}
						else if(IS_ELEMENT_ID(KaxTagAttachmentUID))
						{
							newTag.targetAttachmentUID = uint64(*static_cast<EbmlUInteger *>(Element));
							LOG_INFO_S("- TargetAttachmentUID : " << newTag.targetAttachmentUID);
						}
						else if(IS_ELEMENT_ID(KaxTagTargetTypeValue))
						{
							newTag.targetTypeValue = uint32(*static_cast<EbmlUInteger *>(Element));
							LOG_INFO_S("- TargetTypeValue : " << newTag.targetTypeValue);
						}
						else if(IS_ELEMENT_ID(KaxTagTargetType))
						{
							newTag.targetType = std::string(*static_cast<KaxTagTargetType *>(Element));
							LOG_INFO_S("- TargetType : " << newTag.targetType);
						}
					}
				}
				else if(IS_ELEMENT_ID(KaxTagSimple))
				{
					MatroskaSimpleTag newSimpleTag;
					KaxTagSimple *tagSimpleElement = (KaxTagSimple*)Element;
					LOG_INFO("New SimpleTag");
					for (uint32 k = 0; k < tagSimpleElement->ListSize(); k++)
					{
						Element = (*tagSimpleElement)[k];
						if(IS_ELEMENT_ID(KaxTagName))
						{
							UTFstring str_utf(*static_cast <EbmlUnicodeString *>(Element));
							std::string str_utf8 = str_utf.GetUTF8();
							std::string str_upper = boost::algorithm::to_upper_copy(str_utf8);
							newSimpleTag.name.SetUTF8(str_upper);
							LOG_INFO_S("- Name : " << newSimpleTag.name.GetUTF8());
						}
						else if(IS_ELEMENT_ID(KaxTagString))
						{
							newSimpleTag.value = UTFstring(*static_cast <EbmlUnicodeString *>(Element)).c_str();
							LOG_INFO_S("- Value : " << newSimpleTag.value.GetUTF8());
						}
						else if(IS_ELEMENT_ID(KaxTagDefault))
						{
							newSimpleTag.defaultFlag = uint32(*static_cast<EbmlUInteger *>(Element));
							LOG_INFO_S("- TargetTypeValue : " << newSimpleTag.defaultFlag);
						}
						else if(IS_ELEMENT_ID(KaxTagLangue))
						{
							newSimpleTag.language = std::string(*static_cast <KaxTagLangue *>(Element));
							LOG_INFO_S("- Language : " << newSimpleTag.language);
						}
						else if(IS_ELEMENT_ID(KaxTagSimple))
						{
							// ignore sub-tags
							// if we want "sub-tags" put this loop in another
							// function and call it recursively
						}
					}
					newTag.tags.push_back(newSimpleTag);
				}
			}
			m_Tags.push_back(newTag);
		}
	}
};


int MatroskaParser::FillQueue() 
{
	LOG_DEBUG("MatroskaParser::FillQueue()");

    if (IsAnyQueueFull())
    {
        LOG_WARN_S( "MatroskaParser::FillQueue(): not filling because another queue is full." );
        return -1;
    }

	int UpperElementLevel = 0;
	bool bAllowDummy = false;
	// Elements for different levels
	ElementPtr ElementLevel1;
	ElementPtr ElementLevel2;
	ElementPtr ElementLevel3;
	ElementPtr ElementLevel4;
    ElementPtr ElementLevel5;
	ElementPtr NullElement;

	if (IsSeekable(*m_IOCallback)) {
		cluster_entry_ptr currentCluster = FindCluster(m_CurrentTimecode);
		if (currentCluster.get() == NULL)
		{
			LOG_ERROR_S( "MatroskaParser::FillQueue(): can't find cluster at current timecode: " << m_CurrentTimecode );
			return 2;
		}
        else LOG_DEBUG_S( "MatroskaParser::FillQueue(): got cluster at timecode: " << m_CurrentTimecode );
		int64 clusterFilePos = currentCluster->filePos;

		LOG_DEBUG_S("cluster " << currentCluster->clusterNo);

		m_IOCallback->setFilePointer(clusterFilePos);
		// Find the element data
		ElementLevel1 = ElementPtr(m_InputStream.FindNextID(KaxCluster::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (ElementLevel1 == NullElement)
		{
			LOG_INFO_S( "MatroskaParser::FillQueue(): got NullElement" );
			m_Eof = true;
			return 1;
		}

		if (EbmlId(*ElementLevel1) == KaxCluster::ClassInfos.GlobalId) {
			KaxCluster *SegmentCluster = static_cast<KaxCluster *>(ElementLevel1.get());
			uint32 ClusterTimecode = 0;
			MatroskaFrame *prevFrame = NULL;

			// read blocks and discard the ones we don't care about
			ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, ElementLevel1->ElementSize(), bAllowDummy));
			while (ElementLevel2 != NullElement) {
				if (UpperElementLevel > 0) {
					break;
				}
				if (UpperElementLevel < 0) {
					UpperElementLevel = 0;
				}
				if (EbmlId(*ElementLevel2) == KaxClusterTimecode::ClassInfos.GlobalId) {						
					KaxClusterTimecode & ClusterTime = *static_cast<KaxClusterTimecode*>(ElementLevel2.get());
					ClusterTime.ReadData(m_InputStream.I_O());
					ClusterTimecode = uint32(ClusterTime);
					currentCluster->timecode = ClusterTimecode * m_TimecodeScale;
					SegmentCluster->InitTimecode(ClusterTimecode, m_TimecodeScale);
				} else  if (EbmlId(*ElementLevel2) == KaxBlockGroup::ClassInfos.GlobalId) {
					//KaxBlockGroup & aBlockGroup = *static_cast<KaxBlockGroup*>(ElementLevel2);

					// Create a new frame
					MatroskaFrame *newFrame = new MatroskaFrame();
                    uint16 trackIdx = 0xffff;   // track of frame.

					ElementLevel3 = ElementPtr(m_InputStream.FindNextElement(ElementLevel2->Generic().Context, UpperElementLevel, ElementLevel2->ElementSize(), bAllowDummy));
					while (ElementLevel3 != NullElement) {
						if (UpperElementLevel > 0) {
							break;
						}
						if (UpperElementLevel < 0) {
							UpperElementLevel = 0;
						}
						if (EbmlId(*ElementLevel3) == KaxBlock::ClassInfos.GlobalId) {
							KaxBlock & DataBlock = *static_cast<KaxBlock*>(ElementLevel3.get());														
							DataBlock.ReadData(m_InputStream.I_O());
							DataBlock.SetParent(*SegmentCluster);

							//NOTE4("Track # %u / %u frame%s / Timecode %I64d", DataBlock.TrackNum(), DataBlock.NumberFrames(), (DataBlock.NumberFrames() > 1)?"s":"", DataBlock.GlobalTimecode()/m_TimecodeScale);
							uint16 trackNum = DataBlock.TrackNum();
							if (TrackNumIsEnabled( trackNum ))
                            {
								trackIdx = FindTrack( trackNum );
								MatroskaTrackInfo &track = m_Tracks[trackIdx];

								newFrame->timecode = DataBlock.GlobalTimecode();

								if (DataBlock.NumberFrames() > 1) {	
									// The evil lacing has been used
									newFrame->duration = track.defaultDuration * DataBlock.NumberFrames();

									newFrame->dataBuffer.resize(DataBlock.NumberFrames());
									for (uint32 f = 0; f < DataBlock.NumberFrames(); f++) {
										DataBuffer &buffer = DataBlock.GetBuffer(f);
										newFrame->dataBuffer[f].resize(buffer.Size());								
										memcpy(&newFrame->dataBuffer[f][0], buffer.Buffer(), buffer.Size());
									}
								} else {
									// Non-lacing block		
									newFrame->duration = track.defaultDuration;

									newFrame->dataBuffer.resize(1);
									DataBuffer &buffer = DataBlock.GetBuffer(0);
									newFrame->dataBuffer.at(0).resize(buffer.Size());
                                        
									memcpy(&newFrame->dataBuffer.at(0).at(0), buffer.Buffer(), buffer.Size());
								}
							} else {
								//newFrame->timecode = MAX_UINT64;
							}
						/*
						} else if (EbmlId(*ElementLevel3) == KaxReferenceBlock::ClassInfos.GlobalId) {
							KaxReferenceBlock & RefTime = *static_cast<KaxReferenceBlock*>(ElementLevel3);
							RefTime.ReadData(m_InputStream.I_O());
							newFrame->frameReferences.push_back(int32(RefTime));
							//wxLogDebug("  Reference frame at scaled (%d) timecode %ld\n", int32(RefTime), int32(int64(RefTime) * TimecodeScale));
							*/
						} else if (EbmlId(*ElementLevel3) == KaxBlockDuration::ClassInfos.GlobalId) {
							KaxBlockDuration & BlockDuration = *static_cast<KaxBlockDuration*>(ElementLevel3.get());
							BlockDuration.ReadData(m_InputStream.I_O());
							newFrame->duration = uint64(BlockDuration);
                        } else if (EbmlId(*ElementLevel3) == KaxBlockAdditions::ClassInfos.GlobalId) {
                            ElementLevel4 = ElementPtr(m_InputStream.FindNextElement(ElementLevel3->Generic().Context, UpperElementLevel, 0xFFFFFFFFL, bAllowDummy));
                            while (ElementLevel4 != NullElement) {
                                if (UpperElementLevel > 0) {
							        break;
						        }
						        if (UpperElementLevel < 0) {
							        UpperElementLevel = 0;
						        }
                                if (EbmlId(*ElementLevel4) == KaxBlockMore::ClassInfos.GlobalId) {
                                    ElementLevel5 = ElementPtr(m_InputStream.FindNextElement(ElementLevel4->Generic().Context, UpperElementLevel, 0xFFFFFFFFL, bAllowDummy));
                                    while (ElementLevel5 != NullElement) {
                                        if (UpperElementLevel > 0) {
							                break;
						                }
						                if (UpperElementLevel < 0) {
							                UpperElementLevel = 0;
						                }
                                        if (EbmlId(*ElementLevel5) == KaxBlockAddID::ClassInfos.GlobalId) {
                                            KaxBlockAddID & AddId = *static_cast<KaxBlockAddID*>(ElementLevel5.get());
                                            AddId.ReadData(m_InputStream.I_O());
                                            newFrame->add_id = uint64(AddId);
                                        } else if (EbmlId(*ElementLevel5) == KaxBlockAdditional::ClassInfos.GlobalId) {
                                            KaxBlockAdditional & DataBlockAdditional = *static_cast<KaxBlockAdditional*>(ElementLevel5.get());														
							                DataBlockAdditional.ReadData(m_InputStream.I_O());		
                                            newFrame->additional_data_buffer.resize(DataBlockAdditional.GetSize());
                                            if (!newFrame->add_id) {
                                                newFrame->add_id = 1;
                                            }
                                            memcpy(&newFrame->additional_data_buffer.at(0), DataBlockAdditional.GetBuffer(), DataBlockAdditional.GetSize());
                                        }
                                        ElementLevel5->SkipData(m_InputStream, ElementLevel5->Generic().Context);
							            ElementLevel5 = ElementPtr(m_InputStream.FindNextElement(ElementLevel4->Generic().Context, UpperElementLevel, ElementLevel4->ElementSize(), bAllowDummy));
                                    }
                                }
                                if (UpperElementLevel > 0) {
							        UpperElementLevel--;
							        ElementLevel4 = ElementLevel5;
							        if (UpperElementLevel > 0)
								        break;
						        } else {
							        ElementLevel4->SkipData(m_InputStream, ElementLevel4->Generic().Context);
							        ElementLevel4 = ElementPtr(m_InputStream.FindNextElement(ElementLevel3->Generic().Context, UpperElementLevel, ElementLevel3->ElementSize(), bAllowDummy));
						        }
                            }
                        }
						if (UpperElementLevel > 0) {
							UpperElementLevel--;
							ElementLevel3 = ElementLevel4;
							if (UpperElementLevel > 0)
								break;
						} else {
							ElementLevel3->SkipData(m_InputStream, ElementLevel3->Generic().Context);

							ElementLevel3 = ElementPtr(m_InputStream.FindNextElement(ElementLevel2->Generic().Context, UpperElementLevel, ElementLevel2->ElementSize(), bAllowDummy));
						}							
						//newFrame = new MatroskaReadFrame();
					}
					if (newFrame->dataBuffer.size()>0) {
                        FrameQueueMap::iterator track = m_FrameQueues.find( trackIdx );
                        if (track == m_FrameQueues.end()) continue;

                        FrameQueue &track_queue = track->second;
						track_queue.push_back( newFrame );
						if (prevFrame != NULL && prevFrame->duration == 0) {
							prevFrame->duration = newFrame->timecode - prevFrame->timecode;
							//if (newFrame->duration == 0)
							//	newFrame->duration = prevFrame->duration;
						}

						// !!!!!!!!!!!!!!! HACK ALERT !!!!!!!!!!!!!!!!!!!!!!!!
						// This is an ugly hack to keep us from re-seeking to the same cluster
						m_CurrentTimecode = newFrame->timecode + (newFrame->duration * 2);
						if (newFrame->duration == 0) {
							m_CurrentTimecode += (int64)m_Tracks.at(m_CurrentTrackNo).defaultDuration * 2;
						}
						// !!!!!!!!!!!!!!! HACK ALERT !!!!!!!!!!!!!!!!!!!!!!!!

						prevFrame = newFrame;
                    } else {
                        LOG_INFO("newFrame ==!! delete!!");
                        _DELETE(newFrame);
                    }
				}

				if (UpperElementLevel > 0) {
					UpperElementLevel--;
					//delete ElementLevel2;
					//_DELETE(ElementLevel2);
					ElementLevel2 = ElementLevel3;
					if (UpperElementLevel > 0)
						break;
				} else {
					ElementLevel2->SkipData(m_InputStream, ElementLevel2->Generic().Context);
					//if (ElementLevel2 != pChecksum)
					//	delete ElementLevel2;								
					//ElementLevel2 = NULL;
					//_DELETE(ElementLevel2);

					ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, ElementLevel1->ElementSize(), bAllowDummy));
				}
			}
		}
		//_DELETE(ElementLevel3);
		//_DELETE(ElementLevel2);
		//_DELETE(ElementLevel1);
		//delete ElementLevel1;
		
		if (currentCluster->clusterNo < m_ClusterIndex.size()-1) {
			if (m_ClusterIndex.at(currentCluster->clusterNo+1)->timecode == MAX_UINT64)
				m_ClusterIndex.at(currentCluster->clusterNo+1)->timecode = GetClusterTimecode(m_ClusterIndex.at(currentCluster->clusterNo+1)->filePos);
			m_CurrentTimecode = m_ClusterIndex.at(currentCluster->clusterNo+1)->timecode;
			if(m_CurrentTimecode == 0)
			{
				LOG_INFO_S( (boost::format("clusterNo : %d, m_ClusterIndex.size() : %d")
					% currentCluster->clusterNo % (int) m_ClusterIndex.size()) );
				LOG_INFO("m_CurrentTimecode == 0 (a)");
			}
		} else {
			m_CurrentTimecode = MAX_UINT64;
		}
		
	} else {
		// Find the element data
		ElementLevel1 = ElementPtr(m_InputStream.FindNextID(KaxCluster::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (ElementLevel1 == NullElement)
		{
			LOG_INFO_S( "MatroskaParser::FillQueue(): got NullElement" );
			m_Eof = true;
			return 1;
		}

		if (EbmlId(*ElementLevel1) == KaxCluster::ClassInfos.GlobalId) {
			KaxCluster *SegmentCluster = static_cast<KaxCluster *>(ElementLevel1.get());
			uint32 ClusterTimecode = 0;

			// read blocks and discard the ones we don't care about
			ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, ElementLevel1->ElementSize(), bAllowDummy));
			while (ElementLevel2 != NullElement) {
				if (UpperElementLevel > 0) {
                    LOG_WARN_S( "MatroskaParser::FillQueue(): UpperElementLevel = " << UpperElementLevel << " at line " << __LINE__ );
					break;
				}
				if (UpperElementLevel < 0) {
					UpperElementLevel = 0;
				}
				if (EbmlId(*ElementLevel2) == KaxClusterTimecode::ClassInfos.GlobalId) {						
					KaxClusterTimecode & ClusterTime = *static_cast<KaxClusterTimecode*>(ElementLevel2.get());
					ClusterTime.ReadData(m_InputStream.I_O());
					ClusterTimecode = uint32(ClusterTime);
					SegmentCluster->InitTimecode(ClusterTimecode, m_TimecodeScale);
				} else  if (EbmlId(*ElementLevel2) == KaxBlockGroup::ClassInfos.GlobalId) {
					//KaxBlockGroup & aBlockGroup = *static_cast<KaxBlockGroup*>(ElementLevel2);

					// Create a new frame
					MatroskaFrame *newFrame = new MatroskaFrame();
                    uint16 trackIdx = 0xffff;   // track of frame.

					ElementLevel3 = ElementPtr(m_InputStream.FindNextElement(ElementLevel2->Generic().Context, UpperElementLevel, ElementLevel2->ElementSize(), bAllowDummy));
					while (ElementLevel3 != NullElement) {
						if (UpperElementLevel > 0) {
                            LOG_DEBUG_S( "MatroskaParser::FillQueue(): UpperElementLevel = " << UpperElementLevel << " at line " << __LINE__ );
							break;
						}
						if (UpperElementLevel < 0) {
							UpperElementLevel = 0;
						}
						if (EbmlId(*ElementLevel3) == KaxBlock::ClassInfos.GlobalId) {								
							KaxBlock & DataBlock = *static_cast<KaxBlock*>(ElementLevel3.get());														
							DataBlock.ReadData(m_InputStream.I_O());		
							DataBlock.SetParent(*SegmentCluster);

							//NOTE4("Track # %u / %u frame%s / Timecode %I64d", DataBlock.TrackNum(), DataBlock.NumberFrames(), (DataBlock.NumberFrames() > 1)?"s":"", DataBlock.GlobalTimecode()/m_TimecodeScale);
							uint16 trackNum = DataBlock.TrackNum();
							if (TrackNumIsEnabled( trackNum ))
                            {
								trackIdx = FindTrack( trackNum );
								MatroskaTrackInfo &track = m_Tracks[trackIdx];

								newFrame->timecode = DataBlock.GlobalTimecode();							

								if (DataBlock.NumberFrames() > 1) {	
									// The evil lacing has been used
									newFrame->duration = track.defaultDuration * DataBlock.NumberFrames();

									newFrame->dataBuffer.resize(DataBlock.NumberFrames());
									for (uint32 f = 0; f < DataBlock.NumberFrames(); f++) {
										DataBuffer &buffer = DataBlock.GetBuffer(f);
										newFrame->dataBuffer[f].resize(buffer.Size());								
										memcpy(&newFrame->dataBuffer[f][0], buffer.Buffer(), buffer.Size());
									}
								} else {
									// Non-lacing block		
									newFrame->duration = track.defaultDuration;
									
									newFrame->dataBuffer.resize(1);
									DataBuffer &buffer = DataBlock.GetBuffer(0);
									newFrame->dataBuffer.at(0).resize(buffer.Size());								
									memcpy(&newFrame->dataBuffer[0][0], buffer.Buffer(), buffer.Size());
								}
							} else {
								//newFrame->timecode = MAX_UINT64;
							}
						/*
						} else if (EbmlId(*ElementLevel3) == KaxReferenceBlock::ClassInfos.GlobalId) {
							KaxReferenceBlock & RefTime = *static_cast<KaxReferenceBlock*>(ElementLevel3);
							RefTime.ReadData(m_InputStream.I_O());
							newFrame->frameReferences.push_back(int32(RefTime));
							//wxLogDebug("  Reference frame at scaled (%d) timecode %ld\n", int32(RefTime), int32(int64(RefTime) * TimecodeScale));
							*/
						} else if (EbmlId(*ElementLevel3) == KaxBlockDuration::ClassInfos.GlobalId) {
							KaxBlockDuration & BlockDuration = *static_cast<KaxBlockDuration*>(ElementLevel3.get());
							BlockDuration.ReadData(m_InputStream.I_O());
							newFrame->duration = uint64(BlockDuration);
						}
						if (UpperElementLevel > 0) {
							UpperElementLevel--;
							//delete ElementLevel3;
							//_DELETE(ElementLevel3);
							ElementLevel3 = ElementLevel4;
							if (UpperElementLevel > 0)
                            {
                                LOG_WARN_S( "MatroskaParser::FillQueue(): UpperElementLevel = " << UpperElementLevel << " at line " << __LINE__ );
								break;
                            }
						} else {
							ElementLevel3->SkipData(m_InputStream, ElementLevel3->Generic().Context);
							//delete ElementLevel3;
							//ElementLevel3 = NULL;
							//_DELETE(ElementLevel3);

							ElementLevel3 = ElementPtr(m_InputStream.FindNextElement(ElementLevel2->Generic().Context, UpperElementLevel, ElementLevel2->ElementSize(), bAllowDummy));
						}							
						//newFrame = new MatroskaReadFrame();
					}
					if (newFrame->dataBuffer.size()>0)
                    {
                        FrameQueueMap::iterator track = m_FrameQueues.find( trackIdx );
                        if (track == m_FrameQueues.end()) continue;

                        FrameQueue &track_queue = track->second;
						track_queue.push_back( newFrame );
                    }
                    else delete newFrame;
				}

				if (UpperElementLevel > 0) {
					UpperElementLevel--;
					//delete ElementLevel2;
					//_DELETE(ElementLevel2);
					ElementLevel2 = ElementLevel3;
					if (UpperElementLevel > 0)
                    {
                        LOG_DEBUG_S( "MatroskaParser::FillQueue(): UpperElementLevel = " << UpperElementLevel << " at line " << __LINE__ );
						break;
                    }
				} else {
					ElementLevel2->SkipData(m_InputStream, ElementLevel2->Generic().Context);
					//if (ElementLevel2 != pChecksum)
					//	delete ElementLevel2;								
					//ElementLevel2 = NULL;
					//_DELETE(ElementLevel2);

					ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, ElementLevel1->ElementSize(), bAllowDummy));
				}
			}
		}
		ElementLevel1->SkipData(m_InputStream, ElementLevel1->Generic().Context);
		//_DELETE(ElementLevel3);
		//_DELETE(ElementLevel2);
		//_DELETE(ElementLevel1);
		//delete ElementLevel1;
	}
    for (FrameQueueMap::iterator track = m_FrameQueues.begin(); track != m_FrameQueues.end(); ++track)
    {
        LOG_INFO_S("MatroskaParser::FillQueue() - trackIdx " << track->first << " now has " << track->second.size() << " frames queued");
    }
	return 0;
};

uint64 MatroskaParser::GetClusterTimecode(uint64 filePos) {	
	try {
		uint64 ret = MAX_UINT64;

		int UpperElementLevel = 0;
		// Elements for different levels
		ElementPtr ElementLevel1;
		ElementPtr ElementLevel2;		
		ElementPtr ElementLevel3;
		ElementPtr NullElement;

		m_IOCallback->setFilePointer(filePos);
		// Find the element data
		ElementLevel1 = ElementPtr(m_InputStream.FindNextID(KaxCluster::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (ElementLevel1 == NullElement)
			return MAX_UINT64;

		if (EbmlId(*ElementLevel1) == KaxCluster::ClassInfos.GlobalId) {
			//KaxCluster *SegmentCluster = static_cast<KaxCluster *>(ElementLevel1);
			//uint32 ClusterTimecode = 0;

			// read blocks and discard the ones we don't care about
			ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, ElementLevel1->ElementSize(), false));
			while (ElementLevel2 != NullElement) {
				if (UpperElementLevel > 0) {
					break;
				}
				if (UpperElementLevel < 0) {
					UpperElementLevel = 0;
				}
				if (EbmlId(*ElementLevel2) == KaxClusterTimecode::ClassInfos.GlobalId) {						
					KaxClusterTimecode & ClusterTime = *static_cast<KaxClusterTimecode*>(ElementLevel2.get());
					ClusterTime.ReadData(m_InputStream.I_O());
					ret = uint64(ClusterTime) * m_TimecodeScale;
					
				}

				if (UpperElementLevel > 0) {
					UpperElementLevel--;
					//delete ElementLevel2;
					//_DELETE(ElementLevel2);
					ElementLevel2 = ElementLevel3;
					if (UpperElementLevel > 0)
						break;
				} else {
					ElementLevel2->SkipData(m_InputStream, ElementLevel2->Generic().Context);
					//if (ElementLevel2 != pChecksum)
					//delete ElementLevel2;								
					//ElementLevel2 = NULL;
					//_DELETE(ElementLevel2);
					ElementLevel2 = NullElement;
					if (ret == MAX_UINT64)
						ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, ElementLevel1->ElementSize(), false));
				}
			}
		}
		//_DELETE(ElementLevel3);
		//_DELETE(ElementLevel2);
		//_DELETE(ElementLevel1);
		//delete ElementLevel1;

		return ret;
	} catch (std::exception &e) {
        LOG_ERROR_S( "Caught exception (" << typeid( e ).name() << "): " << e.what() );
	} catch (...) {
        LOG_ERROR_S( "Caught unknown exception." );
	}	
	return MAX_UINT64;
};

cluster_entry_ptr MatroskaParser::FindCluster(uint64 timecode)
{
	try {
		#ifdef _DEBUG_NO_SEEKING
		static size_t callCount = 0;
		if (callCount < m_ClusterIndex.size())
			return &m_ClusterIndex[callCount++];
		else return NULL;
		#endif

        assert( !m_ClusterIndex.empty() );

		if (timecode == 0)
			// Special case
			return m_ClusterIndex.at(0);
		
		cluster_entry_ptr correctEntry;
		double clusterDuration = (double)(int64)m_ClusterIndex.size() / m_Duration;
		size_t clusterIndex = size_t( clusterDuration * (double)(int64)timecode );
		//int lookCount = 0;
		if (clusterIndex > m_ClusterIndex.size())
			clusterIndex = m_ClusterIndex.size()-1;

		cluster_entry_ptr clusterEntry;
		cluster_entry_ptr prevClusterEntry;
		cluster_entry_ptr nextClusterEntry;
		while (correctEntry == NULL) {
			clusterEntry = m_ClusterIndex.at(clusterIndex);		
			if (clusterIndex > 0)
				prevClusterEntry = m_ClusterIndex.at(clusterIndex-1);
			if (clusterIndex+1 < m_ClusterIndex.size())
				nextClusterEntry = m_ClusterIndex.at(clusterIndex+1);			
			
			// We need timecodes to do good seeking
			if (clusterEntry->timecode == MAX_UINT64) {				
				clusterEntry->timecode = GetClusterTimecode(clusterEntry->filePos);			
			}
			if (prevClusterEntry != NULL && prevClusterEntry->timecode == MAX_UINT64) {
				prevClusterEntry->timecode = GetClusterTimecode(prevClusterEntry->filePos);			
			}
			if (nextClusterEntry != NULL && nextClusterEntry->timecode == MAX_UINT64) {				
				nextClusterEntry->timecode = GetClusterTimecode(nextClusterEntry->filePos);			
			}

			if (clusterEntry->timecode == timecode) {
				// WOW, we are seeking directly to this cluster
				correctEntry = clusterEntry;
				break;
			}

			if (prevClusterEntry != NULL) {
				if (clusterEntry->timecode > timecode && timecode > prevClusterEntry->timecode) {
					// We found it !!!
					correctEntry = prevClusterEntry;
					break;
				}
				if (prevClusterEntry->timecode == timecode) {
					// WOW, we are seeking directly to this cluster
					correctEntry = prevClusterEntry;
					break;
				}
				// Check if we overshot the needed cluster
				if (timecode < prevClusterEntry->timecode) {
					clusterIndex--;
					//lookCount++; // This is how many times we have 'looked'
					continue;
				}
			}
			
			if (nextClusterEntry != NULL) {
				if (clusterEntry->timecode < timecode && timecode < nextClusterEntry->timecode) {
					// We found it !!!
					correctEntry = clusterEntry;
					break;
				}			
				if (nextClusterEntry->timecode == timecode) {
					// WOW, we are seeking directly to this cluster
					correctEntry = nextClusterEntry;
					break;
				}
				// Check if we undershot the needed cluster
				if (timecode > nextClusterEntry->timecode) {
					clusterIndex++;
					//lookCount--;
					continue;
				}
			}
			// We should never get here, unless this is the last cluster
			assert(clusterEntry != NULL);	
			if (timecode <= m_Duration)
				correctEntry = clusterEntry;
			else
				break;
		}

		if (correctEntry != NULL)
        {
			LOG_INFO("MatroskaParser::FindCluster(timecode = %u) seeking to cluster %i at %u",
                (uint32)(timecode / m_TimecodeScale), (uint32) correctEntry->clusterNo, (uint32) correctEntry->filePos);
        }
		else
		{
			LOG_INFO("MatroskaParser::FindCluster(timecode = %u) seeking failed", (uint32)(timecode / m_TimecodeScale));
		}

		return correctEntry;
	} catch (std::exception &e) {
        LOG_ERROR_S( "Caught exception (" << typeid( e ).name() << "): " << e.what() );
	} catch (...) {
        LOG_ERROR_S( "Caught unknown exception." );
	}
    cluster_entry_ptr null_ptr;
    return null_ptr;
}

void MatroskaParser::CountClusters() 
{
	for (uint32 c = 0; c < m_ClusterIndex.size(); c++) {
		cluster_entry_ptr clusterEntry = m_ClusterIndex.at(c);		
		clusterEntry->clusterNo = c;
	}
}

void MatroskaParser::FixChapterEndTimes()
{
	if (m_Chapters.size() > 0) {
		MatroskaChapterInfo *nextChapter = &m_Chapters.at(m_Chapters.size()-1);
		if (nextChapter->timeEnd == 0) {
			nextChapter->timeEnd = static_cast<uint64>(m_Duration);
		}
		for (uint32 c = 0; c < m_Chapters.size()-1; c++) {
			MatroskaChapterInfo &currentChapter = m_Chapters.at(c);	
			nextChapter = &m_Chapters.at(c+1);
			if (currentChapter.timeEnd == 0) {
				currentChapter.timeEnd = nextChapter->timeStart;
			}
		}
		nextChapter = &m_Chapters.at(m_Chapters.size()-1);
		if ((nextChapter->timeEnd == 0) || (nextChapter->timeEnd == nextChapter->timeStart)) {
			nextChapter->timeEnd = static_cast<uint64>(m_Duration);
		}
	}	
}

bool MatroskaParser::FindEditionUID(uint64 uid)
{
	for (uint32 c = 0; c < m_Editions.size(); c++) {
		MatroskaEditionInfo &currentEdition = m_Editions.at(c);	
		if (currentEdition.editionUID == uid)
			return true;
	}	
	return false;
}

bool MatroskaParser::FindChapterUID(uint64 uid)
{
	for (uint32 c = 0; c < m_Chapters.size(); c++) {
		MatroskaChapterInfo &currentChapter = m_Chapters.at(c);	
		if (currentChapter.chapterUID == uid)
			return true;
	}	
	return false;
}

void PrintChapters(std::vector<MatroskaChapterInfo> &theChapters) 
{
    LOG_INFO_S( "Got " << theChapters.size() << " chapters" );
	for (uint32 c = 0; c < theChapters.size(); c++) {
		MatroskaChapterInfo &currentChapter = theChapters.at(c);	
		LOG_INFO("Chapter %u, UID: %u", c, (uint32)currentChapter.chapterUID);
		LOG_INFO("\tStart Time: %u", (uint32)currentChapter.timeStart);
		LOG_INFO("\tEnd Time: %u", (uint32)currentChapter.timeEnd);
		for (uint32 d = 0; d < currentChapter.display.size(); d++) {
			LOG_INFO("\tDisplay %u, String: %s Lang: %s", d, currentChapter.display.at(d).string.GetUTF8().c_str(), currentChapter.display.at(d).lang.c_str());
		}
		for (uint32 t = 0; t < currentChapter.tracks.size(); t++) {
			LOG_INFO("\tTrack %u, UID: %u", t, (uint32)currentChapter.tracks.at(t));
		}

	}
};

bool MatroskaSearch::Skip()
{
	int j;

	for (j = 0; j < SEARCH_TABLE_SIZE; j++) skip[j] = SEARCH_PATTERN_SIZE;
	for (j = 0; j < SEARCH_PATTERN_SIZE - 1; j++)
		skip[pattern[j] & 0x00ff] = SEARCH_PATTERN_SIZE-1-j;
	return true;
}

bool MatroskaSearch::Next()
{
	int  j, k, s;
	int  *g;

	if ((g = (int *)malloc(sizeof(int)*SEARCH_PATTERN_SIZE)) == NULL) return false;
	for (j = 0; j < SEARCH_PATTERN_SIZE; j++) next[j] = 2*SEARCH_PATTERN_SIZE - 1 - j;
	j = SEARCH_PATTERN_SIZE;
	for (k = SEARCH_PATTERN_SIZE - 1; k >= 0; k--) {
		g[k] = j;
		while (j != SEARCH_PATTERN_SIZE && pattern[j] != pattern[k]) {
			next[j] = (next[j] <= SEARCH_PATTERN_SIZE-1-k) ? next[j] : SEARCH_PATTERN_SIZE-1-k;
			j = g[j];
		}
		j--;
	}
	s = j;
	for (j = 0; j < SEARCH_PATTERN_SIZE; j++) {
		next[j] = (next[j] <= s+SEARCH_PATTERN_SIZE-j) ? next[j] : s+SEARCH_PATTERN_SIZE-j;
		if (j >= s) s = g[s];
	}
	free(g);
	return true;
}

int MatroskaSearch::Match(unsigned int start)
{
	int i, j;

	i = SEARCH_PATTERN_SIZE - 1 + start;
	while (i < SEARCH_SOURCE_SIZE) {
		j = SEARCH_PATTERN_SIZE - 1;
		while (j >= 0 && source[i] == pattern[j]) {
			i--;
			j--;
		}
		if (j < 0) return i + 1;
		if (skip[source[i] & 0x00ff] >= next[j])
			i += skip[source[i] & 0x00ff];
		else i += next[j];
	}
	return -1;
};


}   // namespace mkvreader

