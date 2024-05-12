#ifndef DEFLATE_H
#define DEFLATE_H

#include "zutil.h"

/* ===========================================================================
 * Internal compression state.
 */

#define LENGTH_CODES 29
/* number of length codes, not counting the special END_BLOCK code */

#define LITERALS  256
/* number of literal bytes 0..255 */

#define L_CODES (LITERALS+1+LENGTH_CODES)
/* number of Literal or Length codes, including the END_BLOCK code */

#define D_CODES   30
/* number of distance codes */

#define BL_CODES  19
/* number of codes used to transfer the bit lengths */

#define HEAP_SIZE (2*L_CODES+1)
/* maximum heap size */

#define MAX_BITS 15
/* All codes must not exceed MAX_BITS bits */

#define Buf_size 16
/* size of bit buffer in bi_buf */

#define INIT_STATE    42    /* zlib header -> BUSY_STATE */
#ifdef GZIP
#  define GZIP_STATE  57    /* gzip header -> BUSY_STATE | EXTRA_STATE */
#endif
#define EXTRA_STATE   69    /* gzip extra block -> NAME_STATE */
#define NAME_STATE    73    /* gzip file name -> COMMENT_STATE */
#define COMMENT_STATE 91    /* gzip comment -> HCRC_STATE */
#define HCRC_STATE   103    /* gzip header CRC -> BUSY_STATE */
#define BUSY_STATE   113    /* deflate -> FINISH_STATE */
#define FINISH_STATE 666    /* stream complete */
#ifdef MY_ZLIB
/* huffman重编码需要的变量 */
typedef enum {
    HEAD = 16180,   /* i: waiting for magic header */
    FLAGS,      /* i: waiting for method and flags (gzip) */
    TIME,       /* i: waiting for modification time (gzip) */
    OS,         /* i: waiting for extra flags and operating system (gzip) */
    EXLEN,      /* i: waiting for extra length (gzip) */
    EXTRA,      /* i: waiting for extra bytes (gzip) */
    NAME,       /* i: waiting for end of file name (gzip) */
    COMMENT,    /* i: waiting for end of comment (gzip) */
    HCRC,       /* i: waiting for header crc (gzip) */
    DICTID,     /* i: waiting for dictionary check value */
    DICT,       /* waiting for inflateSetDictionary() call */
        TYPE,       /* i: waiting for type bits, including last-flag bit */
        TYPEDO,     /* i: same, but skip check to exit inflate on new block */
        STORED,     /* i: waiting for stored size (length and complement) */
        COPY_,      /* i/o: same as COPY below, but only first time in */
        COPY,       /* i/o: waiting for input or output to copy stored block */
        TABLE,      /* i: waiting for dynamic block table lengths */
        LENLENS,    /* i: waiting for code length code lengths */
        CODELENS,   /* i: waiting for length/lit and distance code lengths */
            LEN_,       /* i: same as LEN below, but only first time in */
            LEN,        /* i: waiting for length/lit/eob code */
            INS0,
            INS1,
            INS2,
            LENEXT,     /* i: waiting for length extra bits */
            DIST,       /* i: waiting for distance code */
            DISTEXT,    /* i: waiting for distance extra bits */
            MATCH,      /* o: waiting for output space to copy string */
            LIT,        /* o: waiting for output space to write literal */
    CHECK,      /* i: waiting for 32-bit check value */
    LENGTH,     /* i: waiting for 32-bit length (gzip) */
    DONE,       /* finished check, done -- remain here until reset */
    SENDINS,
    BAD,        /* got a data error -- remain here until reset */
    MEM,        /* got an inflate() memory error -- remain here until reset */
    ENDOFBLOCK,  /* 输出end of block指令*/
    SKPI_HEADER_END, /* 输出HEADER阶段*/
    SYNC        /* looking for synchronization bytes to restart inflate() */
} deflate_mode;
typedef enum {
    CODES,
    LENS,
    DISTS
} code_type;
typedef struct {
    unsigned char op;           /* operation, extra bits, table bits 1B */
    unsigned char bits;         /* bits in this part of the code (code的比特长度)1B */
    unsigned short val;         /* offset in table or code value 2B */
} code_;
typedef enum {
    STORE,
    FIXED,
    DYNAMIC
} tree_type;
# define _tr_tally_lit_my(s, c, flush) \
  { uch cc = (c); \
    s->sym_buf[s->sym_next++] = 0; \
    s->sym_buf[s->sym_next++] = 0; \
    s->sym_buf[s->sym_next++] = cc; \
    flush = (s->sym_next == s->sym_end); \
   }
