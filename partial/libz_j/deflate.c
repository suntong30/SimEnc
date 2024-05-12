#include "deflate.h"
/* ===========================================================================
 *  Function prototypes.
 */
typedef enum {
    need_more,      /* block not completed, need more input or more output */
    block_done,     /* block flush performed */
    finish_started, /* finish started, need only more output at next deflate */
    finish_done     /* finish done, accept no more input or output */
} block_state;

typedef block_state (*compress_func) OF((deflate_state *s, int flush));
/* Compression function. Returns the block state after the call. */

local int deflateStateCheck      OF((z_streamp strm));
local void putShortMSB    OF((deflate_state *s, uInt b));
local void flush_pending  OF((z_streamp strm));
local unsigned read_buf   OF((z_streamp strm, Bytef *buf, unsigned size));

#ifdef ZLIB_DEBUG
local  void check_match OF((deflate_state *s, IPos start, IPos match,
                            int length));
#endif

/* rank Z_BLOCK between Z_NO_FLUSH and Z_PARTIAL_FLUSH */
#define RANK(f) (((f) * 2) - ((f) > 4 ? 9 : 0))
/* ========================================================================= */
int ZEXPORT j_deflateInit_(strm, level, version, stream_size)
    z_streamp strm;
    int level;
    const char *version;
    int stream_size;
{
    return j_deflateInit2_(strm, level, Z_DEFLATED, MAX_WBITS, DEF_MEM_LEVEL,
                         Z_DEFAULT_STRATEGY, version, stream_size);
    /* To do: ignore strm->next_in if we use it as window */
}

/* ========================================================================= */
int ZEXPORT j_deflateInit2_(strm, level, method, windowBits, memLevel, strategy,
                  version, stream_size)
    z_streamp strm;
    int  level;
    int  method;
    int  windowBits;
    int  memLevel;
    int  strategy;
    const char *version;
    int stream_size;
{
  
    deflate_state *s;
    int wrap = 1;
    if (strm == Z_NULL) return Z_STREAM_ERROR;

    strm->msg = Z_NULL;
    if (strm->zalloc == (alloc_func)0) {
#ifdef Z_SOLO
        return Z_STREAM_ERROR;
#else
        strm->zalloc = zcalloc;
        strm->opaque = (voidpf)0;
#endif
    }
    if (strm->zfree == (free_func)0)
#ifdef Z_SOLO
        return Z_STREAM_ERROR;
#else
        strm->zfree = zcfree;
#endif

#ifdef FASTEST
    if (level != 0) level = 1;
#else
    if (level == Z_DEFAULT_COMPRESSION) level = 6;
#endif

    if (windowBits < 0) { /* suppress zlib wrapper */
        wrap = 0;
        if (windowBits < -15)
            return Z_STREAM_ERROR;
        windowBits = -windowBits;
    }
#ifdef GZIP
    else if (windowBits > 15) {
        wrap = 2;       /* write gzip wrapper instead */
        windowBits -= 16;
    }
#endif
    if (memLevel < 1 || memLevel > MAX_MEM_LEVEL || method != Z_DEFLATED ||
        windowBits < 8 || windowBits > 15 || level < 0 || level > 9 ||
        strategy < 0 || strategy > Z_FIXED || (windowBits == 8 && wrap != 1)) {
        return Z_STREAM_ERROR;
    }
    if (windowBits == 8) windowBits = 9;  /* until 256-byte window bug fixed */
    s = (deflate_state *) ZALLOC(strm, 1, sizeof(deflate_state));
    if (s == Z_NULL) return Z_MEM_ERROR;
    strm->state = (struct internal_state FAR *)s;
    s->strm = strm;
    s->status = INIT_STATE;     /* to pass state test in deflateReset() */

    s->wrap = wrap;
    s->gzhead = Z_NULL;
    s->w_bits = (uInt)windowBits;
    s->w_size = 1 << s->w_bits;
    s->w_mask = s->w_size - 1;

    s->hash_bits = (uInt)memLevel + 7;
    s->hash_size = 1 << s->hash_bits;
    s->hash_mask = s->hash_size - 1;
    s->hash_shift =  ((s->hash_bits + MIN_MATCH-1) / MIN_MATCH);

    s->window = (Bytef *) ZALLOC(strm, s->w_size, 2*sizeof(Byte)); // 滑动窗口，两倍窗口窗口大小
    s->prev   = (Posf *)  ZALLOC(strm, s->w_size, sizeof(Pos)); // 哈希冲突链表
    s->head   = (Posf *)  ZALLOC(strm, s->hash_size, sizeof(Pos)); // 哈希表

    s->high_water = 0;      /* nothing written to s->window yet */

    s->lit_bufsize = 1 << (memLevel + 6); /* 16K elements by default */

    /* We   pending_buf and sym_buf. This works since the average size
     * for length/distance pairs over any compressed block is assured to be 31
     * bits or less.
     *
     * Analysis: The longest fixed codes are a length code of 8 bits plus 5
     * extra bits, for lengths 131 to 257. The longest fixed distance codes are
     * 5 bits plus 13 extra bits, for distances 16385 to 32768. The longest
     * possible fixed-codes length/distance pair is then 31 bits total.
     *
     * sym_buf starts one-fourth of the way into pending_buf. So there are
     * three bytes in sym_buf for every four bytes in pending_buf. Each symbol
     * in sym_buf is three bytes -- two for the distance and one for the
     * literal/length. As each symbol is consumed, the pointer to the next
     * sym_buf value to read moves forward three bytes. From that symbol, up to
     * 31 bits are written to pending_buf. The closest the written pending_buf
     * bits gets to the next sym_buf symbol to read is just before the last
     * code is written. At that time, 31*(n - 2) bits have been written, just
     * after 24*(n - 2) bits have been consumed from sym_buf. sym_buf starts at
     * 8*n bits into pending_buf. (Note that the symbol buffer fills when n - 1
     * symbols are written.) The closest the writing gets to what is unread is
     * then n + 14 bits. Here n is lit_bufsize, which is 16384 by default, and
     * can range from 128 to 32768.
     *
     * Therefore, at a minimum, there are 142 bits of space between what is
     * written and what is read in the overlain buffers, so the symbols cannot
     * be overwritten by the compressed data. That space is actually 139 bits,
     * due to the three-bit fixed-code block header.
     *
     * That covers the case where either Z_FIXED is specified, forcing fixed
     * codes, or when the use of fixed codes is chosen, because that choice
     * results in a smaller compressed block than dynamic codes. That latter
     * condition then assures that the above analysis also covers all dynamic
     * blocks. A dynamic-code block will only be chosen to be emitted if it has
     * fewer bits than a fixed-code block would for the same set of symbols.
     * Therefore its average symbol length is assured to be less than 31. So
     * the compressed data for a dynamic block also cannot overwrite the
     * symbols from which it is being constructed.
     */
    /* 我把pending_buf和sym_buf大小变为了4x */
    s->pending_buf = (uchf *) ZALLOC(strm, s->lit_bufsize, 16);
    s->pending_buf_size = (ulg)s->lit_bufsize * 16;

    if (s->window == Z_NULL || s->prev == Z_NULL || s->head == Z_NULL ||
        s->pending_buf == Z_NULL) {
        s->status = FINISH_STATE;
        strm->msg = ERR_MSG(Z_MEM_ERROR);
        j_deflateEnd (strm);
        return Z_MEM_ERROR;
    }
    s->sym_buf = s->pending_buf + s->lit_bufsize;
    s->sym_end = (s->lit_bufsize - 1) * 12;
    /* We avoid equality with lit_bufsize*3 because of wraparound at 64K
     * on 16 bit machines and because stored blocks are restricted to
     * 64K-1 bytes.
     */

    s->level = level;
    s->strategy = strategy;
    s->method = (Byte)method;

#ifdef MY_ZLIB
    /* huffman重编码需要的初始化 */
    s->mode = HEAD;
#endif
    return j_deflateReset(strm);
}

