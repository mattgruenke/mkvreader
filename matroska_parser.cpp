/*
 *  Part of the foobar2000 Matroska plugin
 *
 *  Copyright (C) Jory 'jcsston' Stone (jcsston at toughguy net) - 2003-2004
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

#include "matroska_parser.h"

#include <cmath>
#include <string>
#include <algorithm>

#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/case_conv.hpp>

using std::string;

using namespace LIBEBML_NAMESPACE;	
using namespace LIBMATROSKA_NAMESPACE;

const size_t TextBuffer_Size = 16;

static const char * rg_fields[]=
{
	"REPLAYGAIN_GAIN",
	"REPLAYGAIN_PEAK"
};

static const char * rg_track_fields[]=
{
	"REPLAYGAIN_TRACK_GAIN",
	"REPLAYGAIN_TRACK_PEAK"
};

static const char * rg_album_fields[]=
{
	"REPLAYGAIN_ALBUM_GAIN",
	"REPLAYGAIN_ALBUM_PEAK"
};

#define MATROSKA_TAG_IDX 0
#define FOOBAR2K_TAG_IDX 1

static const char * chapter_tag_mapping[][2] =
{
	{ "PART_NUMBER", "TRACKNUMBER" },
	{ "DATE_RELEASED", "DATE" },
    //{ "LEAD_PERFORMER", "PERFORMER" },
    //{ "ACCOMPANIMENT", "BAND" },
    //{ "LYRICIST", "WRITER" }
};

static const char * edition_tag_mapping[][2] =
{
	{ "TITLE", "ALBUM" },
	{ "CATALOG_NUMBER", "CATALOG" },
    { "TOTAL_PARTS", "TOTALTRACKS" },
    //{ "PART_NUMBER", "DISCNUMBER" },
    //{ "TOTAL_DISCS", "TOTALDISCS" }
};

static const char * hidden_edition_field[] =
{
	"___DUMMY___",
};

static const char * hidden_chapter_field[] =
{
	"CDAUDIO_TRACK_FLAGS",
};

bool starts_with(const string &s, const char *start) {
  return strncmp(s.c_str(), start, strlen(start)) == 0;
};

static bool IsSeekable(const IOCallback &cb)
{
    return true;    // since we're using StdIOCallback, it's always seekable.
}

uint64 MatroskaAudioParser::SecondsToTimecode(double seconds)
{
	return (uint64)floor(seconds * 1000000000);
};

double MatroskaAudioParser::TimecodeToSeconds(uint64 code,unsigned samplerate_hint)
{
	return ((double)(int64)code / 1000000000);
};


MatroskaAudioFrame::MatroskaAudioFrame() 
{
	timecode = 0;
	duration = 0;
    add_id = 0;
};

MatroskaAttachment::MatroskaAttachment()
{
	FileName = L"";
	MimeType = "";
	Description = L"";
	SourceFilename = L"";
	SourceStartPos = 0;
	SourceDataLength = 0;
};

void MatroskaAudioFrame::Reset()
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
	for (int i = tags.size()-1; i >= 0; i--)
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
	for (int i = tags.size()-1; i >= 0; i--)
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

MatroskaAudioParser::MatroskaAudioParser(const char *filename, abort_callback & p_abort) 
	:
		m_filename(filename),
		m_IOCallback(new StdIOCallback(filename, MODE_READ)), // TO_DO: revisit mode
		m_InputStream(*m_IOCallback)
{
	m_TimecodeScale = TIMECODE_SCALE;
	m_FileDate = 0;
	m_Duration = 0;
	m_CurrentTimecode = 0;
	//m_ElementLevel0 = NULL;
	//UpperElementLevel = 0;
	m_CurrentEdition = 0;
	m_CurrentChapter = 0;
	m_FileSize = boost::filesystem::file_size(filename); // TO_DO: throws
	m_TagPos = 0;
	m_TagSize = 0;
	m_TagScanRange = 1024 * 64;
	m_CurrentTrackNo = 0;
};

MatroskaAudioParser::~MatroskaAudioParser() {
	//if (m_ElementLevel0 != NULL)
	//	_DELETE(m_ElementLevel0);
		//delete m_ElementLevel0;
	
	while (!m_Queue.empty()) {
		MatroskaAudioFrame *currentPacket = m_Queue.front();
		//delete currentPacket;
		_DELETE(currentPacket);
		m_Queue.pop();
	}
};

int MatroskaAudioParser::Parse(bool bInfoOnly, bool bBreakAtClusters) 
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
			return 1;
		}
		//We must have found the EBML head :)
		m_ElementLevel0->SkipData(m_InputStream, m_ElementLevel0->Generic().Context);
		//delete m_ElementLevel0;
		//_DELETE(m_ElementLevel0);

		// Next element must be a segment
		m_ElementLevel0 = ElementPtr(m_InputStream.FindNextID(KaxSegment::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (m_ElementLevel0 == NullElement) {
			//No segment/level 0 element found.
			return 1;
		}
		if (!(EbmlId(*m_ElementLevel0) == KaxSegment::ClassInfos.GlobalId)) {
			//delete m_ElementLevel0;
			//m_ElementLevel0 = NULL;
			//_DELETE(m_ElementLevel0);
			return 1;
		}

		UpperElementLevel = 0;
		// We've got our segment, so let's find the tracks
		ElementLevel1 = ElementPtr(m_InputStream.FindNextElement(m_ElementLevel0->Generic().Context, UpperElementLevel, 0xFFFFFFFFFFFFFFFFL, true, 1));
		while (ElementLevel1 != NullElement) {
			if (UpperElementLevel > 0) {
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
						m_Duration = double(duration) * m_TimecodeScale;

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

				unsigned int Index0;
				for (Index0 = 0; Index0 < Tracks->ListSize(); Index0++) {
					if ((*Tracks)[Index0]->Generic().GlobalId == KaxTrackEntry::ClassInfos.GlobalId) {
						KaxTrackEntry &TrackEntry = *static_cast<KaxTrackEntry *>((*Tracks)[Index0]);
						// Create a new MatroskaTrack
						MatroskaTrackInfo newTrack;
						
						unsigned int Index1;
						for (Index1 = 0; Index1 < TrackEntry.ListSize(); Index1++) {
							if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackNumber::ClassInfos.GlobalId) {
								KaxTrackNumber &TrackNumber = *static_cast<KaxTrackNumber*>(TrackEntry[Index1]);
								newTrack.trackNumber = TrackNumber;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackUID::ClassInfos.GlobalId) {
								KaxTrackUID &TrackUID = *static_cast<KaxTrackUID*>(TrackEntry[Index1]);
								newTrack.trackUID = TrackUID;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackType::ClassInfos.GlobalId) {
								KaxTrackType &TrackType = *static_cast<KaxTrackType*>(TrackEntry[Index1]);
								if (uint8(TrackType) != track_audio) {
									newTrack.trackNumber = 0xFFFF;
									break;
								}

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackTimecodeScale::ClassInfos.GlobalId) {
								KaxTrackTimecodeScale &TrackTimecodeScale = *static_cast<KaxTrackTimecodeScale*>(TrackEntry[Index1]);
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
								KaxTrackFlagDefault &TrackFlagDefault = *static_cast<KaxTrackFlagDefault*>(TrackEntry[Index1]);
								//newTrack->FlagDefault = TrackFlagDefault;
							/* Matroska2
							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackFlagEnabled::ClassInfos.GlobalId) {
								KaxTrackFlagEnabled &TrackFlagEnabled = *static_cast<KaxTrackFlagEnabled*>(TrackEntry[Index1]);
								//newTrack->FlagEnabled = TrackFlagEnabled;
							*/
							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackFlagLacing::ClassInfos.GlobalId) {
								KaxTrackFlagLacing &TrackFlagLacing = *static_cast<KaxTrackFlagLacing*>(TrackEntry[Index1]);
								//newTrack->FlagLacing = TrackFlagLacing;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackLanguage::ClassInfos.GlobalId) {
								KaxTrackLanguage &TrackLanguage = *static_cast<KaxTrackLanguage*>(TrackEntry[Index1]);
								newTrack.language = std::string(TrackLanguage);

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackMaxCache::ClassInfos.GlobalId) {
								KaxTrackMaxCache &TrackMaxCache = *static_cast<KaxTrackMaxCache*>(TrackEntry[Index1]);
								//newTrack->MaxCache = TrackMaxCache;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackMinCache::ClassInfos.GlobalId) {
								KaxTrackMinCache &TrackMinCache = *static_cast<KaxTrackMinCache*>(TrackEntry[Index1]);
								//newTrack->MinCache = TrackMinCache;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackName::ClassInfos.GlobalId) {
								KaxTrackName &TrackName = *static_cast<KaxTrackName*>(TrackEntry[Index1]);
								newTrack.name = TrackName;

							} else if (TrackEntry[Index1]->Generic().GlobalId == KaxTrackAudio::ClassInfos.GlobalId) {
								KaxTrackAudio &TrackAudio = *static_cast<KaxTrackAudio*>(TrackEntry[Index1]);

								unsigned int Index2;
								for (Index2 = 0; Index2 < TrackAudio.ListSize(); Index2++) {
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
	} catch (std::exception &) {
		return 1;

	} catch (...) {
		return 1;
	}

	CountClusters();
	return 0;
};

