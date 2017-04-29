#include "mkvreader/matroska_parser.h"

#include <iostream>

#include <boost/format.hpp>
#include <boost/ptr_container/ptr_vector.hpp>


void PrintQueue( const boost::ptr_vector< MatroskaFrame > &frames )
{
    int count = 0;
    uint64 prev_timecode = 0;
    for (boost::ptr_vector< MatroskaFrame >::const_iterator frame = frames.begin();
        frame != frames.end();
        ++frame)
    {
        std::vector< uint8 > payload;
        frame->get_payload( payload );
        uint64 delta = frame->timecode - prev_timecode;
        std::cout << (boost::format( "  %3d:  duration=%.3f  delta=%.3f  time=%.3f  len=%d" )
                % count % frame->get_duration() % delta % frame->get_time() % payload.size())
            << "\n";
        prev_timecode = frame->timecode;
        count++;
    }
}


int main( int argc, const char * const argv[] )
{
    const char *filename = (argc >= 2) ? argv[1] : "test.mkv";
    std::cout << "Reading " << filename << "\n";

    MatroskaParser parser( filename );
    if (int failure = parser.Parse( true, true ))
    {
        std::cerr << "Parsing failed: " << failure << "\n";
        return 1;
    }

    std::cout << "File duration is " << parser.GetDuration() << "\n";
    std::cout << "\n";

    typedef MatroskaParser::attachment_list AttachmentsList;
    AttachmentsList attachments = parser.GetAttachmentList();
    if (attachments.empty())
    std::cout << "Found " << attachments.size() << " attachments.\n";
    else for (AttachmentsList::iterator i = attachments.begin(); i != attachments.end(); ++i)
    {
        std::cout
            << "  Filename:         " << i->FileName << "\n"
            << "  MimeType:         " << i->MimeType << "\n"
            << "  Description:      " << i->Description << "\n"
            << "  SourceFilename:   " << i->SourceFilename << "\n"
            << "  SourceStartPos:   " << i->SourceStartPos << "\n"
            << "  SourceDataLength: " << i->SourceDataLength << "\n";
    }
    std::cout << "\n";

    int audio_track = parser.GetFirstTrack( track_audio );
    std::cout << "First audio track is " << audio_track << "\n";
    std::cout << "Index of first audio track is: " << parser.GetTrackIndex( track_audio, 0 ) << "\n";
    std::cout << "\n";

    int video_track = parser.GetFirstTrack( track_video );
    std::cout << "First video track is " << video_track << "\n";
    std::cout << "Index of first video track is: " << parser.GetTrackIndex( track_video, 0 ) << "\n";
    std::cout << "\n";

    track_type track_pcd = (track_type) 252;
    int pcd_track = parser.GetFirstTrack( track_pcd );
    std::cout << "First pcd track is " << pcd_track << "\n";
    std::cout << "Index of first pcd track is: " << parser.GetTrackIndex( track_pcd, 0 ) << "\n";
    std::cout << "\n";

    std::cout << "Num audio tracks: " << parser.GetTrackCount( track_audio ) << "\n";
    std::cout << "Num video tracks: " << parser.GetTrackCount( track_video ) << "\n";
    std::cout << "Num pcd tracks: "   << parser.GetTrackCount( track_pcd ) << "\n";
    std::cout << "Total tracks: "     << parser.GetTrackCount() << "\n";
    std::cout << "\n";

    for (int i = 0; i < parser.GetTrackCount(); i++)
    {
        MatroskaTrackInfo &track = parser.GetTrack( i );
        std::cout << "Info for track " << i << ":\n"
            << "  type:      " << track.trackType << "\n"
            << "  num:       " << track.trackNumber << "\n"
            << "  uid:       " << track.trackUID << "\n"
            << "  codec:     " << track.codecID << "\n"
            << "  name:      " << track.name << "\n"
            << "  lang:      " << track.language << "\n"
            << "  dur:       " << track.duration << "\n"
            << "  channels:  " << (int) track.channels << "\n"
            << "  samp/se:   " << track.samplesPerSec << "\n"
            << "  out/se:    " << track.samplesOutputPerSec << "\n"
            << "  bits/samp: " << (int) track.bitsPerSample << "\n"
            << "  bytes/sec: " << track.avgBytesPerSec << "\n"
            << "  dflt dur:  " << track.defaultDuration << "\n"
            << "\n";
    }

    parser.EnableTrack( video_track );
    parser.EnableTrack( pcd_track );
    parser.SetMaxQueueDepth( 10 );

    int video_count = 0;
    int   pcd_count = 0;
    boost::ptr_vector< MatroskaFrame > video_frames;
    boost::ptr_vector< MatroskaFrame >   pcd_frames;
    bool progress = false;
    while (!parser.IsEof() || progress)
    {
        progress = false;
        MatroskaFrame *video_frame = NULL;
        do
        {
            video_frame = parser.ReadSingleFrame( video_track ); 
            if (video_frame)
            {
                video_count++;
                progress = true;
                video_frames.push_back( video_frame );
            }
        }
        while (video_frame);
        std::cout << "Got " << video_count << " video frames.\n";

        MatroskaFrame *pcd_frame = NULL;
        do
        {
            pcd_frame = parser.ReadSingleFrame( pcd_track ); 
            if (pcd_frame)
            {
                pcd_count++;
                progress = true;
                pcd_frames.push_back( pcd_frame );
            }
        }
        while (pcd_frame);
        std::cout << "Got " <<   pcd_count << "   pcd frames.\n";

        if (parser.IsEof()) std::cout << "EOF.  progress: " << progress << "\n";
    }
    
    std::cout << "\nVideo frames:\n";
    PrintQueue( video_frames );

    std::cout << "\nPCD frames:\n";
    PrintQueue( pcd_frames );
    std::cout << "\n";

    return 0;
}

