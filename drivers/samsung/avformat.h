/*
 * copyright (c) 2001 Fabrice Bellard
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

#ifndef AVFORMAT_AVFORMAT_H
#define AVFORMAT_AVFORMAT_H

#include "avcodec.h"
#include "avio.h"

#define LIBAVFORMAT_VERSION_MAJOR 52
#define LIBAVFORMAT_VERSION_MINOR 64
#define LIBAVFORMAT_VERSION_MICRO  2

#define LIBAVFORMAT_VERSION_INT AV_VERSION_INT(LIBAVFORMAT_VERSION_MAJOR, \
                                               LIBAVFORMAT_VERSION_MINOR, \
                                               LIBAVFORMAT_VERSION_MICRO)
#define LIBAVFORMAT_VERSION     AV_VERSION(LIBAVFORMAT_VERSION_MAJOR,   \
                                           LIBAVFORMAT_VERSION_MINOR,   \
                                           LIBAVFORMAT_VERSION_MICRO)
#define LIBAVFORMAT_BUILD       LIBAVFORMAT_VERSION_INT

#define LIBAVFORMAT_IDENT       "Lavf" AV_STRINGIFY(LIBAVFORMAT_VERSION)

struct AVFormatContext;


/*
 * Public Metadata API.
 * The metadata API allows libavformat to export metadata tags to a client
 * application using a sequence of key/value pairs. Like all strings in FFmpeg,
 * metadata must be stored as UTF-8 encoded Unicode. Note that metadata
 * exported by demuxers isn't checked to be valid UTF-8 in most cases.
 * Important concepts to keep in mind:
 * 1. Keys are unique; there can never be 2 tags with the same key. This is
 *    also meant semantically, i.e., a demuxer should not knowingly produce
 *    several keys that are literally different but semantically identical.
 *    E.g., key=Author5, key=Author6. In this example, all authors must be
 *    placed in the same tag.
 * 2. Metadata is flat, not hierarchical; there are no subtags. If you
 *    want to store, e.g., the email address of the child of producer Alice
 *    and actor Bob, that could have key=alice_and_bobs_childs_email_address.
 * 3. Several modifiers can be applied to the tag name. This is done by
 *    appending a dash character ('-') and the modifier name in the order
 *    they appear in the list below -- e.g. foo-eng-sort, not foo-sort-eng.
 *    a) language -- a tag whose value is localized for a particular language
 *       is appended with the ISO 639-2/B 3-letter language code.
 *       For example: Author-ger=Michael, Author-eng=Mike
 *       The original/default language is in the unqualified "Author" tag.
 *       A demuxer should set a default if it sets any translated tag.
 *    b) sorting  -- a modified version of a tag that should be used for
 *       sorting will have '-sort' appended. E.g. artist="The Beatles",
 *       artist-sort="Beatles, The".
 *
 * 4. Tag names are normally exported exactly as stored in the container to
 *    allow lossless remuxing to the same format. For container-independent
 *    handling of metadata, av_metadata_conv() can convert it to ffmpeg generic
 *    format. Follows a list of generic tag names:
 *
 * album        -- name of the set this work belongs to
 * album_artist -- main creator of the set/album, if different from artist.
 *                 e.g. "Various Artists" for compilation albums.
 * artist       -- main creator of the work
 * comment      -- any additional description of the file.
 * composer     -- who composed the work, if different from artist.
 * copyright    -- name of copyright holder.
 * date         -- date when the work was created, preferably in ISO 8601.
 * disc         -- number of a subset, e.g. disc in a multi-disc collection.
 * encoder      -- name/settings of the software/hardware that produced the file.
 * encoded_by   -- person/group who created the file.
 * filename     -- original name of the file.
 * genre        -- <self-evident>.
 * language     -- main language in which the work is performed, preferably
 *                 in ISO 639-2 format.
 * performer    -- artist who performed the work, if different from artist.
 *                 E.g for "Also sprach Zarathustra", artist would be "Richard
 *                 Strauss" and performer "London Philharmonic Orchestra".
 * publisher    -- name of the label/publisher.
 * title        -- name of the work.
 * track        -- number of this work in the set, can be in form current/total.
 */