//int MatroskaAudioParser::WriteTags(const file_info & info)
int MatroskaAudioParser::WriteTags()
{
	KaxTags & MyKaxTags = GetChild<KaxTags>(*static_cast<EbmlMaster *>(m_ElementLevel0.get()));

	// On to writing :)
	if (m_TagPos == 0) {
		// Ok, we need to append the tags onto the end of the file, no existing tag element
		m_TagPos = m_FileSize;
	} else {
		
	}	

	//Start going through the list and adding tags
	KaxTag *MyKaxTag_last = NULL;
	for(size_t current_tag_track = 0; current_tag_track < m_Tags.size(); current_tag_track++) {
		KaxTag *MyKaxTag = NULL;
		if (MyKaxTag_last != NULL)
		{
			MyKaxTag = &GetNextChild<KaxTag>(MyKaxTags, *MyKaxTag_last);
			MyKaxTag_last = MyKaxTag;
		}else {
			MyKaxTag = &GetChild<KaxTag>(MyKaxTags);
			MyKaxTag_last = MyKaxTag;
		}
		MatroskaTagInfo &currentTag = m_Tags.at(current_tag_track);
		//The Targets group
		KaxTagTargets & MyKaxTagTargets = GetChild<KaxTagTargets>(*MyKaxTag);
		if (currentTag.targetTrackUID != 0) {
			KaxTagTrackUID & MyKaxTagTrackUID = GetChild<KaxTagTrackUID>(MyKaxTagTargets);
			*static_cast<EbmlUInteger *>(&MyKaxTagTrackUID) = currentTag.targetTrackUID;
		}
		if (currentTag.targetEditionUID != 0) {
			KaxTagEditionUID & MyKaxTagEditionUID = GetChild<KaxTagEditionUID>(MyKaxTagTargets);
			*static_cast<EbmlUInteger *>(&MyKaxTagEditionUID) = currentTag.targetEditionUID;
		}
		if (currentTag.targetChapterUID != 0) {
			KaxTagChapterUID & MyKaxTagChapterUID = GetChild<KaxTagChapterUID>(MyKaxTagTargets);
			*static_cast<EbmlUInteger *>(&MyKaxTagChapterUID) = currentTag.targetChapterUID;
		}
		if (currentTag.targetAttachmentUID != 0) {
			KaxTagAttachmentUID & MyKaxTagAttachmentUID = GetChild<KaxTagAttachmentUID>(MyKaxTagTargets);
			*static_cast<EbmlUInteger *>(&MyKaxTagAttachmentUID) = currentTag.targetAttachmentUID;
		}
		if(currentTag.targetTypeValue != 0) {
			KaxTagTargetTypeValue& MyKaxTagTargetTypeValue = GetChild<KaxTagTargetTypeValue>(MyKaxTagTargets);
			*static_cast<EbmlUInteger *>(&MyKaxTagTargetTypeValue) = currentTag.targetTypeValue;
			if(!currentTag.targetType.empty())
			{
				KaxTagTargetType& MyKaxTagTargetType = GetChild<KaxTagTargetType>(MyKaxTagTargets);
				*static_cast<EbmlString *>(&MyKaxTagTargetType) = currentTag.targetType;				
			}
		}
		
		// Add the millions of simple tags we can have ;)
		KaxTagSimple *MySimpleTag_last = NULL;
		for(size_t st = 0; st < currentTag.tags.size(); st++) {
			KaxTagSimple *MySimpleTag = NULL;
			if (MySimpleTag_last != NULL)
			{
				MySimpleTag = &GetNextChild<KaxTagSimple>(*MyKaxTag, *MySimpleTag_last);
				MySimpleTag_last = MySimpleTag;
			}else {
				MySimpleTag = &GetChild<KaxTagSimple>(*MyKaxTag);
				MySimpleTag_last = MySimpleTag;
			}
			MatroskaSimpleTag &currentSimpleTag = currentTag.tags.at(st);

			KaxTagName & MyKaxTagName = GetChild<KaxTagName>(*MySimpleTag);
			*static_cast<EbmlUnicodeString *>(&MyKaxTagName) = currentSimpleTag.name;

			KaxTagString & MyKaxTagString = GetChild<KaxTagString>(*MySimpleTag);
			*static_cast<EbmlUnicodeString *>(&MyKaxTagString) = currentSimpleTag.value;
			
			KaxTagLangue& MyKaxTagLangue = GetChild<KaxTagLangue>(*MySimpleTag);
			*static_cast<EbmlString *>(&MyKaxTagLangue) = currentSimpleTag.language;

			KaxTagDefault& MyKaxTagDefault = GetChild<KaxTagDefault>(*MySimpleTag);
			*static_cast<EbmlUInteger *>(&MyKaxTagDefault) = currentSimpleTag.defaultFlag;			
		}

		m_IOCallback->setFilePointer(m_TagPos);
		// Now we write the tags to the file
		EbmlVoid & Dummy = GetChild<EbmlVoid>(*static_cast<EbmlMaster *>(m_ElementLevel0.get()));
		uint64 size_of_tags = m_TagSize;
		MyKaxTags.UpdateSize(true);
		if (size_of_tags < MyKaxTags.GetSize())
			size_of_tags = MyKaxTags.GetSize();

		Dummy.SetSize(size_of_tags+8); // Size of the previous tag element
		uint64 pos = m_IOCallback->getFilePointer();
		if (pos == 0)
			//We don't want to overwrite the EBML header :P
			return 2;
		Dummy.Render(*m_IOCallback);
		m_IOCallback->setFilePointer(Dummy.GetElementPosition());
		pos = m_IOCallback->getFilePointer();
		if (pos == 0)
			//We don't want to overwrite the EBML header :P
			return 2;

		MyKaxTags.Render(*m_IOCallback);
		
		//l0->UpdateSize(false, true);
		std::auto_ptr<KaxSegment> new_segment(new KaxSegment);
		KaxSegment * segment = (KaxSegment *)m_ElementLevel0.get();

		m_IOCallback->setFilePointer(segment->GetElementPosition());
		new_segment->WriteHead(*m_IOCallback, segment->HeadSize() - 4);
		m_IOCallback->setFilePointer(0, seek_end);
		int ret = new_segment->ForceSize(m_IOCallback->getFilePointer() - segment->HeadSize() - segment->GetElementPosition());
		if (!ret) { 
			segment->OverwriteHead(*m_IOCallback);
		      /*("Wrote the element at the end of the file but could "
                      "not update the segment size. Therefore the element "
                      "will not be visible. Aborting the process. The file "
                      "has been changed!")*/

			// Truncate the file:
			size_t newsize = segment->HeadSize() - segment->GetElementPosition();
			m_IOCallback->close();
			boost::filesystem::resize_file(m_filename, newsize);

			return 1;
		} else {
			new_segment->OverwriteHead(*m_IOCallback);

		}

	}
	return 0;
};