/* sym_buf作为一个三元组，(dist, dist >> 8, len)，输出<length, distnce>，统计distance和length的词频，先输出dist（2B），后输出len */
# define _tr_tally_dist_my(s, distance, length, flush) \
  { uch len = (uch)(length); \
    ush dist = (ush)(distance); \
    s->sym_buf[s->sym_next++] = (uch)dist; \
    s->sym_buf[s->sym_next++] = (uch)(dist >> 8); \
    s->sym_buf[s->sym_next++] = len; \
    flush = (s->sym_next == s->sym_end); \
  }
#endif
/* Data structure describing a single value and its code string. */
typedef struct ct_data_s {
    union {
        ush  freq;       /* frequency count */
        ush  code;       /* bit string 编码值？*/
    } fc;
    union {
        ush  dad;        /* father node in Huffman tree */
        ush  len;        /* length of bit string 编码长度？*/
    } dl;
} FAR ct_data;

#define Freq fc.freq
#define Code fc.code
#define Dad  dl.dad
#define Len  dl.len

typedef struct static_tree_desc_s  static_tree_desc;

typedef struct tree_desc_s {
    ct_data *dyn_tree;           /* the dynamic tree */
    int     max_code;            /* largest code with non zero frequency */
    const static_tree_desc *stat_desc;  /* the corresponding static tree */
} FAR tree_desc;

typedef ush Pos;
typedef Pos FAR Posf;
typedef unsigned IPos;

/* A Pos is an index in the character window. We use short instead of int to
 * save space in the various tables. IPos is used only for parameter passing.
 */