#define AV_METADATA_MATCH_CASE      1
#define AV_METADATA_IGNORE_SUFFIX   2
#define AV_METADATA_DONT_STRDUP_KEY 4
#define AV_METADATA_DONT_STRDUP_VAL 8
#define AV_METADATA_DONT_OVERWRITE 16   ///< Don't overwrite existing tags.

typedef struct {
    char *key;
    char *value;
}AVMetadataTag;

typedef struct AVMetadata AVMetadata;
typedef struct AVMetadataConv AVMetadataConv;


/*************************************************/
/* fractional numbers for exact pts handling */

/**
 * The exact value of the fractional number is: 'val + num / den'.
 * num is assumed to be 0 <= num < den.
 */
typedef struct AVFrac {
    int64_t val, num, den;
} AVFrac;

/*************************************************/
/* input/output formats */

struct AVCodecTag;

/** This structure contains the data a format has to probe a file. */
typedef struct AVProbeData {
    const char *filename;
    unsigned char *buf; /**< Buffer must have AVPROBE_PADDING_SIZE of extra allocated bytes filled with zero. */
    int buf_size;       /**< Size of buf except extra allocated bytes */
} AVProbeData;

#define AVPROBE_SCORE_MAX 100               ///< maximum score, half of that is used for file-extension-based detection
#define AVPROBE_PADDING_SIZE 32             ///< extra allocated bytes at the end of the probe buffer

typedef struct AVFormatParameters {
    AVRational time_base;
    int sample_rate;
    int channels;
    int width;
    int height;
    enum PixelFormat pix_fmt;
    int channel; /**< Used to select DV channel. */
    const char *standard; /**< TV standard, NTSC, PAL, SECAM */
    unsigned int mpeg2ts_raw:1;  /**< Force raw MPEG-2 transport stream output, if possible. */
    unsigned int mpeg2ts_compute_pcr:1; /**< Compute exact PCR for each transport
                                            stream packet (only meaningful if
                                            mpeg2ts_raw is TRUE). */
    unsigned int initial_pause:1;       /**< Do not begin to play the stream
                                            immediately (RTSP only). */
    unsigned int prealloced_context:1;
#if LIBAVFORMAT_VERSION_INT < (53<<16)
    enum CodecID video_codec_id;
    enum CodecID audio_codec_id;
#endif
} AVFormatParameters;

//! Demuxer will use url_fopen, no opened file should be provided by the caller.
#define AVFMT_NOFILE        0x0001
#define AVFMT_NEEDNUMBER    0x0002 /**< Needs '%d' in filename. */
#define AVFMT_SHOW_IDS      0x0008 /**< Show format stream IDs numbers. */
#define AVFMT_RAWPICTURE    0x0020 /**< Format wants AVPicture structure for
                                      raw picture data. */
#define AVFMT_GLOBALHEADER  0x0040 /**< Format wants global header. */
#define AVFMT_NOTIMESTAMPS  0x0080 /**< Format does not need / have any timestamps. */
#define AVFMT_GENERIC_INDEX 0x0100 /**< Use generic index building code. */
#define AVFMT_TS_DISCONT    0x0200 /**< Format allows timestamp discontinuities. */
#define AVFMT_VARIABLE_FPS  0x0400 /**< Format allows variable fps. */
#define AVFMT_NODIMENSIONS  0x0800 /**< Format does not need width/height */

typedef struct AVOutputFormat {
    const char *name;
    /**
     * Descriptive name for the format, meant to be more human-readable
     * than name. You should use the NULL_IF_CONFIG_SMALL() macro
     * to define it.
     */
    const char *long_name;
    const char *mime_type;
    const char *extensions; /**< comma-separated filename extensions */
    /** size of private data so that it can be allocated in the wrapper */
    int priv_data_size;
    /* output support */
    enum CodecID audio_codec; /**< default audio codec */
    enum CodecID video_codec; /**< default video codec */
    int (*write_header)(struct AVFormatContext *);
    int (*write_packet)(struct AVFormatContext *, AVPacket *pkt);
    int (*write_trailer)(struct AVFormatContext *);
    /** can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER, AVFMT_GLOBALHEADER */
    int flags;
    /** Currently only used to set pixel format if not YUV420P. */
    int (*set_parameters)(struct AVFormatContext *, AVFormatParameters *);
    int (*interleave_packet)(struct AVFormatContext *, AVPacket *out,
                             AVPacket *in, int flush);

    /**
     * List of supported codec_id-codec_tag pairs, ordered by "better
     * choice first". The arrays are all terminated by CODEC_ID_NONE.
     */
    const struct AVCodecTag * const *codec_tag;

    enum CodecID subtitle_codec; /**< default subtitle codec */

    const AVMetadataConv *metadata_conv;

    /* private fields */
    struct AVOutputFormat *next;
} AVOutputFormat;