int meta_get_num(const file_info &info, const char* name, int idx)
{
//	assert(pfc::is_valid_utf8(name));
	int n, m = std::min(info.meta_get_count(), (size_t) idx);
	int rv = 0;
	for(n=0; n<m; n++)
	{
		if (!strcasecmp(name, info.meta_enum_name(n)))
			rv++;
	}
	return rv;
}

bool isEditionTag(const char * name) {
	if (starts_with(name, "ALBUM") ||
		starts_with(name, "SUBALBUM") ||
		starts_with(name, "DISCID") ||
		starts_with(name, "CATALOG"))
	{
		return true;
	} else {
		return false;
	}
}


    // drop-in replacement for the PFC version.
static std::string float_to_string(size_t out_max, double val, unsigned precision)
{
    std::string fmt = boost::str( boost::format( "%%.%df" ) % precision );
    std::string result = boost::str( boost::format( fmt ) % val );
    if (result.size() > out_max) result.resize( out_max );
    return result;
}


void MatroskaAudioParser::SetTags(const file_info &info)
{
	int i, idx;
	const char *name;	
	std::string value;

	if (m_Chapters.size() == 0)
	{		
		// No chapters, works on track
		MatroskaTagInfo *trackTag;
		trackTag = FindTagWithTrackUID(m_Tracks.at(m_CurrentTrackNo).trackUID);
		if (trackTag == NULL) {
			// The tag doesn't exist yet
			MatroskaTagInfo tempTag;
			tempTag.targetTrackUID = m_Tracks.at(m_CurrentTrackNo).trackUID;
			m_Tags.push_back(tempTag);
			trackTag = &m_Tags.at(m_Tags.size()-1);
		}
		//if(trackTag->targetTypeValue == 0)
		//	trackTag->targetTypeValue = 50;
        trackTag->targetTypeValue = 30;
		trackTag->MarkAllAsRemovalPending();
		int metaDataCount = info.meta_get_count();
		for (i = 0; i < metaDataCount; i++)
		{
			for (int j = 0; j != info.meta_enum_value_count(i); ++j) {
				name = info.meta_enum_name(i);
				value = info.meta_enum_value(i, j);
				//idx = meta_get_num(info, name, i);
				idx = j;
//				name = foobar2k_to_matroska_chapter_tag(name);
				if ((name != NULL) && !value.empty()) {
					trackTag->SetTagValue(name, value.c_str(), idx);
				}
			}
		}
		// Add the replay_gain tags
		replaygain_info rg = info.get_replaygain();
		if (rg.getTrackGain()) {
			value = float_to_string(TextBuffer_Size, *rg.getTrackGain(), 7);
		} else {
			value.clear();
		}
		if (!value.empty())
			trackTag->SetTagValue("REPLAYGAIN_GAIN", value.c_str());
		if (rg.getTrackPeak()) {
			value = float_to_string(TextBuffer_Size, *rg.getTrackPeak(), 7);
		} else {
			value.clear();
		}
		if (!value.empty())
			trackTag->SetTagValue("REPLAYGAIN_PEAK", value.c_str());
		trackTag->RemoveMarkedTags();
	}
	
	// Set global track tags as album tags
	if ((m_Chapters.size() > 0) && (m_CurrentChapter != NULL))
	{
		MatroskaTagInfo *trackTag;
		trackTag = FindTagWithTrackUID(m_Tracks.at(m_CurrentTrackNo).trackUID);
		if (trackTag == NULL)
		{
			MatroskaTagInfo tempTag;
			tempTag.targetTrackUID = m_Tracks.at(m_CurrentTrackNo).trackUID;			
			m_Tags.push_back(tempTag);
			trackTag = &m_Tags.at(m_Tags.size()-1);
		}
		if(trackTag->targetTypeValue == 0)
			trackTag->targetTypeValue = 50;
		trackTag->MarkAllAsRemovalPending();
		int metaDataCount = info.meta_get_count();
		for (i = 0; i < metaDataCount; i++)
		{
			for (int j = 0; j != info.meta_enum_value_count(i); ++j) {
				name = info.meta_enum_name(i);
				value = info.meta_enum_value(i, j);
				//idx = meta_get_num(info, name, i);
				idx = j;
/*
				if (isEditionTag(name))
				{
					name = foobar2k_to_matroska_edition_tag(name);
				} else {
					name = NULL;
				}
*/
				if ((name != NULL) && !value.empty()) {
					trackTag->SetTagValue(name, value.c_str(), idx);
				}
			}
		}
		replaygain_info rg = info.get_replaygain();
		if (rg.getAlbumGain()) {
			value = float_to_string(TextBuffer_Size, *rg.getAlbumGain(), 7);
		} else {
			value.clear();
		}
		if (!value.empty())
			trackTag->SetTagValue("REPLAYGAIN_GAIN", value.c_str());
		if (rg.getAlbumPeak()) {
			value = float_to_string(TextBuffer_Size, *rg.getAlbumPeak(), 7);
		} else {
			value.clear();
		}
		if (!value.empty())
			trackTag->SetTagValue("REPLAYGAIN_PEAK", value.c_str());
		trackTag->RemoveMarkedTags();
	}


	//If there's not only a track, set all the chapter tags
	if ((m_Chapters.size() > 0) && (m_CurrentChapter != NULL))
	{
		// Ok we add the tags to tag with chapter+track UID's
		MatroskaTagInfo *chapterTag;		
		chapterTag = FindTagWithChapterUID(m_CurrentChapter->chapterUID,
			m_Tracks.at(m_CurrentTrackNo).trackUID);
		if (chapterTag == NULL)
		{
			MatroskaTagInfo tempTag;
			tempTag.targetTrackUID = m_Tracks.at(m_CurrentTrackNo).trackUID;
			tempTag.targetChapterUID = m_CurrentChapter->chapterUID;
			m_Tags.push_back(tempTag);
			chapterTag = &m_Tags.at(m_Tags.size()-1);
		}
		if(chapterTag->targetTypeValue == 0)
			chapterTag->targetTypeValue = 30;
		chapterTag->MarkAllAsRemovalPending();
		int metaDataCount = info.meta_get_count();
		for (i = 0; i < metaDataCount; i++)
		{
			for (int j = 0; j != info.meta_enum_value_count(i); ++j) {
				name = info.meta_enum_name(i);
				value = info.meta_enum_value(i, j);
				//idx = meta_get_num(info, name, i);
				idx = j;
/*
				if (isEditionTag(name))
				{
					name = NULL;
				} else {
					name = foobar2k_to_matroska_chapter_tag(name);
				}
*/
				if ((name != NULL) && !value.empty()) {
					chapterTag->SetTagValue(name, value.c_str(), idx);
				}
			}
		}
		replaygain_info rg = info.get_replaygain();
		if (rg.getTrackGain()) {
			value = float_to_string(TextBuffer_Size, *rg.getTrackGain(), 7);
		} else {
			value.clear();
		}
		if (!value.empty())
			chapterTag->SetTagValue("REPLAYGAIN_GAIN", value.c_str());
		if (rg.getTrackPeak()) {
			value = float_to_string(TextBuffer_Size, *rg.getTrackPeak(), 7);
		} else {
			value.clear();
		}
		if (!value.empty())
			chapterTag->SetTagValue("REPLAYGAIN_PEAK", value.c_str());
		chapterTag->RemoveMarkedTags();
	}
};