/* =========================================================================
 * Check for a valid deflate stream state. Return 0 if ok, 1 if not.
 */
local int deflateStateCheck(strm)
    z_streamp strm;
{
    deflate_state *s;
    if (strm == Z_NULL ||
        strm->zalloc == (alloc_func)0 || strm->zfree == (free_func)0)
        return 1;
    s = strm->state;
    if (s == Z_NULL || s->strm != strm || (s->status != INIT_STATE &&
#ifdef GZIP
                                           s->status != GZIP_STATE &&
#endif
                                           s->status != EXTRA_STATE &&
                                           s->status != NAME_STATE &&
                                           s->status != COMMENT_STATE &&
                                           s->status != HCRC_STATE &&
                                           s->status != BUSY_STATE &&
                                           s->status != FINISH_STATE))
        return 1;
    return 0;
}
/* ========================================================================= */
int ZEXPORT j_deflateResetKeep(strm)
    z_streamp strm;
{
    deflate_state *s;

    if (deflateStateCheck(strm)) {
        return Z_STREAM_ERROR;
    }

    strm->total_in = strm->total_out = 0;
    strm->msg = Z_NULL; /* use zfree if we ever allocate msg dynamically */
    strm->data_type = Z_UNKNOWN;

    s = (deflate_state *)strm->state;
    s->pending = 0;
    s->pending_out = s->pending_buf;

    if (s->wrap < 0) {
        s->wrap = -s->wrap; /* was made negative by deflate(..., Z_FINISH); */
    }
    s->status =
#ifdef GZIP
        s->wrap == 2 ? GZIP_STATE :
#endif
        INIT_STATE;
    strm->adler =
#ifdef GZIP
        s->wrap == 2 ? crc32(0L, Z_NULL, 0) :
#endif
        adler32(0L, Z_NULL, 0);
    s->last_flush = -2;

    j_tr_init(s);

    return Z_OK;
}

/* ========================================================================= */
int ZEXPORT j_deflateReset(strm)
    z_streamp strm;
{
    int ret;

    ret = j_deflateResetKeep(strm);
    return ret;
}

/* ========================================================================= */
int ZEXPORT j_deflateSetHeader(strm, head)
    z_streamp strm;
    gz_headerp head;
{
    if (deflateStateCheck(strm) || strm->state->wrap != 2)
        return Z_STREAM_ERROR;
    strm->state->gzhead = head;
    return Z_OK;
}

/* ========================================================================= */
int ZEXPORT j_deflatePending(strm, pending, bits)
    unsigned *pending;
    int *bits;
    z_streamp strm;
{
    if (deflateStateCheck(strm)) return Z_STREAM_ERROR;
    if (pending != Z_NULL)
        *pending = strm->state->pending;
    if (bits != Z_NULL)
        *bits = strm->state->bi_valid;
    return Z_OK;
}
/* =========================================================================
 * Put a short in the pending buffer. The 16-bit value is put in MSB order.
 * IN assertion: the stream state is correct and there is enough room in
 * pending_buf.
 */
local void putShortMSB(s, b)
    deflate_state *s;
    uInt b;
{
    put_byte(s, (Byte)(b >> 8));
    put_byte(s, (Byte)(b & 0xff));
}

/* =========================================================================
 * Flush as much pending output as possible. All deflate() output, except for
 * some deflate_stored() output, goes through this function so some
 * applications may wish to modify it to avoid allocating a large
 * strm->next_out buffer and copying into it. (See also read_buf()).
 */