typedef struct internal_state {
    z_streamp strm;      /* pointer back to this zlib stream */
    int   status;        /* as the name implies */
    Bytef *pending_buf;  /* output still pending */
    ulg   pending_buf_size; /* size of pending_buf */
    Bytef *pending_out;  /* next pending byte to output to the stream ，已压缩数据的缓存*/
    ulg   pending;       /* nb of bytes in the pending buffer ，已经压缩的数据在pending_out中的位置*/
    int   wrap;          /* bit 0 true for zlib, bit 1 true for gzip */
    gz_headerp  gzhead;  /* gzip header information to write */
    ulg   gzindex;       /* where in extra, name, or comment */
    Byte  method;        /* can only be DEFLATED */
    int   last_flush;    /* value of flush param for previous deflate call */

                /* used by deflate.c: */

    uInt  w_size;        /* LZ77 window size (32K by default) */
    uInt  w_bits;        /* log2(w_size)  (8..16) 当w_bits = 15, w_size = 32K*/
    uInt  w_mask;        /* w_size - 1 */

    Bytef *window; // 滑动窗口，和字典的关系？
    /* Sliding window. Input bytes are read into the second half of the window,
     * and move to the first half later to keep a dictionary of at least wSize
     * bytes. With this organization, matches are limited to a distance of
     * wSize-MAX_MATCH bytes, but this ensures that IO is always
     * performed with a length multiple of the block size. Also, it limits
     * the window size to 64K, which is quite useful on MSDOS.
     * To do: use the user input buffer as sliding window.
     */

    ulg window_size;
    /* Actual size of window: 2*wSize, except when the user input buffer
     * is directly used as sliding window.
     */

    Posf *prev; // unsigned short, 只保存32K的字符串，字典？解决哈希冲突的链表？
    /* Link to older string with same hash index. To limit the size of this
     * array to 64K, this link is maintained only for the last 32K strings.
     * An index in this array is thus a window index modulo 32K.
     */

    Posf *head; /* Heads of the hash chains or NIL. */

    uInt  ins_h;          /* hash index of string to be inserted */
    uInt  hash_size;      /* number of elements in hash table */
    uInt  hash_bits;      /* log2(hash_size) */
    uInt  hash_mask;      /* hash_size-1 */

    uInt  hash_shift;
    /* Number of bits by which ins_h must be shifted at each input
     * step. It must be such that after MIN_MATCH steps, the oldest
     * byte no longer takes part in the hash key, that is:
     *   hash_shift * MIN_MATCH >= hash_bits
     */

    long block_start;
    /* Window position at the beginning of the current output block. Gets
     * negative when the window is moved backwards.
     */

    uInt match_length;           /* length of best match */
    IPos prev_match;             /* previous match */
    int match_available;         /* set if previous match exists */
    uInt strstart;               /* start of string to insert 字符串匹配开始的问题*/
    uInt match_start;            /* start of matching string */
    uInt lookahead;              /* number of valid bytes ahead in window 窗口内剩余的可用匹配字节*/

    uInt prev_length;
    /* Length of the best match at previous step. Matches not greater than this
     * are discarded. This is used in the lazy match evaluation.
     */

    uInt max_chain_length;
    /* To speed up deflation, hash chains are never searched beyond this
     * length.  A higher limit improves compression ratio but degrades the
     * speed.
     */

    uInt max_lazy_match;
    /* Attempt to find a better match only when the current match is strictly
     * smaller than this value. This mechanism is used only for compression
     * levels >= 4.
     */
#   define max_insert_length  max_lazy_match
    /* Insert new strings in the hash table only if the match length is not
     * greater than this length. This saves time but degrades compression.
     * max_insert_length is used only for compression levels <= 3.
     */

    int level;    /* compression level (1..9) */
    int strategy; /* favor or force Huffman coding*/

    uInt good_match;
    /* Use a faster search when the previous match is longer than this */

    int nice_match; /* Stop searching when current match exceeds this */

                /* used by trees.c: */
    /* Didn't use ct_data typedef below to suppress compiler warning */
    struct ct_data_s dyn_ltree[HEAP_SIZE];   /* literal and length tree 为什么要*2？*/
    struct ct_data_s dyn_dtree[2*D_CODES+1]; /* distance tree */
    struct ct_data_s bl_tree[2*BL_CODES+1];  /* Huffman tree for bit lengths */

    struct tree_desc_s l_desc;               /* desc. for literal tree literal/length霍夫曼树*/
    struct tree_desc_s d_desc;               /* desc. for distance tree distance霍夫曼树*/
    struct tree_desc_s bl_desc;              /* desc. for bit length tree 游程编码霍夫曼树*/

    ush bl_count[MAX_BITS+1];
    /* number of codes at each bit length for an optimal tree */

    int heap[2*L_CODES+1];      /* heap used to build the Huffman trees */
    int heap_len;               /* number of elements in the heap */
    int heap_max;               /* element of largest frequency */
    /* The sons of heap[n] are heap[2*n] and heap[2*n+1]. heap[0] is not used.
     * The same heap array is used to build all trees.
     */

    uch depth[2*L_CODES+1];
    /* Depth of each subtree used as tie breaker for trees of equal frequency
     */

    uchf *sym_buf;        /* buffer for distances and literals/lengths LZ77指令缓存 */

    uInt  lit_bufsize;
    /* Size of match buffer for literals/lengths.  There are 4 reasons for
     * limiting lit_bufsize to 64K:
     *   - frequencies can be kept in 16 bit counters
     *   - if compression is not successful for the first block, all input
     *     data is still in the window so we can still emit a stored block even
     *     when input comes from standard input.  (This can also be done for
     *     all blocks if lit_bufsize is not greater than 32K.)
     *   - if compression is not successful for a file smaller than 64K, we can
     *     even emit a stored file instead of a stored block (saving 5 bytes).
     *     This is applicable only for zip (not gzip or zlib).
     *   - creating new Huffman trees less frequently may not provide fast
     *     adaptation to changes in the input data statistics. (Take for
     *     example a binary file with poorly compressible code followed by
     *     a highly compressible string table.) Smaller buffer sizes give
     *     fast adaptation but have of course the overhead of transmitting
     *     trees more frequently.
     *   - I can't count above 4
     */

    uInt sym_next;      /* running index in sym_buf, sym_buf的指针*/
    uInt sym_end;       /* symbol table full when sym_next reaches this，syn_buf的末尾位置，默认为16384 * 3大小 */

    ulg opt_len;        /* bit length of current block with optimal trees */
    ulg static_len;     /* bit length of current block with static trees */
    uInt matches;       /* number of string matches in current block */
    uInt insert;        /* bytes at end of window left to insert */

#ifdef ZLIB_DEBUG
    ulg compressed_len; /* total bit length of compressed file mod 2^32 */
    ulg bits_sent;      /* bit length of compressed data sent mod 2^32 */
#endif

    ush bi_buf;
    /* Output buffer. bits are inserted starting at the bottom (least
     * significant bits).
     */
    int bi_valid;
    /* Number of valid bits in bi_buf.  All bits above the last valid bit
     * are always zero.
     */

    ulg high_water;
    /* High water mark offset in window for initialized bytes -- bytes above
     * this are set to zero in order to avoid memory check warnings when
     * longest match routines access bytes past the input.  This is then
     * updated to the new high water mark.
     */

    /* 还原huffman编码需要的变量*/
#ifdef MY_ZLIB
    int last;                   /* true if processing last block */
    tree_type ttype;
    unsigned ncode;             /* number of code length code lengths ，huffman编码长度的huffman编码长度，即CLL */
    unsigned nlen;              /* number of length code lengths ，length的huffman编码长度，即CL1的后部分 */
    unsigned ndist;             /* number of distance code lengths ，distance的huffman编码长度，即CL2的后部分 */
    unsigned have;              /* number of code lengths in lens[] ，建立huffman树时的 huffman编码长度缓存*/
    code_ const FAR *lencode;    /* starting table for length/literal codes CCL的比特流，CL1表的开头 */
    code_ const FAR *distcode;   /* starting table for distance codes CL2表的开头 */
    unsigned lenbits;           /* index bits for lencode 索引的比特表示*/
    unsigned distbits;          /* index bits for distcode */
    code_ FAR *next;             /* next available space in codes[] */
    code_ codes[852 + 592];         /* space for code tables */
    unsigned extra;             /* extra bits needed */
    unsigned length;            /* literal or length of data to copy */
    unsigned offset;            /* distance back to copy string from */

    unsigned long hold;         /* input bit accumulator, 保存待处理的比特，字节为单位进入hold缓存，通过右移舍弃1比特 */
    unsigned bits;              /* number of bits in "in", 在in内有多少bits，即hold内的bit数 */
    unsigned short lens[320];   /* temporary storage for code lengths 霍夫曼序列缓存*/
    unsigned short work[288];   /* work area for code table building */
    deflate_mode mode;          /* current inflate mode */
#endif
} FAR deflate_state;