MatroskaTagInfo *MatroskaAudioParser::FindTagWithTrackUID(uint64 trackUID) 
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

MatroskaTagInfo *MatroskaAudioParser::FindTagWithEditionUID(uint64 editionUID, uint64 trackUID)
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

MatroskaTagInfo *MatroskaAudioParser::FindTagWithChapterUID(uint64 chapterUID, uint64 trackUID)
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

double MatroskaAudioParser::GetDuration() { 
	if (m_CurrentChapter != NULL) {
		return TimecodeToSeconds(m_CurrentChapter->timeEnd - m_CurrentChapter->timeStart);
	};
	return m_Duration / 1000000000.0;
	if (m_Tracks.size() != 0) {
		return m_Tracks.at(m_CurrentTrackNo).defaultDuration * (int64)m_TimecodeScale;
	}
};

int32 MatroskaAudioParser::GetFirstAudioTrack()
{
	for (uint16 t = 0; t < m_Tracks.size(); t++)
	{
		MatroskaTrackInfo &currentTrack = m_Tracks.at(t);
		if (!strncmp(currentTrack.codecID.c_str(),"A_",2)) return t;
	}
	return -1;
}

uint32 MatroskaAudioParser::GetAudioTrackCount()
{
	uint32 count = 0;
	for (uint16 t = 0; t < m_Tracks.size(); t++)
	{
		MatroskaTrackInfo &currentTrack = m_Tracks.at(t);
		if (!strncmp(currentTrack.codecID.c_str(),"A_",2))
			count++;
	}
	return count;
}

uint32 MatroskaAudioParser::GetAudioTrackIndex(uint32 index)
{
	uint32 idx = 0;
	for (uint16 t = 0; t < m_Tracks.size(); t++)
	{
		MatroskaTrackInfo &currentTrack = m_Tracks.at(t);
		if (!strncmp(currentTrack.codecID.c_str(),"A_",2))
		{
			if(t == index)
				break;
			idx++;
		}
	}
	return idx;
}

#define tabsize(tab) (sizeof(tab)/sizeof(*tab))

static bool is_rg_field(const char * name)
{
	int m;
	for (m = 0; m < tabsize(rg_fields); m++)
	{
		if (!strcasecmp(name,rg_fields[m])) return true;
	}
	return false;
}

static bool is_hidden_edition_field(const char * name)
{
	for (int i = 0; i < tabsize(hidden_edition_field); i++)
		if (!strcasecmp(name, hidden_edition_field[i]))
			return true;
	return false;
}

static bool is_hidden_chapter_field(const char * name)
{
	for (int i = 0; i < tabsize(hidden_chapter_field); i++)
		if (!strcasecmp(name, hidden_chapter_field[i]))
			return true;
	return false;
}

static bool IsTagNamed(const MatroskaSimpleTag &currentSimpleTag, const char * name)
{
	return !strcasecmp(currentSimpleTag.name.GetUTF8().c_str(), name);
}

static bool IsTagValued(const MatroskaSimpleTag &currentSimpleTag, const char * value)
{
	return !strcmp(currentSimpleTag.value.GetUTF8().c_str(), value);
}

static bool AreTagsNameEqual(const MatroskaSimpleTag &tag1, const MatroskaSimpleTag &tag2)
{
	return !strcasecmp(tag1.name.GetUTF8().c_str(), tag2.name.GetUTF8().c_str());
}

static bool AreTagsValueEqual(const MatroskaSimpleTag &tag1, const MatroskaSimpleTag &tag2)
{
	return !strcmp(tag1.value.GetUTF8().c_str(), tag2.value.GetUTF8().c_str());
}

static bool AreTagsEqual(const MatroskaSimpleTag &tag1, const MatroskaSimpleTag &tag2)
{
	return AreTagsNameEqual(tag1,tag2) && AreTagsValueEqual(tag1,tag2);
}

const MatroskaSimpleTag* GetTagWithName(MatroskaTagInfo *SimpleTags, const char * name)
{
	if(SimpleTags == NULL)
		return NULL;

	for (size_t s = 0; s < SimpleTags->tags.size(); s++)
	{
		const MatroskaSimpleTag &currentSimpleTag = SimpleTags->tags.at(s);		
		if(IsTagNamed(currentSimpleTag, name))
			return &currentSimpleTag;
	}
	return NULL;
}

bool TagExistsAtEditionLevel(MatroskaTagInfo *TrackTags, const char * name)
{
	if(TrackTags == NULL)
		return false;

	for (size_t s = 0; s < TrackTags->tags.size(); s++)
	{
		const MatroskaSimpleTag &currentSimpleTag = TrackTags->tags.at(s);		
		if(IsTagNamed(currentSimpleTag,name))
			return true;
	}
	return false;
}

bool TagExistsAtChapterLevel(MatroskaTagInfo *ChapterTags, const char * name)
{
	if(ChapterTags == NULL)
		return false;

	for (size_t s = 0; s < ChapterTags->tags.size(); s++)
	{
		const MatroskaSimpleTag &currentSimpleTag = ChapterTags->tags.at(s);		
		if(IsTagNamed(currentSimpleTag,name))
			return true;
	}
	return false;
}

bool MatroskaAudioParser::AreTagsIdenticalAtAllLevels(const char * name)
{
	const MatroskaSimpleTag* ReferenceTag = NULL;
	bool AtLeastOneChapter = false;

	for (size_t i = 0; i < m_Tags.size(); i++)
	{
		MatroskaTagInfo &currentTags = m_Tags.at(i);
		if(ReferenceTag == NULL)
		{
			AtLeastOneChapter = true;
			ReferenceTag = GetTagWithName(&currentTags, name);
		} else {
			const MatroskaSimpleTag* TagToCheck = GetTagWithName(&currentTags, name);
			if(TagToCheck && !AreTagsValueEqual(*ReferenceTag,*TagToCheck))
				return false;				
		}
	}
	return AtLeastOneChapter;
}

bool MatroskaAudioParser::AreTagsIdenticalAtEditionLevel(const char * name)
{
	const MatroskaSimpleTag* ReferenceTag = NULL;
	bool AtLeastOneChapter = false;

	for (size_t i = 0; i < m_Tags.size(); i++)
	{
		MatroskaTagInfo &currentTags = m_Tags.at(i);
		if (currentTags.targetChapterUID == 0)
		{
			// Chapter tags
			if(ReferenceTag == NULL)
			{
				AtLeastOneChapter = true;
				ReferenceTag = GetTagWithName(&currentTags, name);
			} else {
				const MatroskaSimpleTag* TagToCheck = GetTagWithName(&currentTags, name);
				if(TagToCheck && !AreTagsValueEqual(*ReferenceTag,*TagToCheck))
					return false;				
			}
		}
	}
	return AtLeastOneChapter;
}

bool MatroskaAudioParser::AreTagsIdenticalAtChapterLevel(const char * name)
{
	const MatroskaSimpleTag* ReferenceTag = NULL;
	bool AtLeastOneChapter = false;

	for (size_t i = 0; i < m_Tags.size(); i++)
	{
		MatroskaTagInfo &currentTags = m_Tags.at(i);
		if (currentTags.targetChapterUID != 0)
		{
			// Chapter tags
			if(ReferenceTag == NULL)
			{
				AtLeastOneChapter = true;
				ReferenceTag = GetTagWithName(&currentTags, name);
			} else {
				const MatroskaSimpleTag* TagToCheck = GetTagWithName(&currentTags, name);
				if(TagToCheck && !AreTagsValueEqual(*ReferenceTag,*TagToCheck))
					return false;				
			}
		}
	}
	return AtLeastOneChapter;
}