local void flush_pending(strm)
    z_streamp strm;
{
    unsigned len;
    deflate_state *s = strm->state;

    j_tr_flush_bits(s); // 把bi_buf已有的字节输出到pending
    len = s->pending;
    if (len > strm->avail_out) len = strm->avail_out;
    if (len == 0) return;

    zmemcpy(strm->next_out, s->pending_out, len);
    strm->next_out  += len;
    s->pending_out  += len;
    strm->total_out += len;
    strm->avail_out -= len;
    s->pending      -= len;
    if (s->pending == 0) {
        s->pending_out = s->pending_buf;
    }
}
/* ========================================================================= */
int ZEXPORT j_deflate(strm, flush)
    z_streamp strm;
    int flush;
{
    
    int old_flush; /* value of flush param for previous deflate call */
    deflate_state *s;

    if (deflateStateCheck(strm) || flush > Z_BLOCK || flush < 0) { // 检查状态变量
        return Z_STREAM_ERROR;
    }
    s = strm->state;

    if (strm->next_out == Z_NULL ||
        (strm->avail_in != 0 && strm->next_in == Z_NULL) ||
        (s->status == FINISH_STATE && flush != Z_FINISH)) {
        ERR_RETURN(strm, Z_STREAM_ERROR);
    }
    if (strm->avail_out == 0) ERR_RETURN(strm, Z_BUF_ERROR);

    old_flush = s->last_flush;
    s->last_flush = flush;

    /* Flush as much pending output as possible */
    if (s->pending != 0) { // 上次退出时未把pending全部flush，此处flush余下的
        flush_pending(strm); // 把已生成的bits流输出
        if (strm->avail_out == 0) {
            /* Since avail_out is 0, deflate will be called again with
             * more output space, but possibly with both pending and
             * avail_in equal to zero. There won't be anything to do,
             * but this is not an error situation so make sure we
             * return OK instead of BUF_ERROR at next call of deflate:
             * 预防error错误判断
             */
            s->last_flush = -1;
            return Z_OK;
        }
    /* Make sure there is something to do and avoid duplicate consecutive
     * flushes. For repeated and useless calls with Z_FINISH, we keep
     * returning Z_STREAM_END instead of Z_BUF_ERROR.
     */
    } else if (strm->avail_in == 0 && RANK(flush) <= RANK(old_flush) && flush != Z_FINISH) {
        ERR_RETURN(strm, Z_BUF_ERROR);
    }
    int ret = deflate_my(strm, flush);
    if(ret != Z_OK && ret != Z_STREAM_END)
        return ret;
    return s->pending != 0 ? Z_OK : Z_STREAM_END; // 只要return的不是Z_STREAM_ERROR都行
}

/* ========================================================================= */
int ZEXPORT j_deflateEnd(strm)
    z_streamp strm;
{
    int status;

    if (deflateStateCheck(strm)) return Z_STREAM_ERROR;

    status = strm->state->status;

    ZFREE(strm, strm->state);
    strm->state = Z_NULL;

    return status == BUSY_STATE ? Z_DATA_ERROR : Z_OK;
}
/* ===========================================================================
 * Read a new buffer from the current input stream, update the adler32
 * and total number of bytes read.  All deflate() input goes through
 * this function so some applications may wish to modify it to avoid
 * allocating a large strm->next_in buffer and copying from it.
 * (See also flush_pending()).
 */
local unsigned read_buf(strm, buf, size)
    z_streamp strm;
    Bytef *buf;
    unsigned size;
{
    unsigned len = strm->avail_in;

    if (len > size) len = size;
    if (len == 0) return 0;

    strm->avail_in  -= len;

    zmemcpy(buf, strm->next_in, len);
    if (strm->state->wrap == 1) {
        strm->adler = adler32(strm->adler, buf, len);
    }
#ifdef GZIP
    else if (strm->state->wrap == 2) {
        strm->adler = crc32(strm->adler, buf, len);
    }
#endif
    strm->next_in  += len;
    strm->total_in += len;

    return len;
}
/* ===========================================================================
 * Flush the current block, with given end-of-file flag.
 * IN assertion: strstart is set to the end of the current match.
 */
#define FLUSH_BLOCK_ONLY(s, last) { \
   _tr_flush_block(s, (s->block_start >= 0L ? \
                   (charf *)&s->window[(unsigned)s->block_start] : \
                   (charf *)Z_NULL), \
                (ulg)((long)s->strstart - s->block_start), \
                (last)); \
   s->block_start = s->strstart; \
   flush_pending(s->strm); \
   Tracev((stderr,"[FLUSH]")); \
}
/* Same but force premature exit if necessary. */
#define FLUSH_BLOCK(s, last) { \
   FLUSH_BLOCK_ONLY(s, last); \
   if (s->strm->avail_out == 0) return (last) ? finish_started : need_more; \
}

/* Maximum stored block length in deflate format (not including header). */
#define MAX_STORED 65535