typedef struct AVInputFormat {
    const char *name;
    /**
     * Descriptive name for the format, meant to be more human-readable
     * than name. You should use the NULL_IF_CONFIG_SMALL() macro
     * to define it.
     */
    const char *long_name;
    /** Size of private data so that it can be allocated in the wrapper. */
    int priv_data_size;
    /**
     * Tell if a given file has a chance of being parsed as this format.
     * The buffer provided is guaranteed to be AVPROBE_PADDING_SIZE bytes
     * big so you do not have to check for that unless you need more.
     */
    int (*read_probe)(AVProbeData *);
    /** Read the format header and initialize the AVFormatContext
       structure. Return 0 if OK. 'ap' if non-NULL contains
       additional parameters. Only used in raw format right
       now. 'av_new_stream' should be called to create new streams.  */
    int (*read_header)(struct AVFormatContext *,
                       AVFormatParameters *ap);
    /** Read one packet and put it in 'pkt'. pts and flags are also
       set. 'av_new_stream' can be called only if the flag
       AVFMTCTX_NOHEADER is used.
       @return 0 on success, < 0 on error.
               When returning an error, pkt must not have been allocated
               or must be freed before returning */
    int (*read_packet)(struct AVFormatContext *, AVPacket *pkt);
    /** Close the stream. The AVFormatContext and AVStreams are not
       freed by this function */
    int (*read_close)(struct AVFormatContext *);

#if LIBAVFORMAT_VERSION_MAJOR < 53
    /**
     * Seek to a given timestamp relative to the frames in
     * stream component stream_index.
     * @param stream_index Must not be -1.
     * @param flags Selects which direction should be preferred if no exact
     *              match is available.
     * @return >= 0 on success (but not necessarily the new offset)
     */
    int (*read_seek)(struct AVFormatContext *,
                     int stream_index, int64_t timestamp, int flags);
#endif
    /**
     * Gets the next timestamp in stream[stream_index].time_base units.
     * @return the timestamp or AV_NOPTS_VALUE if an error occurred
     */
    int64_t (*read_timestamp)(struct AVFormatContext *s, int stream_index,
                              int64_t *pos, int64_t pos_limit);
    /** Can use flags: AVFMT_NOFILE, AVFMT_NEEDNUMBER. */
    int flags;
    /** If extensions are defined, then no probe is done. You should
       usually not use extension format guessing because it is not
       reliable enough */
    const char *extensions;
    /** General purpose read-only value that the format can use. */
    int value;

    /** Starts/resumes playing - only meaningful if using a network-based format
       (RTSP). */
    int (*read_play)(struct AVFormatContext *);

    /** Pauses playing - only meaningful if using a network-based format
       (RTSP). */
    int (*read_pause)(struct AVFormatContext *);

    const struct AVCodecTag * const *codec_tag;

    /**
     * Seeks to timestamp ts.
     * Seeking will be done so that the point from which all active streams
     * can be presented successfully will be closest to ts and within min/max_ts.
     * Active streams are all streams that have AVStream.discard < AVDISCARD_ALL.
     */
    int (*read_seek2)(struct AVFormatContext *s, int stream_index, int64_t min_ts, int64_t ts, int64_t max_ts, int flags);

    const AVMetadataConv *metadata_conv;

    /* private fields */
    struct AVInputFormat *next;
} AVInputFormat;

enum AVStreamParseType {
    AVSTREAM_PARSE_NONE,
    AVSTREAM_PARSE_FULL,       /**< full parsing and repack */
    AVSTREAM_PARSE_HEADERS,    /**< Only parse headers, do not repack. */
    AVSTREAM_PARSE_TIMESTAMPS, /**< full parsing and interpolation of timestamps for frames not starting on a packet boundary */
};