void MatroskaAudioParser::SetAlbumTags(file_info & info,
									   MatroskaTagInfo* AlbumTags,
									   MatroskaTagInfo* TrackTags)
{
	if (AlbumTags == NULL)
		return;

	for (size_t s = 0; s < AlbumTags->tags.size(); s++)
	{
		MatroskaSimpleTag &simpleTag = AlbumTags->tags.at(s);
		
		if (is_rg_field(simpleTag.name.GetUTF8().c_str()))
		{
			if(IsTagNamed(simpleTag, "REPLAYGAIN_GAIN"))
			{
				info.info_set_replaygain("replaygain_album_gain", simpleTag.value.GetUTF8().c_str());
				if (TrackTags == NULL) info.info_set_replaygain("replaygain_track_gain", simpleTag.value.GetUTF8().c_str());
			} else if(IsTagNamed(simpleTag, "REPLAYGAIN_PEAK")) {
				info.info_set_replaygain("replaygain_album_peak", simpleTag.value.GetUTF8().c_str());
				if (TrackTags == NULL) info.info_set_replaygain("replaygain_track_peak", simpleTag.value.GetUTF8().c_str());
			}
		}
		else if (is_hidden_edition_field(simpleTag.name.GetUTF8().c_str()))
		{
			// Ignored tag, will be rewrited later
			simpleTag.hidden = true;
		}
		else if(IsTagNamed(simpleTag,"TITLE"))
		{
			// Special case for Edition/TITLE
			if(TagExistsAtChapterLevel(TrackTags, "ALBUM"))
			{
				info.meta_add("ALBUM TITLE", simpleTag.value.GetUTF8().c_str());
			} else {
				info.meta_add("ALBUM", simpleTag.value.GetUTF8().c_str());
			}
		}
        /*
		else if(IsTagNamed(simpleTag,"SUBTITLE"))
		{
			// Special case for Edition/SUBTITLE
			if(TagExistsAtChapterLevel(TrackTags, "SUBALBUM"))
			{
				info.meta_add("ALBUM SUBTITLE", simpleTag.value.GetUTF8().c_str());
			} else {
				info.meta_add("SUBALBUM", simpleTag.value.GetUTF8().c_str());
			}
		}
        */
        else if (IsTagNamed(simpleTag, "DISCID") ||
		         IsTagNamed(simpleTag, "CATALOG_NUMBER") ||
                 IsTagNamed(simpleTag, "TOTAL_PARTS") ||
                 IsTagNamed(simpleTag, "PART_NUMBER") ||
                 IsTagNamed(simpleTag, "TOTAL_DISCS"))
		{
//			const char *name = matroska_to_foobar2k_edition_tag(simpleTag.name.GetUTF8().c_str());
			const char *name = simpleTag.name.GetUTF8().c_str();
			info.meta_add(name, simpleTag.value.GetUTF8().c_str());
		}
        /*else if (IsTagNamed(simpleTag,"COMMENTS"))
		{
			info.meta_add("ALBUM COMMENT", simpleTag.value.GetUTF8().c_str());
		}*/
		else if((!AreTagsIdenticalAtAllLevels(simpleTag.name.GetUTF8().c_str()))
			|| (!TagExistsAtChapterLevel(TrackTags, simpleTag.name.GetUTF8().c_str())))
		{
			// Prefix tag with "ALBUM "
			char newTagName[255] = "ALBUM ";
//			const char *name = matroska_to_foobar2k_edition_tag(simpleTag.name.GetUTF8().c_str());
			const char *name = simpleTag.name.GetUTF8().c_str();
			strncat(newTagName, name, 255);
            std::string new_tagname;
//            convert_matroska_to_foobar2k_tag(new_tagname, newTagName);
            new_tagname = newTagName;
			info.meta_add(new_tagname.c_str(), simpleTag.value.GetUTF8().c_str());
		}
		else 
		{
			// Ignored tag, will be rewrited later
			simpleTag.hidden = true;
		}
	}
}

void MatroskaAudioParser::SetTrackTags(file_info &info, MatroskaTagInfo* TrackTags)
{
	if(TrackTags == NULL)
		return;

	for (size_t s = 0; s < TrackTags->tags.size(); s++)
	{
		MatroskaSimpleTag &simpleTag = TrackTags->tags.at(s);
		
		if (is_rg_field(simpleTag.name.GetUTF8().c_str()))
		{
			if(IsTagNamed(simpleTag, "REPLAYGAIN_GAIN"))
			{
				info.info_set_replaygain("replaygain_track_gain", simpleTag.value.GetUTF8().c_str());
			} else if(IsTagNamed(simpleTag, "REPLAYGAIN_PEAK")) {
				info.info_set_replaygain("replaygain_track_peak", simpleTag.value.GetUTF8().c_str());
			}
		}
		else if (is_hidden_chapter_field(simpleTag.name.GetUTF8().c_str()))
		{
			// Ignore tag
			simpleTag.hidden = true;
		}
        /*else if(IsTagNamed(simpleTag,"COMMENTS"))
		{
			info.meta_add("COMMENT", simpleTag.value.GetUTF8().c_str());
		}*/
		else if(IsTagNamed(simpleTag,"ALBUM"))
		{
			if(!TagExistsAtEditionLevel(TrackTags, "TITLE") && AreTagsIdenticalAtChapterLevel("ALBUM")) {
				info.meta_add("ALBUM", simpleTag.value.GetUTF8().c_str());
			} else {
				info.meta_add("ORIGINAL ALBUM", simpleTag.value.GetUTF8().c_str());
			}
		}
		else
		{
            std::string new_tagname;
//            convert_matroska_to_foobar2k_tag(new_tagname, matroska_to_foobar2k_chapter_tag(simpleTag.name.GetUTF8().c_str()));
            new_tagname = simpleTag.name.GetUTF8().c_str();
			info.meta_add(new_tagname.c_str(), simpleTag.value.GetUTF8().c_str());	
		}
	}
}


static uint64_t time_to_samples(double time, uint32_t sample_rate) {
	return (uint64_t) floor(sample_rate * time + 0.5);
}


static std::string cuesheet_format_index_time(double time) {
	uint64_t ticks = time_to_samples(time, 75);
	uint64_t seconds = ticks / 75;
	uint64_t minutes = seconds / 60;
	ticks %= 75;
	seconds %= 60;
	return boost::str( boost::format( "%02d:%02d:%02d" ) % minutes % seconds % ticks );
}