/* Output a byte on the stream.
 * IN assertion: there is enough room in pending_buf.
 */
#define put_byte(s, c) {s->pending_buf[s->pending++] = (Bytef)(c);}


#define MIN_LOOKAHEAD (MAX_MATCH+MIN_MATCH+1)
/* Minimum amount of lookahead, except at the end of the input file.
 * See deflate.c for comments about the MIN_MATCH+1.
 */

#define MAX_DIST(s)  ((s)->w_size-MIN_LOOKAHEAD)
/* In order to simplify the code, particularly on 16 bit machines, match
 * distances are limited to MAX_DIST instead of WSIZE.
 */

#define WIN_INIT MAX_MATCH
/* Number of bytes after end of data in window to initialize in order to avoid
   memory checker errors from longest match routines */

        /* in trees.c */
void ZLIB_INTERNAL j_tr_init OF((deflate_state *s));
int ZLIB_INTERNAL _tr_tally OF((deflate_state *s, unsigned dist, unsigned lc));
void ZLIB_INTERNAL _tr_flush_block OF((deflate_state *s, charf *buf,
                        ulg stored_len, int last));
void ZLIB_INTERNAL j_tr_flush_bits OF((deflate_state *s));
void ZLIB_INTERNAL j_tr_align OF((deflate_state *s));
void ZLIB_INTERNAL j_tr_stored_block OF((deflate_state *s, charf *buf,
                        ulg stored_len, int last));

#define d_code(dist) \
   ((dist) < 256 ? j_dist_code[dist] : j_dist_code[256+((dist)>>7)])
/* Mapping from a distance to a distance code. dist is the distance - 1 and
 * must not have side effects. _dist_code[256] and _dist_code[257] are never
 * used.
 */

#ifndef ZLIB_DEBUG
/* Inline versions of _tr_tally for speed: */

#if defined(GEN_TREES_H) || !defined(STDC)
  extern uch ZLIB_INTERNAL j_length_code[];
  extern uch ZLIB_INTERNAL j_dist_code[];
#else
  extern const uch ZLIB_INTERNAL j_length_code[];
  extern const uch ZLIB_INTERNAL j_dist_code[];
#endif
/* sym_buf作为一个三元组，(0, 0, c)，输出<literal cc>，并统计cc频次 */
# define _tr_tally_lit(s, c, flush) \
  { uch cc = (c); \
    s->sym_buf[s->sym_next++] = 0; \
    s->sym_buf[s->sym_next++] = 0; \
    s->sym_buf[s->sym_next++] = cc; \
    s->dyn_ltree[cc].Freq++; \
    flush = (s->sym_next == s->sym_end); \
   }
/* sym_buf作为一个三元组，(dist, dist >> 8, len)，输出<length, distnce>，统计distance和length的词频，先输出dist（2B），后输出len */
# define _tr_tally_dist(s, distance, length, flush) \
  { uch len = (uch)(length); \
    ush dist = (ush)(distance); \
    s->sym_buf[s->sym_next++] = (uch)dist; \
    s->sym_buf[s->sym_next++] = (uch)(dist >> 8); \
    s->sym_buf[s->sym_next++] = len; \
    dist--; \
    s->dyn_ltree[j_length_code[len]+LITERALS+1].Freq++; \
    s->dyn_dtree[d_code(dist)].Freq++; \
    flush = (s->sym_next == s->sym_end); \
  }
#else
# define _tr_tally_lit(s, c, flush) flush = _tr_tally(s, 0, c)
# define _tr_tally_dist(s, distance, length, flush) \
              flush = _tr_tally(s, distance, length)
#endif

#endif /* DEFLATE_H */