typedef struct AVIndexEntry {
    int64_t pos;
    int64_t timestamp;
#define AVINDEX_KEYFRAME 0x0001
    int flags:2;
    int size:30; //Yeah, trying to keep the size of this small to reduce memory requirements (it is 24 vs. 32 bytes due to possible 8-byte alignment).
    int min_distance;         /**< Minimum distance between this and the previous keyframe, used to avoid unneeded searching. */
} AVIndexEntry;

#define AV_DISPOSITION_DEFAULT   0x0001
#define AV_DISPOSITION_DUB       0x0002
#define AV_DISPOSITION_ORIGINAL  0x0004
#define AV_DISPOSITION_COMMENT   0x0008
#define AV_DISPOSITION_LYRICS    0x0010
#define AV_DISPOSITION_KARAOKE   0x0020

/**
 * Stream structure.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 * sizeof(AVStream) must not be used outside libav*.
 */
typedef struct AVStream {
    int index;    /**< stream index in AVFormatContext */
    int id;       /**< format-specific stream ID */
    AVCodecContext *codec; /**< codec context */
    /**
     * Real base framerate of the stream.
     * This is the lowest framerate with which all timestamps can be
     * represented accurately (it is the least common multiple of all
     * framerates in the stream). Note, this value is just a guess!
     * For example, if the time base is 1/90000 and all frames have either
     * approximately 3600 or 1800 timer ticks, then r_frame_rate will be 50/1.
     */
    AVRational r_frame_rate;
    void *priv_data;

    /* internal data used in av_find_stream_info() */
    int64_t first_dts;
    /** encoding: pts generation when outputting stream */
    struct AVFrac pts;

    /**
     * This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. For fixed-fps content,
     * time base should be 1/framerate and timestamp increments should be 1.
     */
    AVRational time_base;
    int pts_wrap_bits; /**< number of bits in pts (used for wrapping control) */
    /* ffmpeg.c private use */
    int stream_copy; /**< If set, just copy stream. */
    enum AVDiscard discard; ///< Selects which packets can be discarded at will and do not need to be demuxed.
    //FIXME move stuff to a flags field?
    /** Quality, as it has been removed from AVCodecContext and put in AVVideoFrame.
     * MN: dunno if that is the right place for it */
    float quality;
    /**
     * Decoding: pts of the first frame of the stream, in stream time base.
     * Only set this if you are absolutely 100% sure that the value you set
     * it to really is the pts of the first frame.
     * This may be undefined (AV_NOPTS_VALUE).
     * @note The ASF header does NOT contain a correct start_time the ASF
     * demuxer must NOT set this.
     */
    int64_t start_time;
    /**
     * Decoding: duration of the stream, in stream time base.
     * If a source file does not specify a duration, but does specify
     * a bitrate, this value will be estimated from bitrate and file size.
     */
    int64_t duration;

#if LIBAVFORMAT_VERSION_INT < (53<<16)
    char language[4]; /** ISO 639-2/B 3-letter language code (empty string if undefined) */
#endif

    /* av_read_frame() support */
    enum AVStreamParseType need_parsing;
    struct AVCodecParserContext *parser;

    int64_t cur_dts;
    int last_IP_duration;
    int64_t last_IP_pts;
    /* av_seek_frame() support */
    AVIndexEntry *index_entries; /**< Only used if the format does not
                                    support seeking natively. */
    int nb_index_entries;
    unsigned int index_entries_allocated_size;

    int64_t nb_frames;                 ///< number of frames in this stream if known or 0

#if LIBAVFORMAT_VERSION_INT < (53<<16)
    int64_t unused[4+1];

    char *filename; /**< source filename of the stream */
#endif

    int disposition; /**< AV_DISPOSITION_* bit field */

    AVProbeData probe_data;
#define MAX_REORDER_DELAY 16
    int64_t pts_buffer[MAX_REORDER_DELAY+1];

    /**
     * sample aspect ratio (0 if unknown)
     * - encoding: Set by user.
     * - decoding: Set by libavformat.
     */
    AVRational sample_aspect_ratio;

    AVMetadata *metadata;

    /* av_read_frame() support */
    const uint8_t *cur_ptr;
    int cur_len;
    AVPacket cur_pkt;

    // Timestamp generation support:
    /**
     * Timestamp corresponding to the last dts sync point.
     *
     * Initialized when AVCodecParserContext.dts_sync_point >= 0 and
     * a DTS is received from the underlying container. Otherwise set to
     * AV_NOPTS_VALUE by default.
     */
    int64_t reference_dts;

    /**
     * Number of packets to buffer for codec probing
     * NOT PART OF PUBLIC API
     */
#define MAX_PROBE_PACKETS 2500
    int probe_packets;

    /**
     * last packet in packet_buffer for this stream when muxing.
     * used internally, NOT PART OF PUBLIC API, dont read or write from outside of libav*
     */
    struct AVPacketList *last_in_packet_buffer;

    /**
     * Average framerate
     */
    AVRational avg_frame_rate;

    /**
     * Number of frames that have been demuxed during av_find_stream_info()
     */
    int codec_info_nb_frames;
} AVStream;