bool MatroskaAudioParser::SetFB2KInfo(file_info &info, uint32_t p_subsong)
{
	if (m_MuxingApp.length() > 0)
		info.info_set("MUXING_APP", m_MuxingApp.GetUTF8().c_str());
	if (m_WritingApp.length() > 0)
		info.info_set("WRITING_APP", m_WritingApp.GetUTF8().c_str());
	if (m_FileTitle.length() > 0)
		info.info_set("TITLE", m_FileTitle.GetUTF8().c_str());

	// pregap
	try {
		if (m_Chapters.at(p_subsong).subChapters.size() > 1) {
			double pregap = TimecodeToSeconds(m_Chapters.at(p_subsong).subChapters.at(1).timeStart - m_Chapters.at(p_subsong).subChapters.at(0).timeStart);
			info.info_set("pregap", cuesheet_format_index_time(pregap).c_str());
		}
	} catch (std::out_of_range & e) {
	}

	MatroskaTagInfo *TrackTags = FindTagWithTrackUID(m_Tracks.at(m_CurrentTrackNo).trackUID);
	MatroskaTagInfo *ChapterTags = NULL;
	if(m_CurrentChapter != NULL)
	{
		ChapterTags = FindTagWithChapterUID(m_CurrentChapter->chapterUID,
			m_Tracks.at(m_CurrentTrackNo).trackUID);
	}

	MatroskaTagInfo *EditionTags = NULL;
	if(m_CurrentEdition != NULL)
	{
		EditionTags = FindTagWithEditionUID(m_CurrentEdition->editionUID,
			m_Tracks.at(m_CurrentTrackNo).trackUID);
	}
	if(EditionTags != NULL && EditionTags->targetTypeValue == 50)
	{
		SetAlbumTags(info, EditionTags, ChapterTags);
	}

	if(TrackTags != NULL)
	{
		if(TrackTags->targetTypeValue == 50)
		{
			SetAlbumTags(info, TrackTags, ChapterTags);
			SetTrackTags(info, ChapterTags);
		} else if (TrackTags->targetTypeValue == 30) {
			SetTrackTags(info, TrackTags);
		}
	}
	
	// Last chance,
	if(m_CurrentChapter != NULL)
	{
		// If TITLE tag is empty we get it from the chapter name
		if ( (m_CurrentChapter->display.size() > 0) &&
			 ((info.meta_get("TITLE", 0) == NULL) ||
			  (strlen(info.meta_get("TITLE", 0)) == 0) ) )
		{
			info.meta_set("TITLE", m_CurrentChapter->display.at(0).string.GetUTF8().c_str());
		}
		if ((info.meta_get("TRACKNUMBER", 0) == NULL) || (strlen(info.meta_get("TRACKNUMBER", 0)) == 0))
		{
			//char trackNumberString[32];
            std::string trackNumberString;
#ifdef MULTITRACK			
			//wsprintf((LPWSTR)trackNumberString, (LPCWSTR)"%d",
			//	(p_subsong % m_Chapters.size()) +1);
            trackNumberString += ((p_subsong % m_Chapters.size()) +1);
#else
			wsprintf(trackNumberString, "%d",p_subsong+1);
#endif
			info.meta_set("TRACKNUMBER", trackNumberString.c_str());
		}
		if ((info.meta_get("ALBUM", 0) == NULL) || (strlen(info.meta_get("ALBUM", 0)) == 0))
		{
			if(m_FileTitle.length() > 0)
				info.meta_set("ALBUM", m_FileTitle.GetUTF8().c_str());
		}
	}

	return true;
};

void MatroskaAudioParser::MarkHiddenTags()
{
	// We call MatroskaAudioParser::SetFB2KInfo with a dummy file_info that
	// do nothing, so we will only mark hidden tags.
	// Hidden tag will be copied to keep track of them.
	// This is needed cause tag are read and written in 2 different instances
	file_info info;
	SetFB2KInfo(info, 0);
};

void MatroskaAudioParser::SetCurrentTrack(uint32 newTrackNo)
{
	m_CurrentTrackNo = newTrackNo;
	// Clear the current queue (we are changing tracks)
	while (!m_Queue.empty())
		m_Queue.pop();
};

void MatroskaAudioParser::SetSubSong(int subsong)
{
	// As we don't (yet?) use several Editions, select the first (default) one as the current one.
	m_CurrentEdition = NULL;
	m_CurrentChapter = NULL;
	if(m_Editions.size() > 0)
		m_CurrentEdition = &m_Editions.at(0);
	if (m_Chapters.size() > subsong)
		m_CurrentChapter = &m_Chapters.at(subsong);
	
};

int32 MatroskaAudioParser::GetAvgBitrate() 
{ 
	double ret = 0;
	ret = static_cast<double>(int64(m_FileSize)) / 1024;
	ret = ret / (m_Duration / 1000000000.0);
	ret = ret * 8;
	return static_cast<int32>(ret);
};

bool MatroskaAudioParser::skip_frames_until(double destination,unsigned & frames,double & last_timecode_delta,unsigned hint_samplerate)
{
	unsigned done = 0;
	unsigned last_laced = 0;

	double last_time;
	{
		uint64 last_timecode = get_current_frame_timecode();
		if (last_timecode == (uint64)(-1)) return false;
		last_time = TimecodeToSeconds(last_timecode,hint_samplerate);
	}

	for(;;)
	{
		while (!m_Queue.empty()) {
			MatroskaAudioFrame *currentPacket = m_Queue.front();
			double packet_time = TimecodeToSeconds(currentPacket->timecode,hint_samplerate);
			if (packet_time > destination)
			{
				if (done==0) return false;
				last_timecode_delta = destination - last_time;
				frames = done - last_laced;
				return true;
			}
			last_time = packet_time;
			done += (last_laced = currentPacket->dataBuffer.size());
			//delete currentPacket;
			_DELETE(currentPacket);
			m_Queue.pop();
		}
		if (FillQueue()!=0)
			return false;
	}
}


void MatroskaAudioParser::flush_queue()
{
	while (!m_Queue.empty()) {
		MatroskaAudioFrame *currentPacket = m_Queue.front();
		//delete currentPacket;
		_DELETE(currentPacket);
		m_Queue.pop();
	}
}

uint64 MatroskaAudioParser::get_current_frame_timecode()
{
	if (m_Queue.empty())
	{
		if (FillQueue()!=0) return -1;
		if (m_Queue.empty()) return -1;
	}
	return m_Queue.front()->timecode;
}

bool MatroskaAudioParser::Seek(double seconds,unsigned & frames_to_skip,double & time_to_skip,unsigned samplerate_hint)
{
	int64 ret = 0;

	if (m_CurrentChapter != NULL) {
		seconds += (double)(int64)m_CurrentChapter->timeStart / 1000000000; // ns -> seconds
	}

	uint64 seekToTimecode = SecondsToTimecode(seconds);
	
	flush_queue();
	m_CurrentTimecode = seekToTimecode;

	if (!skip_frames_until(seconds,frames_to_skip,time_to_skip,samplerate_hint)) return false;

	flush_queue();
	m_CurrentTimecode = seekToTimecode;
	if (FillQueue()!=0) return false;
	return true;

};

MatroskaAudioFrame * MatroskaAudioParser::ReadSingleFrame()
{
	for(;;)
	{
		if (!m_Queue.empty()) {
			MatroskaAudioFrame *newFrame = m_Queue.front();		
			m_Queue.pop();
			return newFrame;
		} else {
			if (FillQueue()!=0)
				return 0;
		}
	}
};

MatroskaAudioFrame * MatroskaAudioParser::ReadFirstFrame()
{
    m_CurrentTimecode = 0;
    return ReadSingleFrame();
};

typedef boost::shared_ptr<EbmlId> EbmlIdPtr;