/* Minimum of a and b. */
#define MIN(a, b) ((a) > (b) ? (b) : (a))
int ZLIB_INTERNAL inflate_table_my(type, lens, codes, table, bits, work, tree)
code_type type;
unsigned short FAR *lens;
unsigned codes;
code_ FAR * FAR *table;
unsigned FAR *bits;
unsigned short FAR *work;
ct_data *tree;
{
#define MAXBITS 15
#define ENOUGH_LENS 852
#define ENOUGH_DISTS 592
    unsigned len;               /* a code's length in bits */
    unsigned sym;               /* index of code symbols */
    unsigned min, max;          /* minimum and maximum code lengths */
    unsigned root;              /* number of index bits for root table */
    unsigned curr;              /* number of index bits for current table */
    unsigned drop;              /* code bits to drop for sub-table */
    int left;                   /* number of prefix codes available */
    unsigned used;              /* code entries in table used */
    unsigned huff;              /* Huffman code */
    unsigned incr;              /* for incrementing code, index */
    unsigned fill;              /* index for replicating entries */
    unsigned low;               /* low bits for current root entry */
    unsigned mask;              /* mask for low root bits */
    code_ here;                  /* table entry for duplication */
    code_ FAR *next;             /* next available space in table */
    const unsigned short FAR *base;     /* base value table to use */
    const unsigned short FAR *extra;    /* extra bits table to use */
    unsigned match;             /* use base and extra for symbol >= match */
    unsigned short count[MAXBITS+1];    /* number of codes of each length */
    unsigned short offs[MAXBITS+1];     /* offsets in table for each length */
    static const unsigned short lbase[31] = { /* Length codes 257..285 base ：length的开始区间*/
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
    static const unsigned short lext[31] = { /* Length codes 257..285 extra ：16 + extra bits数*/
        16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18,
        19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 16, 194, 65};
    static const unsigned short dbase[32] = { /* Distance codes 0..29 base ：distance的开始区间*/
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577, 0, 0};
    static const unsigned short dext[32] = { /* Distance codes 0..29 extra ：16 + extra bits数*/
        16, 16, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
        23, 23, 24, 24, 25, 25, 26, 26, 27, 27,
        28, 28, 29, 29, 64, 64};

    /* accumulate lengths for codes (assumes lens[] all in 0..MAXBITS) */
    for (len = 0; len <= MAXBITS; len++)
        count[len] = 0; // 对bit长度数组清零
    for (sym = 0; sym < codes; sym++)
        count[lens[sym]]++; // 记录不同长度的码字的数量

    /* bound code lengths, force root to be within code lengths */
    root = *bits; // *bits 为huffman Entry索引比特
    for (max = MAXBITS; max >= 1; max--) // count跳过在末尾0计数的序列长度
        if (count[max] != 0) break; // 若root大于最长的序列长度，取最长序列长度
    if (root > max) root = max;
    if (max == 0) {                     /* no symbols to code at all */
        here.op = (unsigned char)64;    /* invalid code marker */
        here.bits = (unsigned char)1;
        here.val = (unsigned short)0;
        *(*table)++ = here;             /* make a table to force an error */
        *(*table)++ = here;
        *bits = 1;
        return 0;     /* no symbols, but wait for decoding to report error */
    }
    for (min = 1; min < max; min++) // count跳过在开头0计数的序列长度
        if (count[min] != 0) break; 
    if (root < min) root = min; // 若root小于最小的序列长度，取最小序列长度

    /* check for an over-subscribed or incomplete set of lengths */
    left = 1;
    for (len = 1; len <= MAXBITS; len++) { // 检查序列长度的个数是否正常
        left <<= 1;
        left -= count[len];
        if (left < 0) return -1;        /* over-subscribed */
    }
    if (left > 0 && (type == CODES || max != 1))
        return -1;                      /* incomplete set */

    /* generate offsets into symbol table for each length for sorting */
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) // 每个序列长度在 总体 里面的排名
        offs[len + 1] = offs[len] + count[len];

    /* sort symbols by length, by symbol order within each length */
    for (sym = 0; sym < codes; sym++) // 每个符号按长度进行排序，同一长度按符号顺序进行排序
        if (lens[sym] != 0) work[offs[lens[sym]]++] = (unsigned short)sym; // work索引表示排名，值表示符号

    /* set up for code type */
    switch (type) {
        case CODES:
            base = extra = work;    /* dummy value--not used 没有额外比特，直接赋值*/
            match = 20; // code小于20都是 literal
            Tracev((stderr,"type: CODES\n"));
            break;
        case LENS:
            base = lbase;
            extra = lext;
            match = 257; // code小于257都是literal， 大于是 length
            Tracev((stderr,"type: LENS\n"));
            break;
        default:    /* DISTS */
            base = dbase;
            extra = dext;
            match = 0;
            Tracev((stderr,"type: DISTS\n"));
    }

    /* initialize state for loop */
    huff = 0;                   /* starting code */
    sym = 0;                    /* starting code symbol */
    len = min;                  /* starting code length */
    next = *table;              /* current table to fill in */
    curr = root;                /* current table index bits */
    drop = 0;                   /* current bits to drop from code for index */
    low = (unsigned)(-1);       /* trigger new sub-table when len > root */
    used = 1U << root;          /* use root table entries */
    mask = used - 1;            /* mask for comparing low 用来比较低位，sub table? */

    /* check available table space */
    if ((type == LENS && used > ENOUGH_LENS) ||
        (type == DISTS && used > ENOUGH_DISTS))
        return 1;

    /* process all codes and make table entries */
    for (;;) { // 此时sym表示排名，排名从小到大遍历，即huf编码长度从小到大遍历，work[sym]表示该排名下的符号
        /* create table entry 从排名第一的开始，即从编码最短的开始。构造huffman table。*/
        // here.bits获得code len，即ctdata.len；huff获得huffman编码，即ctdata.code；
        // here.val作为ctdata索引；here.op作为指令类型
        if(work[sym]=='E'){
            int a = 0;
        }
        here.bits = (unsigned char)(len - drop);
        if (work[sym] + 1U < match) { // val小于match时，只可能是literal
            here.op = (unsigned char)0; // literal
            here.val = work[sym]; // literal值
            Tracevv((stderr,"literal 0x%02x %u(%u)\n", here.val, huff, len));
        }
        else if (work[sym] >= match) { // val大于match时，采用extra bits,（即length和distance）
            here.op = (unsigned char)(extra[work[sym] - match]); // op使用extra数组内的数表示
            here.val = base[work[sym] - match]; // val只保存base
            Tracev((stderr,"dist/len %u %u(%u)\n", work[sym], huff, len));
            if(work[sym]==3 && huff == 511 && len==9){
                int a = 0;
            }
        }
        else { // work[sym] = match -1
            here.op = (unsigned char)(32 + 64);         /* end of block */
            here.val = 0;
            Tracev((stderr,"end of block %u %u(%u)\n", work[sym], huff, len));
        }
        /* replicate for those indices with low len bits equal to huff */
        incr = 1U << (len - drop); // 此时的incr为高len位的数值，低位有效，高位无效。较长的bit编码会覆盖较短的bit编码
        fill = 1U << curr;
        min = fill;                 /* save offset to next table */
        do {
            fill -= incr; // 每间隔incr个，进行一次赋值？
            next[(huff >> drop) + fill] = here; 
        } while (fill != 0);
        /* 在这里为defalte建立huffman表 */
        tree[work[sym]].Len = len; // work[sym]表示符号
        tree[work[sym]].Code = huff;
        /* backwards increment the len-bit code huff 获得倒置的huffman编码 */
        incr = 1U << (len - 1); // 此时incr为mask？
        while (huff & incr)
            incr >>= 1;
        if (incr != 0) {
            huff &= incr - 1; // incr - 1 作为掩码
            huff += incr;
        }
        else
            huff = 0;

        /* go to next symbol, update count, len */
        sym++;
        if (--(count[len]) == 0) {
            if (len == max) break; // 结束建立Huffman table
            len = lens[work[sym]]; // 进行到下一个序列长度
        }

        /* create new sub-table if needed 建立新表，当序列长度大于root时；比较Low是为什么？*/
        if (len > root && (huff & mask) != low) {
            /* if first time, transition to sub-tables */
            if (drop == 0)
                drop = root;

            /* increment past last table */
            next += min;            /* here min is 1 << curr，跳过第一个huffman table */

            /* determine length of next table ，下一个表索引的比特长度*/
            curr = len - drop; // 新表的比特索引长度
            left = (int)(1 << curr);
            while (curr + drop < max) { // 将curr拓展到涵盖max，并检查序列是否正确
                left -= count[curr + drop];
                if (left <= 0) break;
                curr++;
                left <<= 1;
            }

            /* check for enough space */
            used += 1U << curr; // used增加curr比特个大小
            if ((type == LENS && used > ENOUGH_LENS) ||
                (type == DISTS && used > ENOUGH_DISTS))
                return 1;

            /* point entry in root table to sub-table 生成table transfer指令，该指令指向第二级huffman table中的指令 */
            low = huff & mask; // 在第一级huffman table的索引，生
            (*table)[low].op = (unsigned char)curr; // 新表的索引长度
            (*table)[low].bits = (unsigned char)root; // bit index长度
            (*table)[low].val = (unsigned short)(next - *table); // table的偏移
        }
    }

    /* fill in remaining table entry if code is incomplete (guaranteed to have
       at most one remaining entry, since if the code is incomplete, the
       maximum code length that was allowed to get this far is one bit) */
    if (huff != 0) {
        here.op = (unsigned char)64;            /* invalid code marker */
        here.bits = (unsigned char)(len - drop);
        here.val = (unsigned short)0;
        next[huff] = here;
    }

    /* set return parameters */
    *table += used;
    *bits = root;
    return 0;
}
unsigned char * de_ins_buf[6];
long de_ins_buf_index = 0;
long de_ins_buf_have = 0;
long de_ins_type = 0;
unsigned char * insbuf[6];
int ZEXPORT deflate_my(strm, flush)
z_streamp strm;
int flush;
{
    struct internal_state FAR *state;
    z_const unsigned char FAR *next;    /* next input */
    //unsigned char FAR *put;     /* next output */
    unsigned have;
    //unsigned left;        /* available input and output，剩余的avail_in和avail_out*/
    unsigned long hold;         /* bit buffer */
    unsigned bits;              /* bits in bit buffer */
    unsigned in, out;           /* save starting available input and output，保存起始的avail_in和avail_out*/
    unsigned copy;              /* number of stored or match bytes to copy */
    unsigned char FAR *from;    /* where to copy match bytes from */
    code_ here; /* current decoding table entry */             
    unsigned len;               /* length to copy for repeats, bits to drop */
    int ret;                    /* return code */
    static const unsigned short order[19] = /* permutation of code lengths，huffman序列的调整顺序 */
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int bflush;              /* set if current block must be flushed */
    #define LOAD() do { /*put = strm->next_out; left = strm->avail_out; */\
     next = strm->next_in; have = strm->avail_in; hold = state->hold;  bits = state->bits; } while (0)
    #define RESTORE() do { /*strm->next_out = put; strm->avail_out = left;*/ \
    strm->next_in = next; strm->avail_in = have; state->hold = hold; state->bits = bits; } while (0)
    #define INITBITS() do { hold = 0; bits = 0; } while (0)
    #define PULLBYTE() do { if (have == 0) goto inf_leave; have--; hold += (unsigned long)(*next++) << bits; bits += 8;} while (0)
    #define NEEDBITS(n) do { while (bits < (unsigned)(n)) PULLBYTE(); } while (0)
    #define BITS(n) ((unsigned)hold & ((1U << (n)) - 1))
    #define DROPBITS(n) do { hold >>= (n); bits -= (unsigned)(n); } while (0)
    #define BYTEBITS() do { hold >>= bits & 7; bits -= bits & 7; } while (0)
    #define NEEDBYTES(n) do { } while (0)
    #define DROPBYTES(n) do { } while (0)
    if (strm->next_out == Z_NULL ||
        (strm->next_in == Z_NULL && strm->avail_in != 0))
        return Z_STREAM_ERROR;

    state = (struct internal_state FAR *)strm->state;
    if (state->mode == TYPE) state->mode = TYPEDO;      /* skip check */

    /* 把本地指针指向压缩流，即把状态装载入函数本地寄存器：
     * put = strm->next_out; left = strm->avail_out; 
     * next = strm->next_in; have = strm->avail_in; 
     * hold = state->hold; bits = state->bits; */
    LOAD(); 
    in = have; // in = have = strm->avail_in
    //out = left; // out = left = strm->avail_out
    ret = Z_OK;
    for (;;)
        switch (state->mode) {
        case HEAD:
            if (state->wrap == 0) { // 没有zip头部，直接逃到TYPEDO
                state->mode = TYPEDO;
                break;
            }
            NEEDBITS(8);
            de_ins_buf[0] = BITS(8);
            DROPBITS(8);

            NEEDBITS(8);
            de_ins_buf[1] = BITS(8);
            DROPBITS(8);
            long de_ins_buf_index = 0;
            long de_ins_buf_have = 2;
            long de_ins_type = 0;
            state->mode = SENDINS;
            break;
                /* fallthrough */
        case TYPE:
            if (flush == Z_BLOCK || flush == Z_TREES) goto inf_leave;
                /* fallthrough */
        case TYPEDO: // 开始处理deflate流
            /* huffman树初始化*/
            memset(&state->bl_tree, 0, (2 * BL_CODES + 1) * sizeof(struct ct_data_s));
            memset(&state->dyn_ltree, 0, (HEAP_SIZE) * sizeof(struct ct_data_s));
            memset(&state->dyn_dtree, 0, (2 * D_CODES + 1) * sizeof(struct ct_data_s));
            /* 开始处理deflate流 */
            if (state->last) { // 最后一块处理完毕
                BYTEBITS();
                state->mode = DONE; 
                break;
            }
            NEEDBITS(3);
            state->last = BITS(1); // 是否使最后一个数据块
            DROPBITS(1); // 使用完舍弃1bit
            switch (BITS(2)) {
                case 0:                             /* stored block 没使用huffman树*/
                    Tracev((stderr, "inflate:     stored block%s\n",
                            state->last ? " (last)" : ""));
                    state->ttype = STORE;
                    state->mode = STORED;
                    break;
                case 1:                             /* fixed block 使用固定huffman树*/
                    //fixedtables(state);
                    Tracev((stderr, "inflate:     fixed codes block%s\n",
                            state->last ? " (last)" : ""));
                    state->mode = SKPI_HEADER_END; // 直接跳转到TABLE，获得huffman编码
                    state->ttype = FIXED;
                    if (flush == Z_TREES) {
                        DROPBITS(2);
                        goto inf_leave;
                    }
                    break;
                case 2:                             /* dynamic block 使用动态huffman树*/
                    Tracev((stderr, "inflate:     dynamic codes block%s\n",
                            state->last ? " (last)" : ""));
                    state->mode = TABLE; // 直接跳转到TABLE，获得huffman编码
                    state->ttype = DYNAMIC;
                    break;
                case 3:
                    strm->msg = (char *)"invalid block type";
                    state->mode = BAD;
            }
            DROPBITS(2); // 舍弃2bit
            break;
        case STORED: // store方式保存
            BYTEBITS();                         /* go to byte boundary 舍弃该字节内的剩余bit*/
            NEEDBITS(32);
            if ((hold & 0xffff) != ((hold >> 16) ^ 0xffff)) {
                strm->msg = (char *)"invalid stored block lengths";
                state->mode = BAD;
                break;
            }
            state->length = (unsigned)hold & 0xffff;
            Tracev((stderr, "inflate:       stored length %u\n",
                    state->length));
            INITBITS(); // 清空32bit
            state->mode = SKPI_HEADER_END;
            break;
                /* fallthrough */
        case COPY_:
            state->mode = COPY;
                /* fallthrough */
        case COPY: // store方式保存，直接copy
            copy = state->length;
            if (copy) { // 把字节COPY到sym_buf
                if (copy > have) copy = have;
                /* if (copy > left) copy = left; 默认sym_buf空间足够 */
                if (copy == 0) goto inf_leave;
                zmemcpy(state->sym_buf + state->sym_next, next, copy);
                have -= copy;
                next += copy;
                state->sym_next += copy;
                state->length -= copy;
                break;
            }
            Tracev((stderr, "inflate:       stored end\n"));
            NEEDBITS(16); // 跳过2个字节, (0xaa, 0xff)
            DROPBITS(16); // 
            state->mode = ENDOFBLOCK;
            break;
        case TABLE: // 解析完头部3bits，直接跳转到这，获得huffman序列长度
            NEEDBITS(14);
            // HLIT：5比特，记录literal/length码树中码长序列（CL1）个数的一个变量。后面CL1个数等于HLIT+257（因为至少有0-255总共256个literal，还有一个256表示解码结束，但length的个数不定。
            state->nlen = BITS(5) + 257; 
            state->l_desc.max_code = state->nlen;
            DROPBITS(5);
            // HDIST：5比特，记录distance码树中码长序列（CL2）个数的一个变量。后面CL2个数等于HDIST+1。哪怕没有1个重复字符串，distance都为0也是一个CL。
            state->ndist = BITS(5) + 1; 
            state->d_desc.max_code = state->ndist;
            DROPBITS(5);
            // HCLEN：4比特，记录Huffman码表3中码长序列（CCL）个数的一个变量。后面CCL个数等于HCLEN+4。PK认为CCL个数不会低于4个，即使对于整个文件只有1个字符的情况。
            // 游程编码：CL1和CL2树的深度不会超过15，因此，CL1和CL2这两个序列的任意整数值的范围是0-15。0-15是CL可能出现的值，16表示除了0以外的其它游程；17、18表示0游程
            // CCL对CL1和CL2游程编码后的码字（0-18）进行huffman编码，得到SQ1, SQ2
            state->ncode = BITS(4) + 4; 
            DROPBITS(4);
            Tracev((stderr, "inflate:       table sizes ok\n"));
            state->have = 0;
            state->mode = LENLENS;
                /* fallthrough */
        case LENLENS: // 获得CLL中的huffman编码
            while (state->have < state->ncode) {
                NEEDBITS(3); // CCL序列3bit定长编码，一共HCLEN+4个
                state->lens[order[state->have++]] = (unsigned short)BITS(3); // 获得huffman树序列，通过order置换顺序
                DROPBITS(3);
            }
            while (state->have < 19) // CCL标准长度19，如果没达到，需要在末尾补0
                state->lens[order[state->have++]] = 0;
            state->next = state->codes;
            state->lencode = (const code_ FAR *)(state->next); // 此时lencode作为CCL, state->next指向state->codes
            state->lenbits = 7; // 索引比特
            ret = inflate_table_my(CODES, state->lens, 19, &(state->next),
                                &(state->lenbits), state->work, state->bl_tree); // 获得huffman编码
            if (ret) {
                strm->msg = (char *)"invalid code lengths set";
                state->mode = BAD;
                break;
            }
            Tracev((stderr, "inflate:       code lengths ok\n"));
            state->have = 0;
            state->mode = CODELENS;
                /* fallthrough */
        case CODELENS: // 获得CL1，CL2中的huffman编码
            while (state->have < state->nlen + state->ndist) { // state->nlen CL1大小， state->ndist CL2大小，这里获得CL1和CL2的huffman序列
                for (;;) { // lencode保存huffman entry，索引为比特流中的每state->lenbits个比特的值
                    here = state->lencode[BITS(state->lenbits)]; // 取hold的lenbits位作为huffman entry的索引
                    if ((unsigned)(here.bits) <= bits) break; // 读取了足够的bits
                    PULLBYTE();
                } // 不考虑op，只判断val，进行游程解码
                if (here.val < 16) { // val<16，直接add
                    DROPBITS(here.bits);
                    state->lens[state->have++] = here.val; // lens此时为cl1,cl2的huffman序列
                }
                else {
                    if (here.val == 16) { // val=16，紧跟2bits表示重复长度
                        NEEDBITS(here.bits + 2);
                        DROPBITS(here.bits);
                        if (state->have == 0) {
                            strm->msg = (char *)"invalid bit length repeat";
                            state->mode = BAD;
                            break;
                        }
                        len = state->lens[state->have - 1];
                        copy = 3 + BITS(2); // 复制3+bits个
                        DROPBITS(2);
                    }
                    else if (here.val == 17) { // 重复的0，紧跟3bits表示长度
                        NEEDBITS(here.bits + 3);
                        DROPBITS(here.bits);
                        len = 0;
                        copy = 3 + BITS(3); // 复制3+bits个
                        DROPBITS(3);
                    }
                    else { // val=18, 重复的0，紧跟7bits表示长度
                        NEEDBITS(here.bits + 7);
                        DROPBITS(here.bits);
                        len = 0;
                        copy = 11 + BITS(7); // 复制11+bits个
                        DROPBITS(7);
                    }
                    if (state->have + copy > state->nlen + state->ndist) {
                        strm->msg = (char *)"invalid bit length repeat";
                        state->mode = BAD;
                        break;
                    }
                    while (copy--) // 进行复制
                        state->lens[state->have++] = (unsigned short)len;
                }
            }
            // cl1, cl2的huffman序列生成完成，结果保存在state->lens，长度为state->have
            /* handle error breaks in while */
            if (state->mode == BAD) break;

            /* check for end-of-block code (better have one) */
            if (state->lens[256] == 0) {
                strm->msg = (char *)"invalid code -- missing end-of-block";
                state->mode = BAD;
                break;
            }

            /* build code tables -- note: do not change the lenbits or distbits
               values here (9 and 6) without reading the comments in inftrees.h
               concerning the ENOUGH constants, which depend on those values */
            state->next = state->codes; // 已经使用完ccl，直接生成cl1的huffman entry，并覆盖state->codes
            state->lencode = (const code_ FAR *)(state->next); // 此时lencode作为CL1，state->next指向state->codes
            state->lenbits = 9; // cl1的huffman entry索引比特数为9？
            // inflate_table(huffman_type, huffman_sequence, symbol_length, huffman_table, index_bits, work_table)
            ret = inflate_table_my(LENS, state->lens, state->nlen, &(state->next), // 建立CL1 huffman表
                                &(state->lenbits), state->work, state->dyn_ltree);
            if (ret) {
                strm->msg = (char *)"invalid literal/lengths set";
                state->mode = BAD;
                break;
            }
            state->distcode = (const code_ FAR *)(state->next); // distcode指向state->next，紧跟在lencode之后
            state->distbits = 6; // cl2的huffman entry索引比特数为6？
            ret = inflate_table_my(DISTS, state->lens + state->nlen, state->ndist, // 建立CL2 huffman表
                            &(state->next), &(state->distbits), state->work, state->dyn_dtree);
            if (ret) { 
                strm->msg = (char *)"invalid distances set";
                state->mode = BAD;
                break;
            }
            Tracev((stderr, "inflate:       codes ok\n"));
            /* HEADER 处理结束 */
            state->mode = SKPI_HEADER_END;
            if (flush == Z_TREES) goto inf_leave;
            break; // 我加的break
        case SKPI_HEADER_END:
            BYTEBITS(); // 舍弃最后一个字节未使用的bit
            NEEDBITS(16); // 跳过2个字节(0xaa, 0xff)
            DROPBITS(16); // 
            if(state->ttype == STORE) state->mode = COPY_;
            else state->mode = LEN_;
            break;
        case LEN_:
            INITBITS();
            state->mode = LEN;
                /* fallthrough 接下来进行解压缩*/
        case LEN: /* 指令解析 */
            NEEDBITS(8); // 读取1个字节的指令
            insbuf[0] = BITS(8);
            DROPBITS(8);
            if(insbuf[0] != 0xaa){
                state->length = insbuf[0];
                Tracevv((stderr, state->length >= 0x20 && state->length < 0x7f ?
                        "inflate:         literal '%c'\n" :
                        "inflate:         literal 0x%02x\n", state->length));
                state->mode = LIT; // mode变为LIT，转到literal输出代码块
                break;
            }
            state->mode = INS1;
        case INS1:
            NEEDBITS(8); // 读取1个字节的指令
            insbuf[1] = BITS(8);
            DROPBITS(8);
            if(insbuf[1] == 0xaa){ // <literal 0xaa>
                state->length = 0xaa;
                Tracevv((stderr, state->length >= 0x20 && state->length < 0x7f ?
                        "inflate:         literal '%c'\n" :
                        "inflate:         literal 0x%02x\n", state->length));
                state->mode = LIT; // mode变为LIT，转到literal输出代码块
                break;
            }
            else if(insbuf[1] == 0xff){ // <end of block>
                Tracevv((stderr, "inflate:         end of block\n"));
                state->mode = ENDOFBLOCK; // 跳转到TYPEDO，重新进行一块deflate块解压
                break;
            }
            else if(insbuf[1] > 0x80){ // 无效指令
                strm->msg = (char *)"invalid literal/length code";
                state->mode = BAD;
                break;
            }
            state->mode = INS2;
        case INS2:
            NEEDBITS(16); // 读取1个字节的指令
            insbuf[2] = BITS(8);
            DROPBITS(8);
            insbuf[3] = BITS(8);
            DROPBITS(8);
            // <dist >> 8, dist, length>
            state->mode = MATCH; // mode变为LENEXT，转到LENEXT代码块，获得extra bits
            state->length = insbuf[3];
            state->offset = insbuf[2] + ((int)insbuf[1] << 8);
            Tracevv((stderr, "inflate:         length %u\n", state->length+3));
            Tracevv((stderr, "inflate:         distance %u\n", state->offset));
                /* fallthrough */
        case MATCH: // 输出<length, distance>指令结果
            /* 此处生成 sym_buf 指令 */
            _tr_tally_dist_my(state, state->offset, state->length, bflush);
            if(bflush){ Tracevv((stderr, "inflate:         sym_buf full! exit\n")); return 1; }
            state->mode = LEN;
            break;
        case LIT: // 输出literal指令结果
            /* 此处生成 sym_buf 指令 */
            _tr_tally_lit_my(state, state->length, bflush);
            if(bflush){ Tracevv((stderr, "inflate:         sym_buf full! exit\n")); return 1; }
            state->mode = LEN;
            break;
        case DONE:
            if (state->wrap == 0) { // 没有zip头部，直接逃到TYPEDO
                ret = Z_STREAM_END;
                goto inf_leave;
            }
            NEEDBITS(32);
            //out -= left;
            //strm->total_out += out;

            //out = left;
            de_ins_buf_index = 0;
            de_ins_buf_have = 4;
            de_ins_type = 1;
            de_ins_buf[0] = (unsigned char)BITS(8);
            DROPBITS(8);
            de_ins_buf[1] = (unsigned char)BITS(8);
            DROPBITS(8);
            de_ins_buf[2] = (unsigned char)BITS(8);
            DROPBITS(8);
            de_ins_buf[3] = (unsigned char)BITS(8);
            DROPBITS(8);
            INITBITS();
            state->mode = SENDINS;
        case SENDINS: // 发送指令
            if (strm->avail_out == 0) goto inf_leave;
            if(de_ins_buf_have - de_ins_buf_index > strm->avail_out) copy = strm->avail_out;
            else copy = de_ins_buf_have - de_ins_buf_index;
            strm->avail_out -= copy;
            while(copy--) *strm->next_out++ = de_ins_buf[de_ins_buf_index++];
            if (de_ins_buf_index == de_ins_buf_have) {
                if(de_ins_type == 0) state->mode = TYPE; // zlib header
                else if(de_ins_type == 1) {
                    ret = Z_STREAM_END;
                    goto inf_leave;
                }
            }
            break;
        case BAD:
            ret = Z_DATA_ERROR;
            goto inf_leave;
        case MEM:
            return Z_MEM_ERROR;
        case ENDOFBLOCK: /* 此处输出end of block指令 */
            /* 此处生成sym_buf指令 */
            state->mode = TYPE;
            _tr_flush_block_my(state, (charf *)state->sym_buf, state->sym_next, state->ttype, state->last);
            flush_pending(state->strm);
            Tracev((stderr,"[FLUSH]"));
            if (strm->avail_out == 0) goto inf_leave;; 
            break;
        default:
            return Z_STREAM_ERROR;
        }
  inf_leave:
    RESTORE(); // 恢复状态
    in -= strm->avail_in; // 本次函数使用的大小
    out -= strm->avail_out; // 本次函数输出的大小
    strm->total_in += in;
    strm->total_out += out;
    if (((in == 0 && out == 0) || flush == Z_FINISH) && ret == Z_OK)
        ret = Z_BUF_ERROR;
    return ret;
}