#define AV_PROGRAM_RUNNING 1

/**
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 * sizeof(AVProgram) must not be used outside libav*.
 */
typedef struct AVProgram {
    int            id;
#if LIBAVFORMAT_VERSION_INT < (53<<16)
    char           *provider_name; ///< network name for DVB streams
    char           *name;          ///< service name for DVB streams
#endif
    int            flags;
    enum AVDiscard discard;        ///< selects which program to discard and which to feed to the caller
    unsigned int   *stream_index;
    unsigned int   nb_stream_indexes;
    AVMetadata *metadata;
} AVProgram;

#define AVFMTCTX_NOHEADER      0x0001 /**< signal that no header is present
                                         (streams are added dynamically) */

typedef struct AVChapter {
    int id;                 ///< unique ID to identify the chapter
    AVRational time_base;   ///< time base in which the start/end timestamps are specified
    int64_t start, end;     ///< chapter start/end time in time_base units
#if LIBAVFORMAT_VERSION_INT < (53<<16)
    char *title;            ///< chapter title
#endif
    AVMetadata *metadata;
} AVChapter;

#if LIBAVFORMAT_VERSION_MAJOR < 53
#define MAX_STREAMS 20
#else
#define MAX_STREAMS 100
#endif

/**
 * Format I/O context.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 * sizeof(AVFormatContext) must not be used outside libav*.
 */