void MatroskaAudioParser::Parse_MetaSeek(ElementPtr metaSeekElement, bool bInfoOnly) 
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

					} else if (*id == KaxSeekHead::ClassInfos.GlobalId) {
						NOTE1("Found MetaSeek Seek Entry Postion: %u", (unsigned long)lastSeekPos);
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

void MatroskaAudioParser::Parse_Chapter_Atom(KaxChapterAtom *ChapterAtom)
{
	Parse_Chapter_Atom(ChapterAtom, m_Chapters);
}

void MatroskaAudioParser::Parse_Chapter_Atom(KaxChapterAtom *ChapterAtom, std::vector<MatroskaChapterInfo> &p_chapters)
{
	int i, j;
	EbmlElement *Element = NULL;
	MatroskaChapterInfo newChapter;

	NOTE("New chapter");

	for (i = 0; i < ChapterAtom->ListSize(); i++)
	{	
		Element = (*ChapterAtom)[i];
				
		if (IS_ELEMENT_ID(KaxChapterUID))
		{
			newChapter.chapterUID = uint64(*static_cast<EbmlUInteger *>(Element));
			NOTE1("- UID : %I64d", newChapter.chapterUID);
		}
		else if(IS_ELEMENT_ID(KaxChapterTimeStart))
		{							
			newChapter.timeStart = uint64(*static_cast<EbmlUInteger *>(Element)); // it's in ns
			NOTE1("- TimeStart : %I64d", newChapter.timeStart);
		}
		else if(IS_ELEMENT_ID(KaxChapterTimeEnd))
		{
			newChapter.timeEnd = uint64(*static_cast<EbmlUInteger *>(Element)); // it's in ns
			NOTE1("- TimeEnd : %I64d", newChapter.timeEnd);
		}
		else if(IS_ELEMENT_ID(KaxChapterTrack))
		{
			KaxChapterTrack *ChapterTrack = (KaxChapterTrack *)Element;
			
			for (j = 0; j < ChapterTrack->ListSize(); j++)
			{
				Element = (*ChapterTrack)[j];
				if(IS_ELEMENT_ID(KaxChapterTrackNumber))
				{
					uint64 chapTrackNo = uint64(*static_cast<EbmlUInteger *>(Element));
					newChapter.tracks.push_back(chapTrackNo);
					NOTE1("- TrackNumber : %I64d", chapTrackNo);
				}
				else if(IS_ELEMENT_ID(KaxChapterAtom))
				{									
					// Ignore sub-chapter
					NOTE("ignore sub-chapter");
					//Parse_Chapter_Atom((KaxChapterAtom *)Element);
				}
			}
		}
		else if(IS_ELEMENT_ID(KaxChapterDisplay))
		{
			// A new chapter display string+lang+country
			MatroskaChapterDisplayInfo newChapterDisplay;							
			KaxChapterDisplay *ChapterDisplay = (KaxChapterDisplay *)Element;
			
			for (j = 0; j < ChapterDisplay->ListSize(); j++) {
				Element = (*ChapterDisplay)[j];
				if(IS_ELEMENT_ID(KaxChapterString))
				{
					newChapterDisplay.string = UTFstring(*static_cast <EbmlUnicodeString *>(Element)).c_str();
					NOTE1("- String : %s", newChapterDisplay.string.GetUTF8().c_str());
				}
				else if(IS_ELEMENT_ID(KaxChapterAtom))
				{									
					// Ignore sub-chapter
					NOTE("ignore sub-chapter");
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

void MatroskaAudioParser::Parse_Chapters(KaxChapters *chaptersElement)
{
	int i, j;	
	EbmlElement *Element = NULL;	
	int UpperEltFound = 0;

	NOTE("New edition");

	if (chaptersElement == NULL)
		return;

	chaptersElement->Read(m_InputStream, KaxChapters::ClassInfos.Context,
		UpperEltFound, Element, true);

	for (i = 0; i < chaptersElement->ListSize(); i++)
	{
		Element = (*chaptersElement)[i];
		if(IS_ELEMENT_ID(KaxEditionEntry))
		{
			MatroskaEditionInfo newEdition;
			KaxEditionEntry *edition = (KaxEditionEntry *)Element;
			for (j = 0; j < edition->ListSize(); j++)
			{
				Element = (*edition)[j];
				if(IS_ELEMENT_ID(KaxEditionUID))
				{
					// A new edition :)
					newEdition.editionUID = uint64(*static_cast<EbmlUInteger *>(Element));
					NOTE1("- UID : %I64d", newEdition.editionUID);
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

void MatroskaAudioParser::Parse_Tags(KaxTags *tagsElement)
{
	int i, j, k;
	EbmlElement *Element = NULL;
	int UpperEltFound = 0;

	if (tagsElement == NULL)
		return;

	m_TagPos = tagsElement->GetElementPosition();
	m_TagSize = tagsElement->GetSize();

	tagsElement->Read(m_InputStream, KaxTags::ClassInfos.Context, UpperEltFound, Element, true);

	for (i = 0; i < tagsElement->ListSize(); i++)
	{
		Element = (*tagsElement)[i];
		if(IS_ELEMENT_ID(KaxTag))
		{
			MatroskaTagInfo newTag;
			newTag.targetTypeValue = 50;
			KaxTag *tagElement = (KaxTag*)Element;
			NOTE("New Tag");
			for (j = 0; j < tagElement->ListSize(); j++)
			{
				Element = (*tagElement)[j];
				if(IS_ELEMENT_ID(KaxTagTargets))
				{
					KaxTagTargets *tagTargetsElement = (KaxTagTargets*)Element;					
					for (k = 0; k < tagTargetsElement->ListSize(); k++)
					{
						Element = (*tagTargetsElement)[k];
						if(IS_ELEMENT_ID(KaxTagTrackUID))
						{
							newTag.targetTrackUID = uint64(*static_cast<EbmlUInteger *>(Element));
							NOTE1("- TargetTrackUID : %I64d", newTag.targetTrackUID);
						}
						else if(IS_ELEMENT_ID(KaxTagEditionUID))
						{
							newTag.targetEditionUID = uint64(*static_cast<EbmlUInteger *>(Element));
							NOTE1("- TargetEditionUID : %I64d", newTag.targetEditionUID);
						}
						else if(IS_ELEMENT_ID(KaxTagChapterUID))
						{
							newTag.targetChapterUID = uint64(*static_cast<EbmlUInteger *>(Element));
							NOTE1("- TargetChapterUIDUID : %I64d", newTag.targetChapterUID);
						}
						else if(IS_ELEMENT_ID(KaxTagAttachmentUID))
						{
							newTag.targetAttachmentUID = uint64(*static_cast<EbmlUInteger *>(Element));
							NOTE1("- TargetAttachmentUID : %I64d", newTag.targetAttachmentUID);
						}
						else if(IS_ELEMENT_ID(KaxTagTargetTypeValue))
						{
							newTag.targetTypeValue = uint32(*static_cast<EbmlUInteger *>(Element));
							NOTE1("- TargetTypeValue : %d", newTag.targetTypeValue);
						}
						else if(IS_ELEMENT_ID(KaxTagTargetType))
						{
							newTag.targetType = std::string(*static_cast<KaxTagTargetType *>(Element));
							NOTE1("- TargetType : %s", newTag.targetType.c_str());
						}
					}
				}
				else if(IS_ELEMENT_ID(KaxTagSimple))
				{
					MatroskaSimpleTag newSimpleTag;
					KaxTagSimple *tagSimpleElement = (KaxTagSimple*)Element;
					NOTE("New SimpleTag");
					for (k = 0; k < tagSimpleElement->ListSize(); k++)
					{
						Element = (*tagSimpleElement)[k];
						if(IS_ELEMENT_ID(KaxTagName))
						{
							UTFstring str_utf(*static_cast <EbmlUnicodeString *>(Element));
							std::string str_utf8 = str_utf.GetUTF8();
							std::string str_upper = boost::algorithm::to_upper_copy(str_utf8);
							newSimpleTag.name.SetUTF8(str_upper);
							NOTE1("- Name : %s", newSimpleTag.name.GetUTF8().c_str());
						}
						else if(IS_ELEMENT_ID(KaxTagString))
						{
							newSimpleTag.value = UTFstring(*static_cast <EbmlUnicodeString *>(Element)).c_str();
							NOTE1("- Value : %s", newSimpleTag.value.GetUTF8().c_str());
						}
						else if(IS_ELEMENT_ID(KaxTagDefault))
						{
							newSimpleTag.defaultFlag = uint32(*static_cast<EbmlUInteger *>(Element));
							NOTE1("- TargetTypeValue : %d", newSimpleTag.default);
						}
						else if(IS_ELEMENT_ID(KaxTagLangue))
						{
							newSimpleTag.language = std::string(*static_cast <KaxTagLangue *>(Element));
							NOTE1("- Language : %s", newSimpleTag.language.c_str());
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

static void log_error( const boost::format &fmt )
{
}

static void log_info( const std::string &str )
{
}


int MatroskaAudioParser::FillQueue() 
{
	flush_queue();

	NOTE("MatroskaAudioParser::FillQueue()");

	int UpperElementLevel = 0;
	bool bAllowDummy = false;
	// Elements for different levels
	ElementPtr ElementLevel1;
	ElementPtr ElementLevel2;
	ElementPtr ElementLevel3;
	ElementPtr ElementLevel4;
    ElementPtr ElementLevel5;
	ElementPtr NullElement;
	//m_framebuffer.set_size(0);

	if (IsSeekable(*m_IOCallback)) {
		cluster_entry_ptr currentCluster = FindCluster(m_CurrentTimecode);
		if (currentCluster.get() == NULL)
			return 2;
		int64 clusterFilePos = currentCluster->filePos;

		//log_info(uStringPrintf("cluster %d", currentCluster->clusterNo));

		m_IOCallback->setFilePointer(clusterFilePos);
		// Find the element data
		ElementLevel1 = ElementPtr(m_InputStream.FindNextID(KaxCluster::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (ElementLevel1 == NullElement)
			return 1;

		if (EbmlId(*ElementLevel1) == KaxCluster::ClassInfos.GlobalId) {
			KaxCluster *SegmentCluster = static_cast<KaxCluster *>(ElementLevel1.get());
			uint32 ClusterTimecode = 0;
			MatroskaAudioFrame *prevFrame = NULL;

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
					MatroskaAudioFrame *newFrame = new MatroskaAudioFrame;

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
							if (DataBlock.TrackNum() == m_Tracks.at(m_CurrentTrackNo).trackNumber) {											
								newFrame->timecode = DataBlock.GlobalTimecode();

								if (DataBlock.NumberFrames() > 1) {	
									// The evil lacing has been used
									newFrame->duration = m_Tracks.at(m_CurrentTrackNo).defaultDuration * DataBlock.NumberFrames();

									newFrame->dataBuffer.resize(DataBlock.NumberFrames());
									for (uint32 f = 0; f < DataBlock.NumberFrames(); f++) {
										DataBuffer &buffer = DataBlock.GetBuffer(f);
										newFrame->dataBuffer[f].resize(buffer.Size());								
										memcpy(&newFrame->dataBuffer[f][0], buffer.Buffer(), buffer.Size());
									}
								} else {
									// Non-lacing block		
									newFrame->duration = m_Tracks.at(m_CurrentTrackNo).defaultDuration;

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
						m_Queue.push(newFrame);
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
                        log_info("newFrame ==!! delete!!\n");
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
				log_error(boost::format("clusterNo : %d, m_ClusterIndex.size() : %d") % currentCluster->clusterNo % (int) m_ClusterIndex.size());
				log_info("m_CurrentTimecode == 0 (a)");
			}
		} else {
			m_CurrentTimecode = MAX_UINT64;
		}
		
	} else {
		// Find the element data
		ElementLevel1 = ElementPtr(m_InputStream.FindNextID(KaxCluster::ClassInfos, 0xFFFFFFFFFFFFFFFFL));
		if (ElementLevel1 == NullElement)
			return 1;

		if (EbmlId(*ElementLevel1) == KaxCluster::ClassInfos.GlobalId) {
			KaxCluster *SegmentCluster = static_cast<KaxCluster *>(ElementLevel1.get());
			uint32 ClusterTimecode = 0;

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
					SegmentCluster->InitTimecode(ClusterTimecode, m_TimecodeScale);
				} else  if (EbmlId(*ElementLevel2) == KaxBlockGroup::ClassInfos.GlobalId) {
					//KaxBlockGroup & aBlockGroup = *static_cast<KaxBlockGroup*>(ElementLevel2);

					// Create a new frame
					MatroskaAudioFrame *newFrame = new MatroskaAudioFrame;				

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
							if (DataBlock.TrackNum() == m_Tracks.at(m_CurrentTrackNo).trackNumber) {											
								newFrame->timecode = DataBlock.GlobalTimecode();							

								if (DataBlock.NumberFrames() > 1) {	
									// The evil lacing has been used
									newFrame->duration = m_Tracks.at(m_CurrentTrackNo).defaultDuration * DataBlock.NumberFrames();

									newFrame->dataBuffer.resize(DataBlock.NumberFrames());
									for (uint32 f = 0; f < DataBlock.NumberFrames(); f++) {
										DataBuffer &buffer = DataBlock.GetBuffer(f);
										newFrame->dataBuffer[f].resize(buffer.Size());								
										memcpy(&newFrame->dataBuffer[f][0], buffer.Buffer(), buffer.Size());
									}
								} else {
									// Non-lacing block		
									newFrame->duration = m_Tracks.at(m_CurrentTrackNo).defaultDuration;
									
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
								break;
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
						m_Queue.push(newFrame);
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
		ElementLevel1->SkipData(m_InputStream, ElementLevel1->Generic().Context);
		//_DELETE(ElementLevel3);
		//_DELETE(ElementLevel2);
		//_DELETE(ElementLevel1);
		//delete ElementLevel1;
	}
	//NOTE1("MatroskaAudioParser::FillQueue() - Queue now has %u frames", m_Queue.size());
	return 0;
};

uint64 MatroskaAudioParser::GetClusterTimecode(uint64 filePos) {	
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
					if (ret == -1)
						ElementLevel2 = ElementPtr(m_InputStream.FindNextElement(ElementLevel1->Generic().Context, UpperElementLevel, ElementLevel1->ElementSize(), false));
				}
			}
		}
		//_DELETE(ElementLevel3);
		//_DELETE(ElementLevel2);
		//_DELETE(ElementLevel1);
		//delete ElementLevel1;

		return ret;
	} catch (...) {
		return MAX_UINT64;
	}	
};

cluster_entry_ptr MatroskaAudioParser::FindCluster(uint64 timecode)
{
	try {
		#ifdef _DEBUG_NO_SEEKING
		static size_t callCount = 0;
		if (callCount < m_ClusterIndex.size())
			return &m_ClusterIndex[callCount++];
		else return NULL;
		#endif

		if (timecode == 0)
			// Special case
			return m_ClusterIndex.at(0);
		
		cluster_entry_ptr correctEntry;
		double clusterDuration = (double)(int64)m_ClusterIndex.size() / m_Duration;
		size_t clusterIndex = clusterDuration * (double)(int64)timecode;
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
			NOTE3("MatroskaAudioParser::FindCluster(timecode = %u) seeking to cluster %i at %u", (uint32)(timecode / m_TimecodeScale), (uint32)correctEntry->clusterNo, (uint32)correctEntry->filePos);
		else
			NOTE1("MatroskaAudioParser::FindCluster(timecode = %u) seeking failed", (uint32)(timecode / m_TimecodeScale));

		return correctEntry;
	} catch (...) {
        cluster_entry_ptr null_ptr;
		return null_ptr;
	}
}

void MatroskaAudioParser::CountClusters() 
{
	for (uint32 c = 0; c < m_ClusterIndex.size(); c++) {
		cluster_entry_ptr clusterEntry = m_ClusterIndex.at(c);		
		clusterEntry->clusterNo = c;
	}
}

void MatroskaAudioParser::FixChapterEndTimes()
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

bool MatroskaAudioParser::FindEditionUID(uint64 uid)
{
	for (uint32 c = 0; c < m_Editions.size(); c++) {
		MatroskaEditionInfo &currentEdition = m_Editions.at(c);	
		if (currentEdition.editionUID == uid)
			return true;
	}	
	return false;
}

bool MatroskaAudioParser::FindChapterUID(uint64 uid)
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
	for (uint32 c = 0; c < theChapters.size(); c++) {
		MatroskaChapterInfo &currentChapter = theChapters.at(c);	
		NOTE2("Chapter %u, UID: %u", c, (uint32)currentChapter.chapterUID);
		NOTE1("\tStart Time: %u", (uint32)currentChapter.timeStart);
		NOTE1("\tEnd Time: %u", (uint32)currentChapter.timeEnd);
		for (uint32 d = 0; d < currentChapter.display.size(); d++) {
			NOTE3("\tDisplay %u, String: %s Lang: %s", d, currentChapter.display.at(d).string.GetUTF8().c_str(), currentChapter.display.at(d).lang.c_str());
		}
		for (uint32 t = 0; t < currentChapter.tracks.size(); t++) {
			NOTE2("\tTrack %u, UID: %%u", t, (uint32)currentChapter.tracks.at(t));
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