typedef struct AVFormatContext {
    const AVClass *av_class; /**< Set by avformat_alloc_context. */
    /* Can only be iformat or oformat, not both at the same time. */
    struct AVInputFormat *iformat;
    struct AVOutputFormat *oformat;
    void *priv_data;
    ByteIOContext *pb;
    unsigned int nb_streams;
    AVStream *streams[MAX_STREAMS];
    char filename[1024]; /**< input or output filename */
    /* stream info */
    int64_t timestamp;
#if LIBAVFORMAT_VERSION_INT < (53<<16)
    char title[512];
    char author[512];
    char copyright[512];
    char comment[512];
    char album[512];
    int year;  /**< ID3 year, 0 if none */
    int track; /**< track number, 0 if none */
    char genre[32]; /**< ID3 genre */
#endif

    int ctx_flags; /**< Format-specific flags, see AVFMTCTX_xx */
    /* private data for pts handling (do not modify directly). */
    /** This buffer is only needed when packets were already buffered but
       not decoded, for example to get the codec parameters in MPEG
       streams. */
    struct AVPacketList *packet_buffer;

    /** Decoding: position of the first frame of the component, in
       AV_TIME_BASE fractional seconds. NEVER set this value directly:
       It is deduced from the AVStream values.  */
    int64_t start_time;
    /** Decoding: duration of the stream, in AV_TIME_BASE fractional
       seconds. Only set this value if you know none of the individual stream
       durations and also dont set any of them. This is deduced from the
       AVStream values if not set.  */
    int64_t duration;
    /** decoding: total file size, 0 if unknown */
    int64_t file_size;
    /** Decoding: total stream bitrate in bit/s, 0 if not
       available. Never set it directly if the file_size and the
       duration are known as FFmpeg can compute it automatically. */
    int bit_rate;

    /* av_read_frame() support */
    AVStream *cur_st;
#if LIBAVFORMAT_VERSION_INT < (53<<16)
    const uint8_t *cur_ptr_deprecated;
    int cur_len_deprecated;
    AVPacket cur_pkt_deprecated;
#endif

    /* av_seek_frame() support */
    int64_t data_offset; /** offset of the first packet */
    int index_built;

    int mux_rate;
    unsigned int packet_size;
    int preload;
    int max_delay;

#define AVFMT_NOOUTPUTLOOP -1
#define AVFMT_INFINITEOUTPUTLOOP 0
    /** number of times to loop output in formats that support it */
    int loop_output;

    int flags;
#define AVFMT_FLAG_GENPTS       0x0001 ///< Generate missing pts even if it requires parsing future frames.
#define AVFMT_FLAG_IGNIDX       0x0002 ///< Ignore index.
#define AVFMT_FLAG_NONBLOCK     0x0004 ///< Do not block when reading packets from input.
#define AVFMT_FLAG_IGNDTS       0x0008 ///< Ignore DTS on frames that contain both DTS & PTS
#define AVFMT_FLAG_NOFILLIN     0x0010 ///< Do not infer any values from other values, just return what is stored in the container
#define AVFMT_FLAG_NOPARSE      0x0020 ///< Do not use AVParsers, you also must set AVFMT_FLAG_NOFILLIN as the fillin code works on frames and no parsing -> no frames. Also seeking to frames can not work if parsing to find frame boundaries has been disabled
#define AVFMT_FLAG_RTP_HINT     0x0040 ///< Add RTP hinting to the output file

    int loop_input;
    /** decoding: size of data to probe; encoding: unused. */
    unsigned int probesize;

    /**
     * Maximum time (in AV_TIME_BASE units) during which the input should
     * be analyzed in av_find_stream_info().
     */
    int max_analyze_duration;

    const uint8_t *key;
    int keylen;

    unsigned int nb_programs;
    AVProgram **programs;

    /**
     * Forced video codec_id.
     * Demuxing: Set by user.
     */
    enum CodecID video_codec_id;
    /**
     * Forced audio codec_id.
     * Demuxing: Set by user.
     */
    enum CodecID audio_codec_id;
    /**
     * Forced subtitle codec_id.
     * Demuxing: Set by user.
     */
    enum CodecID subtitle_codec_id;

    /**
     * Maximum amount of memory in bytes to use for the index of each stream.
     * If the index exceeds this size, entries will be discarded as
     * needed to maintain a smaller size. This can lead to slower or less
     * accurate seeking (depends on demuxer).
     * Demuxers for which a full in-memory index is mandatory will ignore
     * this.
     * muxing  : unused
     * demuxing: set by user
     */
    unsigned int max_index_size;

    /**
     * Maximum amount of memory in bytes to use for buffering frames
     * obtained from realtime capture devices.
     */
    unsigned int max_picture_buffer;

    unsigned int nb_chapters;
    AVChapter **chapters;

    /**
     * Flags to enable debugging.
     */
    int debug;
#define FF_FDEBUG_TS        0x0001

    /**
     * Raw packets from the demuxer, prior to parsing and decoding.
     * This buffer is used for buffering packets until the codec can
     * be identified, as parsing cannot be done without knowing the
     * codec.
     */
    struct AVPacketList *raw_packet_buffer;
    struct AVPacketList *raw_packet_buffer_end;

    struct AVPacketList *packet_buffer_end;

    AVMetadata *metadata;

    /**
     * Remaining size available for raw_packet_buffer, in bytes.
     * NOT PART OF PUBLIC API
     */
#define RAW_PACKET_BUFFER_SIZE 2500000
    int raw_packet_buffer_remaining_size;

    /**
     * Start time of the stream in real world time, in microseconds
     * since the unix epoch (00:00 1st January 1970). That is, pts=0
     * in the stream was captured at this real world time.
     * - encoding: Set by user.
     * - decoding: Unused.
     */
    int64_t start_time_realtime;
} AVFormatContext;

typedef struct AVPacketList {
    AVPacket pkt;
    struct AVPacketList *next;
} AVPacketList;

#if LIBAVFORMAT_VERSION_INT < (53<<16)
extern AVInputFormat *first_iformat;
extern AVOutputFormat *first_oformat;
#endif

#define AVSEEK_FLAG_BACKWARD 1 ///< seek backward
#define AVSEEK_FLAG_BYTE     2 ///< seeking based on position in bytes
#define AVSEEK_FLAG_ANY      4 ///< seek to any frame, even non-keyframes
#define AVSEEK_FLAG_FRAME    8 ///< seeking based on frame number

#endif /* AVFORMAT_AVFORMAT_H */
