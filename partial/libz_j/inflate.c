#include "zutil.h"
#include "inftrees.h"
#include "inflate.h"
#include "inffast.h"
#include <stdio.h>
#include "inffixed.h"

/* function prototypes */
local int inflateStateCheck OF((z_streamp strm));
local void fixedtables OF((struct inflate_state FAR *state));

local int inflateStateCheck(strm)
z_streamp strm;
{
    struct inflate_state FAR *state;
    if (strm == Z_NULL ||
        strm->zalloc == (alloc_func)0 || strm->zfree == (free_func)0)
        return 1;
    state = (struct inflate_state FAR *)strm->state;
    if (state == Z_NULL || state->strm != strm ||
        state->mode < HEAD || state->mode > SYNC)
        return 1;
    return 0;
}

int ZEXPORT j_inflateResetKeep(strm)
z_streamp strm;
{
    struct inflate_state FAR *state;

    if (inflateStateCheck(strm)) return Z_STREAM_ERROR;
    state = (struct inflate_state FAR *)strm->state;
    strm->total_in = strm->total_out = state->total = 0;
    strm->msg = Z_NULL;
    if (state->wrap)        /* to support ill-conceived Java test suite */
        strm->adler = state->wrap & 1;
    state->mode = HEAD;
    state->last = 0;
    state->havedict = 0;
    state->flags = -1;
    state->dmax = 32768U;
    state->head = Z_NULL;
    state->hold = 0;
    state->bits = 0;
    state->lencode = state->distcode = state->next = state->codes;
    state->sane = 1;
    state->back = -1;
    Tracev((stderr, "inflate: reset\n"));
    return Z_OK;
}

int ZEXPORT j_inflateReset(strm)
z_streamp strm;
{
    struct inflate_state FAR *state;

    if (inflateStateCheck(strm)) return Z_STREAM_ERROR;
    state = (struct inflate_state FAR *)strm->state;
    state->wsize = 0;
    state->whave = 0;
    state->wnext = 0;
    return j_inflateResetKeep(strm);
}

int ZEXPORT j_inflateReset2(strm, windowBits)
z_streamp strm;
int windowBits;
{
    int wrap;
    struct inflate_state FAR *state;

    /* get the state */
    if (inflateStateCheck(strm)) return Z_STREAM_ERROR;
    state = (struct inflate_state FAR *)strm->state;

    /* extract wrap request from windowBits parameter */
    if (windowBits < 0) {
        if (windowBits < -15)
            return Z_STREAM_ERROR;
        wrap = 0;
        windowBits = -windowBits;
    }
    else {
        wrap = (windowBits >> 4) + 5;
#ifdef GUNZIP
        if (windowBits < 48)
            windowBits &= 15;
#endif
    }

    /* set number of window bits, free window if different */
    if (windowBits && (windowBits < 8 || windowBits > 15))
        return Z_STREAM_ERROR;
    if (state->window != Z_NULL && state->wbits != (unsigned)windowBits) {
        ZFREE(strm, state->window);
        state->window = Z_NULL;
    }

    /* update state and reset the rest of it */
    state->wrap = wrap;
    state->wbits = (unsigned)windowBits;
    return j_inflateReset(strm);
}

int ZEXPORT j_inflateInit2_(strm, windowBits, version, stream_size)
z_streamp strm;
int windowBits;
const char *version;
int stream_size;
{
    int ret;
    struct inflate_state FAR *state;
    if (strm == Z_NULL) return Z_STREAM_ERROR;
    strm->msg = Z_NULL;                 /* in case we return an error */
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
    state = (struct inflate_state FAR *)
            ZALLOC(strm, 1, sizeof(struct inflate_state));
    if (state == Z_NULL) return Z_MEM_ERROR;
    Tracev((stderr, "inflate: allocated\n"));
    strm->state = (struct internal_state FAR *)state;
    state->strm = strm;
    state->window = Z_NULL;
    state->mode = HEAD;     /* to pass state test in inflateReset2() */
    ret = j_inflateReset2(strm, windowBits);
    state->header_flag = 1;
    if (ret != Z_OK) {
        ZFREE(strm, state);
        strm->state = Z_NULL;
    }
    return ret;
}

int ZEXPORT j_inflateInit_(strm, version, stream_size)
z_streamp strm;
const char *version;
int stream_size;
{
    return j_inflateInit2_(strm, DEF_WBITS, version, stream_size);
}
/*
   Return state with length and distance decoding tables and index sizes set to
   fixed code decoding.  Normally this returns fixed tables from inffixed.h.
   If BUILDFIXED is defined, then instead this routine builds the tables the
   first time it's called, and returns those tables the first time and
   thereafter.  This reduces the size of the code by about 2K bytes, in
   exchange for a little execution time.  However, BUILDFIXED should not be
   used for threaded applications, since the rewriting of the tables and virgin
   may not be thread-safe.
 */
local void fixedtables(state)
struct inflate_state FAR *state;
{
    state->lencode = lenfix;
    state->lenbits = 9;
    state->distcode = distfix;
    state->distbits = 5;
}

/* Macros for inflate(): */

/* check function to use adler32() for zlib or crc32() for gzip */
#ifdef GUNZIP
#  define UPDATE_CHECK(check, buf, len) \
    (state->flags ? crc32(check, buf, len) : adler32(check, buf, len))
#else
#  define UPDATE_CHECK(check, buf, len) adler32(check, buf, len)
#endif

/* check macros for header crc */
#ifdef GUNZIP
#  define CRC2(check, word) \
    do { \
        hbuf[0] = (unsigned char)(word); \
        hbuf[1] = (unsigned char)((word) >> 8); \
        check = crc32(check, hbuf, 2); \
    } while (0)

#  define CRC4(check, word) \
    do { \
        hbuf[0] = (unsigned char)(word); \
        hbuf[1] = (unsigned char)((word) >> 8); \
        hbuf[2] = (unsigned char)((word) >> 16); \
        hbuf[3] = (unsigned char)((word) >> 24); \
        check = crc32(check, hbuf, 4); \
    } while (0)
#endif

/* Load registers with state in inflate() for speed 把状态装载入寄存器，加速 */
#define LOAD() \
    do { \
        put = strm->next_out; \
        left = strm->avail_out; \
        next = strm->next_in; \
        have = strm->avail_in; \
        hold = state->hold; \
        bits = state->bits; \
    } while (0)

/* Restore state from registers in inflate() 从寄存器中回复状态 */
#define RESTORE() \
    do { \
        strm->next_out = put; \
        strm->avail_out = left; \
        strm->next_in = next; \
        strm->avail_in = have; \
        state->hold = hold; \
        state->bits = bits; \
    } while (0)

/* Clear the input bit accumulator */
#define INITBITS() \
    do { \
        hold = 0; \
        bits = 0; \
    } while (0)

/* Get a byte of input into the bit accumulator, or return from inflate()
   if there is no input available. */
#define PULLBYTE() \
    do { \
        if (have == 0) goto inf_leave; \
        have--; /* 相当于strm->avail_in-- */\
        hold += (unsigned long)(*next++) << bits; /* 相当于strm->next++ */\
        bits += 8; \
    } while (0)

/* Assure that there are at least n bits in the bit accumulator.  If there is
   not enough available input to do that, then return from inflate(). */
#define NEEDBITS(n) \
    do { \
        while (bits < (unsigned)(n)) \
            PULLBYTE(); \
    } while (0)

/* Return the low n bits of the bit accumulator (n < 16) */
#define BITS(n) \
    ((unsigned)hold & ((1U << (n)) - 1))
unsigned long long HEADER_BITS_CNT = 0;
/* Remove n bits from the bit accumulator */
#ifdef MY_ZLIB
/* 用来保存丢弃的Bits，即 huffman bits */
#define my_Buf_size 16
#define my_put_byte(s, c) {s->header_buf[s->header_have++] = (Bytef)(c);}
#define my_put_short(s, w) { \
    my_put_byte(s, (uch)((w) & 0xff)); \
    my_put_byte(s, (uch)((ush)(w) >> 8)); \
}
#define my_send_bits(s, value, length) \
{ \
    int len = length;\
    if (s->my_bi_valid > (int)my_Buf_size - len) {\
        int val = (int)value;\
        s->my_bi_buf |= (ush)val << s->my_bi_valid;\
        my_put_short(s, s->my_bi_buf);\
        s->my_bi_buf = (ush)val >> (my_Buf_size - s->my_bi_valid);\
        s->my_bi_valid += len - my_Buf_size;\
    } else {\
        s->my_bi_buf |= (ush)(value) << s->my_bi_valid;\
        s->my_bi_valid += len;\
    }\
}
#define DROP_HEADER_BITS(s,n) \
do { \
    if(s->header_flag) {\
        my_send_bits(s,BITS(n),n); \
        HEADER_BITS_CNT +=n; \
    } \
} while (0)
#else
#define DROP_HEADER_BITS(s,n)
#endif

#define DROPBITS(n) \
    do { \
        DROP_HEADER_BITS(state,n); \
        hold >>= (n); \
        bits -= (unsigned)(n); \
    } while (0)
/* Remove zero to seven bits as needed to go to a byte boundary */
#define BYTEBITS() \
    do { \
        /*DROP_HEADER_BITS(state, bits & 7);*/ \
        hold >>= bits & 7; \
        bits -= bits & 7; \
    } while (0)
#ifdef MY_ZLIB
void my_bi_windup(s)
struct inflate_state FAR *s;
{
    if (s->my_bi_valid > 8) {
        my_put_short(s, s->my_bi_buf);
    } else if (s->my_bi_valid > 0) {
        my_put_byte(s, (Byte)s->my_bi_buf);
    }
    s->my_bi_buf = 0;
    s->my_bi_valid = 0;
}
#endif
/*
   inflate() uses a state machine to process as much input data and generate as
   much output data as possible before returning.  The state machine is
   structured roughly as follows:

    for (;;) switch (state) {
    ...
    case STATEn:
        if (not enough input data or output space to make progress)
            return;
        ... make progress ...
        state = STATEm;
        break;
    ...
    }

   so when inflate() is called again, the same case is attempted again, and
   if the appropriate resources are provided, the machine proceeds to the
   next state.  The NEEDBITS() macro is usually the way the state evaluates
   whether it can proceed or should return.  NEEDBITS() does the return if
   the requested bits are not available.  The typical use of the BITS macros
   is:

        NEEDBITS(n);
        ... do something with BITS(n) ...
        DROPBITS(n);

   where NEEDBITS(n) either returns from inflate() if there isn't enough
   input left to load n bits into the accumulator, or it continues.  BITS(n)
   gives the low n bits in the accumulator.  When done, DROPBITS(n) drops
   the low n bits off the accumulator.  INITBITS() clears the accumulator
   and sets the number of available bits to zero.  BYTEBITS() discards just
   enough bits to put the accumulator on a byte boundary.  After BYTEBITS()
   and a NEEDBITS(8), then BITS(8) would return the next byte in the stream.

   NEEDBITS(n) uses PULLBYTE() to get an available byte of input, or to return
   if there is no input available.  The decoding of variable length codes uses
   PULLBYTE() directly in order to pull just enough bytes to decode the next
   code, and no more.

   Some states loop until they get enough input, making sure that enough
   state information is maintained to continue the loop where it left off
   if NEEDBITS() returns in the loop.  For example, want, need, and keep
   would all have to actually be part of the saved state in case NEEDBITS()
   returns:

    case STATEw:
        while (want < need) {
            NEEDBITS(n);
            keep[want++] = BITS(n);
            DROPBITS(n);
        }
        state = STATEx;
    case STATEx:

   As shown above, if the next state is also the next case, then the break
   is omitted.

   A state may also return if there is not enough output space available to
   complete that state.  Those states are copying stored data, writing a
   literal byte, and copying a matching string.

   When returning, a "goto inf_leave" is used to update the total counters,
   update the check value, and determine whether any progress has been made
   during that inflate() call in order to return the proper return code.
   Progress is defined as a change in either strm->avail_in or strm->avail_out.
   When there is a window, goto inf_leave will update the window with the last
   output written.  If a goto inf_leave occurs in the middle of decompression
   and there is no window currently, goto inf_leave will create one and copy
   output to the window for the next call of inflate().

   In this implementation, the flush parameter of inflate() only affects the
   return code (per zlib.h).  inflate() always writes as much as possible to
   strm->next_out, given the space available and the provided input--the effect
   documented in zlib.h of Z_SYNC_FLUSH.  Furthermore, inflate() always defers
   the allocation of and copying into a sliding window until necessary, which
   provides the effect documented in zlib.h for Z_FINISH when the entire input
   stream available.  So the only thing the flush parameter actually does is:
   when flush is set to Z_FINISH, inflate() cannot return Z_OK.  Instead it
   will return Z_BUF_ERROR if it has not reached the end of the stream.
 */
unsigned char * ins_buffer[6];
long ins_buffer_index = 0;
long ins_buffer_have = 0;
long ins_type = 0;
int ZEXPORT j_inflate(strm, flush)
z_streamp strm;
int flush;
{
    struct inflate_state FAR *state;
    z_const unsigned char FAR *next;    /* next input */
    unsigned char FAR *put;     /* next output */
    unsigned have, left;        /* available input and output，剩余的avail_in和avail_out*/
    unsigned long hold;         /* bit buffer */
    unsigned bits;              /* bits in bit buffer */
    unsigned in, out;           /* save starting available input and output，保存起始的avail_in和avail_out*/
    unsigned copy;              /* number of stored or match bytes to copy */
    unsigned char FAR *from;    /* where to copy match bytes from */
    code here;                  /* current decoding table entry */
    code last;                  /* parent table entry */
    unsigned len;               /* length to copy for repeats, bits to drop */
    int ret;                    /* return code */
#ifdef GUNZIP
    unsigned char hbuf[4];      /* buffer for gzip header crc calculation */
#endif
    static const unsigned short order[19] = /* permutation of code lengths，huffman序列的调整顺序 */
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    if (inflateStateCheck(strm) || strm->next_out == Z_NULL ||
        (strm->next_in == Z_NULL && strm->avail_in != 0))
        return Z_STREAM_ERROR;

    state = (struct inflate_state FAR *)strm->state;
    if (state->mode == TYPE) state->mode = TYPEDO;      /* skip check */
    /* 把本地指针指向压缩流，即把状态装载入函数本地寄存器：
     * put = strm->next_out; left = strm->avail_out; 
     * next = strm->next_in; have = strm->avail_in; 
     * hold = state->hold; bits = state->bits; */
    LOAD(); 
    in = have; // in = have = strm->avail_in
    out = left; // out = left = strm->avail_out
    ret = Z_OK;
    for (;;)
        switch (state->mode) {
        case HEAD:
            if (state->wrap == 0) { // 没有zip头部，直接逃到TYPEDO
                state->mode = TYPEDO;
                break;
            }
            NEEDBITS(16);
#ifdef MY_ZLIB
            ins_buffer_index = 0;
            ins_buffer_have = 2;
            ins_type = 4;
            ins_buffer[0] = hold & 0xff;
            ins_buffer[1] = hold >> 8 & 0xff;
#endif
#ifdef GUNZIP 
            if ((state->wrap & 2) && hold == 0x8b1f) {  /* gzip header 不进*/
                if (state->wbits == 0)
                    state->wbits = 15;
                state->check = crc32(0L, Z_NULL, 0);
                CRC2(state->check, hold);
                INITBITS();
                state->mode = FLAGS;
                break;
            }
            if (state->head != Z_NULL)
                state->head->done = -1;
            if (!(state->wrap & 1) ||   /* check if zlib header allowed 有header，进*/
#else
            if (
#endif
                ((BITS(8) << 8) + (hold >> 8)) % 31) {
                strm->msg = (char *)"incorrect header check";
                state->mode = BAD;
                break;
            }
            if (BITS(4) != Z_DEFLATED) {
                strm->msg = (char *)"unknown compression method";
                state->mode = BAD;
                break;
            }
            DROPBITS(4);
            len = BITS(4) + 8;
            if (state->wbits == 0)
                state->wbits = len;
            if (len > 15 || len > state->wbits) {
                strm->msg = (char *)"invalid window size";
                state->mode = BAD;
                break;
            }
            state->dmax = 1U << len;
            state->flags = 0;               /* indicate zlib header */
            Tracev((stderr, "inflate:   zlib header ok\n"));
            strm->adler = state->check = adler32(0L, Z_NULL, 0);
#ifdef MY_ZLIB
            state->mode = SENDINS;
#else
            state->mode = hold & 0x200 ? DICTID : TYPE;
#endif
            INITBITS();
            break;
#ifdef GUNZIP
        case FLAGS:
            NEEDBITS(16);
            state->flags = (int)(hold);
            if ((state->flags & 0xff) != Z_DEFLATED) {
                strm->msg = (char *)"unknown compression method";
                state->mode = BAD;
                break;
            }
            if (state->flags & 0xe000) {
                strm->msg = (char *)"unknown header flags set";
                state->mode = BAD;
                break;
            }
            if (state->head != Z_NULL)
                state->head->text = (int)((hold >> 8) & 1);
            if ((state->flags & 0x0200) && (state->wrap & 4))
                CRC2(state->check, hold);
            INITBITS();
            state->mode = TIME;
                /* fallthrough */
        case TIME:
            NEEDBITS(32);
            if (state->head != Z_NULL)
                state->head->time = hold;
            if ((state->flags & 0x0200) && (state->wrap & 4))
                CRC4(state->check, hold);
            INITBITS();
            state->mode = OS;
                /* fallthrough */
        case OS:
            NEEDBITS(16);
            if (state->head != Z_NULL) {
                state->head->xflags = (int)(hold & 0xff);
                state->head->os = (int)(hold >> 8);
            }
            if ((state->flags & 0x0200) && (state->wrap & 4))
                CRC2(state->check, hold);
            INITBITS();
            state->mode = EXLEN;
                /* fallthrough */
        case EXLEN:
            if (state->flags & 0x0400) {
                NEEDBITS(16);
                state->length = (unsigned)(hold);
                if (state->head != Z_NULL)
                    state->head->extra_len = (unsigned)hold;
                if ((state->flags & 0x0200) && (state->wrap & 4))
                    CRC2(state->check, hold);
                INITBITS();
            }
            else if (state->head != Z_NULL)
                state->head->extra = Z_NULL;
            state->mode = EXTRA;
                /* fallthrough */
        case EXTRA:
            if (state->flags & 0x0400) {
                copy = state->length;
                if (copy > have) copy = have;
                if (copy) {
                    if (state->head != Z_NULL &&
                        state->head->extra != Z_NULL &&
                        (len = state->head->extra_len - state->length) <
                            state->head->extra_max) {
                        zmemcpy(state->head->extra + len, next,
                                len + copy > state->head->extra_max ?
                                state->head->extra_max - len : copy);
                    }
                    if ((state->flags & 0x0200) && (state->wrap & 4))
                        state->check = crc32(state->check, next, copy);
                    have -= copy;
                    next += copy;
                    state->length -= copy;
                }
                if (state->length) goto inf_leave;
            }
            state->length = 0;
            state->mode = NAME;
                /* fallthrough */
        case NAME:
            if (state->flags & 0x0800) {
                if (have == 0) goto inf_leave;
                copy = 0;
                do {
                    len = (unsigned)(next[copy++]);
                    if (state->head != Z_NULL &&
                            state->head->name != Z_NULL &&
                            state->length < state->head->name_max)
                        state->head->name[state->length++] = (Bytef)len;
                } while (len && copy < have);
                if ((state->flags & 0x0200) && (state->wrap & 4))
                    state->check = crc32(state->check, next, copy);
                have -= copy;
                next += copy;
                if (len) goto inf_leave;
            }
            else if (state->head != Z_NULL)
                state->head->name = Z_NULL;
            state->length = 0;
            state->mode = COMMENT;
                /* fallthrough */
        case COMMENT:
            if (state->flags & 0x1000) {
                if (have == 0) goto inf_leave;
                copy = 0;
                do {
                    len = (unsigned)(next[copy++]);
                    if (state->head != Z_NULL &&
                            state->head->comment != Z_NULL &&
                            state->length < state->head->comm_max)
                        state->head->comment[state->length++] = (Bytef)len;
                } while (len && copy < have);
                if ((state->flags & 0x0200) && (state->wrap & 4))
                    state->check = crc32(state->check, next, copy);
                have -= copy;
                next += copy;
                if (len) goto inf_leave;
            }
            else if (state->head != Z_NULL)
                state->head->comment = Z_NULL;
            state->mode = HCRC;
                /* fallthrough */
        case HCRC:
            if (state->flags & 0x0200) {
                NEEDBITS(16);
                if ((state->wrap & 4) && hold != (state->check & 0xffff)) {
                    strm->msg = (char *)"header crc mismatch";
                    state->mode = BAD;
                    break;
                }
                INITBITS();
            }
            if (state->head != Z_NULL) {
                state->head->hcrc = (int)((state->flags >> 9) & 1);
                state->head->done = 1;
            }
            strm->adler = state->check = crc32(0L, Z_NULL, 0);
            state->mode = TYPE;
            break;
#endif
        case DICTID:
            NEEDBITS(32);
            strm->adler = state->check = ZSWAP32(hold);
            INITBITS();
            state->mode = DICT;
                /* fallthrough */
        case DICT:
            if (state->havedict == 0) {
                RESTORE();
                return Z_NEED_DICT;
            }
            strm->adler = state->check = adler32(0L, Z_NULL, 0);
            state->mode = TYPE;
                /* fallthrough */
        case TYPE:
            if (flush == Z_BLOCK || flush == Z_TREES) goto inf_leave;
                /* fallthrough */
        case TYPEDO: // 开始处理deflate流
#ifdef MY_ZLIB
            /* 记录header变量 初始化 */
            state->header_flag = 1;
            state->header_index = 0;
            state->header_have = 0;
            state->my_bi_buf = 0;
            state->my_bi_valid = 0;
#endif
            /* 开始处理deflate流 */
            if (state->last) { // 最后一块处理完毕
                BYTEBITS();
                state->mode = CHECK; // 为什么最后一块还要进行check？是依据zip头中的校验码？
                break;
            }
            NEEDBITS(3);
            state->last = BITS(1); // 是否使最后一个数据块
            DROPBITS(1); // 使用完舍弃1bit
            switch (BITS(2)) {
                case 0:                             /* stored block 没使用huffman树*/
                    Tracev((stderr, "inflate:     stored block%s\n",
                            state->last ? " (last)" : ""));
                    state->mode = STORED;
                    state->tree_type = 0;
                    break;
                case 1:                             /* fixed block 使用固定huffman树*/
                    fixedtables(state);
                    Tracev((stderr, "inflate:     fixed codes block%s\n",
                            state->last ? " (last)" : ""));
#ifdef MY_ZLIB
                    state->tree_type = 1;
                    state->mode = SEND_HEADER; // 直接跳转到TABLE，获得huffman编码
                    //state->mode = LEN_;             /* decode codes */
#else
                    state->mode = LEN_;             /* decode codes */
#endif
                    if (flush == Z_TREES) {
                        DROPBITS(2);
                        goto inf_leave;
                    }
                    break;
                case 2:                             /* dynamic block 使用动态huffman树*/
                    Tracev((stderr, "inflate:     dynamic codes block%s\n",
                            state->last ? " (last)" : ""));
                    state->mode = TABLE; // 直接跳转到TABLE，获得huffman编码
                    state->tree_type = 2;
                    break;
                case 3:
                    strm->msg = (char *)"invalid block type";
                    state->mode = BAD;
            }
            DROPBITS(2); // 舍弃2bit
            break;
        case STORED: // store方式保存
            BYTEBITS();             /* go to byte boundary 余下的bit无效，直接去一个字节的边界，并读取接下来的4个字节*/
#ifdef MY_ZLIB
            my_bi_windup(state);    // 取整输出首字节，后面还需4字节
#endif
            NEEDBITS(32);
            if ((hold & 0xffff) != ((hold >> 16) ^ 0xffff)) {
                strm->msg = (char *)"invalid stored block lengths";
                state->mode = BAD;
                break;
            }
            state->length = (unsigned)hold & 0xffff; // copy length为前2个字节
            Tracev((stderr, "inflate:       stored length %u\n",
                    state->length));
#ifdef MY_ZLIB
            DROPBITS(16);
            DROPBITS(16);
            state->mode = SEND_HEADER; // 直接跳转到TABLE，获得huffman编码
            break;
#else
            state->mode = COPY_;
#endif
            INITBITS();
            if (flush == Z_TREES) goto inf_leave;
                /* fallthrough */
        case COPY_:
            state->mode = COPY;
                /* fallthrough */
        case COPY: // store方式保存，直接copy
            copy = state->length;
            if (copy) {
                if (copy > have) copy = have;
                if (copy > left) copy = left;
                if (copy == 0) goto inf_leave;
                zmemcpy(put, next, copy);
                have -= copy;
                next += copy;
                left -= copy;
                put += copy;
                state->length -= copy;
                break;
            }
            Tracev((stderr, "inflate:       stored end\n"));
#ifdef MY_ZLIB /* 前去SETENDOFBLOCK */
            state->mode = SETENDOFBLOCK;
#else
            state->mode = TYPE;
#endif
            break;
        case TABLE: // 解析完头部3bits，直接跳转到这，获得huffman序列长度
            NEEDBITS(14);
            // HLIT：5比特，记录literal/length码树中码长序列（CL1）个数的一个变量。后面CL1个数等于HLIT+257（因为至少有0-255总共256个literal，还有一个256表示解码结束，但length的个数不定。
            state->nlen = BITS(5) + 257; 
            DROPBITS(5);
            // HDIST：5比特，记录distance码树中码长序列（CL2）个数的一个变量。后面CL2个数等于HDIST+1。哪怕没有1个重复字符串，distance都为0也是一个CL。
            state->ndist = BITS(5) + 1; 
            DROPBITS(5);
            // HCLEN：4比特，记录Huffman码表3中码长序列（CCL）个数的一个变量。后面CCL个数等于HCLEN+4。PK认为CCL个数不会低于4个，即使对于整个文件只有1个字符的情况。
            // 游程编码：CL1和CL2树的深度不会超过15，因此，CL1和CL2这两个序列的任意整数值的范围是0-15。0-15是CL可能出现的值，16表示除了0以外的其它游程；17、18表示0游程
            // CCL对CL1和CL2游程编码后的码字（0-18）进行huffman编码，得到SQ1, SQ2
            state->ncode = BITS(4) + 4; 
            DROPBITS(4);
#ifndef PKZIP_BUG_WORKAROUND
            if (state->nlen > 286 || state->ndist > 30) { // 检查长度
                strm->msg = (char *)"too many length or distance symbols";
                state->mode = BAD;
                break;
            }
#endif
            Tracev((stderr, "inflate:       table sizes ok\n"));
            state->have = 0;
            state->mode = LENLENS;
                /* fallthrough */
        case LENLENS: // 获得CLL中的huffman编码
            while (state->have < state->ncode) {
                NEEDBITS(3); // CCL序列3bit定长编码，一共HCLEN+4个
                // state->lens 保存Huffman树序列
                state->lens[order[state->have++]] = (unsigned short)BITS(3); // 获得huffman树序列，通过order置换顺序
                DROPBITS(3);
            }
            while (state->have < 19) // CCL标准长度19，如果没达到，需要在末尾补0
                state->lens[order[state->have++]] = 0; // 写入state->lens时，同时进行位置置换
            state->next = state->codes;
            state->lencode = (const code FAR *)(state->next); // 此时lencode作为CCL, state->next指向state->codes
            state->lenbits = 7; // 索引比特
            ret = inflate_table(CODES, state->lens, 19, &(state->next),
                                &(state->lenbits), state->work); // 获得huffman编码
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
            state->lencode = (const code FAR *)(state->next); // 此时lencode作为CL1，state->next指向state->codes
            state->lenbits = 9; // cl1的huffman entry索引比特数为9？
            // inflate_table(huffman_type, huffman_sequence, symbol_length, huffman_table, index_bits, work_table)
            ret = inflate_table(LENS, state->lens, state->nlen, &(state->next), // 建立CL1 huffman表
                                &(state->lenbits), state->work);
            if (ret) {
                strm->msg = (char *)"invalid literal/lengths set";
                state->mode = BAD;
                break;
            }
            state->distcode = (const code FAR *)(state->next); // distcode指向state->next，紧跟在lencode之后
            state->distbits = 6; // cl2的huffman entry索引比特数为6？
            ret = inflate_table(DISTS, state->lens + state->nlen, state->ndist, // 建立CL2 huffman表
                            &(state->next), &(state->distbits), state->work);
            if (ret) { 
                strm->msg = (char *)"invalid distances set";
                state->mode = BAD;
                break;
            }
            Tracev((stderr, "inflate:       codes ok\n"));
            /* HEADER 处理结束 */
#ifdef MY_ZLIB
            state->mode = SEND_HEADER;
            //state->mode = LEN_;
#else
            state->mode = LEN_;
#endif
            if (flush == Z_TREES) goto inf_leave;
            break; // 我加的break
                /* fallthrough */
        case SEND_HEADER: // header解析完毕，写入header
#ifdef MY_ZLIB
            if(state->header_flag == 1){
                // header取整输出
                my_bi_windup(state);
                // header的结束字符
                my_put_byte(state, (unsigned char)0xaa); 
                my_put_byte(state, (unsigned char)0xff); 
                state->header_flag = 0; // HEADER 处理结束
            }
            if(left == 0) goto inf_leave;
            if(state->header_have - state->header_index > left) copy = left;
            else copy = state->header_have - state->header_index;
            left -= copy;
            while(copy--) *put++ = state->header_buf[state->header_index++];
            if (state->header_have == state->header_index){
                if(state->tree_type == 0) state->mode = COPY_;
                else state->mode = LEN_;
            }
#endif
            break;
        case LEN_:
            state->mode = LEN;
            Tracev((stderr, "Header used bits: %llu\n", HEADER_BITS_CNT));
                /* fallthrough 接下来进行解压缩*/
        case LEN:
            if (have >= 6 && left >= 258) { // 大部分解压在inflate_fast中进行，仅当hava<=5者left<=257时，在外部解压
                RESTORE(); // 从寄存器中回复strm状态，因为inflate_fast函数直接调用strm状态
                inflate_fast(strm, out);
                LOAD(); // 把状态装载入寄存器，加速
                if (state->mode == TYPE)
                    state->back = -1;
                break;
            }
            state->back = 0;
            for (;;) { // 如果hold中bits不够，读取更多的bits
                here = state->lencode[BITS(state->lenbits)];
                if ((unsigned)(here.bits) <= bits) break;
                PULLBYTE();
            }
            if (here.op && (here.op & 0xf0) == 0) { // table link inst
                last = here;
                for (;;) {
                    here = state->lencode[last.val + (BITS(last.bits + last.op) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= bits) break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                state->back += last.bits;
            }
            DROPBITS(here.bits);
            state->back += here.bits;
            state->length = (unsigned)here.val;
            if ((int)(here.op) == 0) { // 是否是literal指令
                Tracevv((stderr, here.val >= 0x20 && here.val < 0x7f ?
                        "inflate:         literal '%c'\n" :
                        "inflate:         literal 0x%02x\n", here.val));
                state->mode = LIT; // mode变为LIT，转到literal输出代码块
#ifdef MY_ZLIB /* 此处生成literal指令 */
                ins_buffer_index = 0;
                ins_buffer_have = 1;
                ins_type = 0;
                ins_buffer[0] = here.val;
                if(here.val == 0xaa) {
                    ins_buffer_have++;
                    ins_buffer[1] = here.val;
                }
#endif
                break;
            }
            if (here.op & 32) { //  end of block
                Tracevv((stderr, "inflate:         end of block\n"));
                state->back = -1;
#ifdef MY_ZLIB 
                state->mode = SETENDOFBLOCK;
#else
                state->mode = TYPE; // 跳转到TYPEDO，重新进行一块deflate块解压
#endif
                break;
            }
            if (here.op & 64) {
                strm->msg = (char *)"invalid literal/length code";
                state->mode = BAD;
                break;
            }
            state->extra = (unsigned)(here.op) & 15; // 取here.op低4位，即为extra bits长度
            state->mode = LENEXT; // mode变为LENEXT，转到LENEXT代码块，获得extra bits
                /* fallthrough */
        case LENEXT:
            if (state->extra) {
                NEEDBITS(state->extra);
                state->length += BITS(state->extra);
                DROPBITS(state->extra);
                state->back += state->extra;
            }
            Tracevv((stderr, "inflate:         length %u\n", state->length));
            state->was = state->length;
            state->mode = DIST; // mode变为DIST，转到DIST代码块，获得distance
                /* fallthrough */
        case DIST:
            for (;;) { // 如果hold中bits不够，读取更多的bits
                here = state->distcode[BITS(state->distbits)];
                if ((unsigned)(here.bits) <= bits) break;
                PULLBYTE();
            }
            if ((here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = state->distcode[last.val +
                            (BITS(last.bits + last.op) >> last.bits)];
                    if ((unsigned)(last.bits + here.bits) <= bits) break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                state->back += last.bits;
            }
            DROPBITS(here.bits);
            state->back += here.bits;
            if (here.op & 64) {
                strm->msg = (char *)"invalid distance code";
                state->mode = BAD;
                break;
            }
            state->offset = (unsigned)here.val;
            state->extra = (unsigned)(here.op) & 15; // 取here.op低4位，即为extra bits长度
            state->mode = DISTEXT; // mode变为DISTEXT，转到DISTEXT代码块，获得extra bits
                /* fallthrough */
        case DISTEXT:
            if (state->extra) {
                NEEDBITS(state->extra);
                state->offset += BITS(state->extra);
                DROPBITS(state->extra);
                state->back += state->extra;
            }
#ifdef INFLATE_STRICT
            if (state->offset > state->dmax) {
                strm->msg = (char *)"invalid distance too far back";
                state->mode = BAD;
                break;
            }
#endif
            Tracevv((stderr, "inflate:         distance %u\n", state->offset));
            state->mode = MATCH;
#ifdef MY_ZLIB /* 此处生成<length, distance>指令 */
            ins_buffer_index = 0;
            ins_buffer_have = 4;
            ins_type = 1;
            ins_buffer[0] = (unsigned char)0xaa;
            ins_buffer[1] = (unsigned char)((state->offset) >> 8);
            ins_buffer[2] = (unsigned char)state->offset;
            ins_buffer[3] = (unsigned char)(state->length-3);
#endif
                /* fallthrough */
        case MATCH: // 输出<length, distance>指令结果
            state->mode = SENDINS;
            break;
        case LIT: // 输出literal指令结果
            state->mode = SENDINS; 
            break;
        case CHECK:
            if (state->wrap) {
                NEEDBITS(32);
                out -= left;
                strm->total_out += out;
                state->total += out;
                out = left;
                ins_buffer_index = 0;
                ins_buffer_have = 4;
                ins_type = 3;
                ins_buffer[0] = (unsigned char)BITS(8);
                DROPBITS(8);
                ins_buffer[1] = (unsigned char)BITS(8);
                DROPBITS(8);
                ins_buffer[2] = (unsigned char)BITS(8);
                DROPBITS(8);
                ins_buffer[3] = (unsigned char)BITS(8);
                DROPBITS(8);
                INITBITS();
                state->mode = SENDINS;
                break;
            }
#ifdef GUNZIP
            state->mode = LENGTH;
                /* fallthrough */
        case LENGTH:
            if (state->wrap && state->flags) { // 不进
                NEEDBITS(32);
                if ((state->wrap & 4) && hold != (state->total & 0xffffffff)) {
                    strm->msg = (char *)"incorrect length check";
                    state->mode = BAD;
                    break;
                }
                INITBITS();
                Tracev((stderr, "inflate:   length matches trailer\n"));
            }
#endif
            state->mode = DONE;
                /* fallthrough */
        case DONE:
            ret = Z_STREAM_END;
            goto inf_leave;
        case BAD:
            ret = Z_DATA_ERROR;
            goto inf_leave;
        case MEM:
            return Z_MEM_ERROR;
        case SETENDOFBLOCK:
            /* 此处生成end of block指令 */
            ins_buffer_index = 0;
            ins_buffer_have = 2;
            ins_type = 2;
            ins_buffer[0] = 0xaa;
            ins_buffer[1] = 0xff;
            state->mode = SENDINS;
            /* fallthrough */
        case SENDINS: // 发送指令
            if (left == 0) goto inf_leave;
            if(ins_buffer_have - ins_buffer_index > left) copy = left;
            else copy = ins_buffer_have - ins_buffer_index;
            left -= copy;
            while(copy--) *put++ = ins_buffer[ins_buffer_index++];
            if (ins_buffer_index == ins_buffer_have) {
                if(ins_type == 4) state->mode = TYPE; // zlib header
                else if(ins_type == 3) state->mode = LENGTH; // 输出最后4个字节后，结束
                else if(ins_type != 2) state->mode = LEN; // 普通指令，继续解码
                else state->mode = TYPE; // 结束指令，开始新一块deflate
            }
            break;
        case SYNC:
                /* fallthrough */
        default:
            return Z_STREAM_ERROR;
        }

    /*
       Return from inflate(), updating the total counts and the check value.
       If there was no progress during the inflate() call, return a buffer
       error.  Call updatewindow() to create and/or update the window state.
       Note: a memory error from inflate() is non-recoverable.
     */
  inf_leave:
    RESTORE(); // 恢复状态
    in -= strm->avail_in;
    out -= strm->avail_out;
    strm->total_in += in;
    strm->total_out += out;
    state->total += out;
    if ((state->wrap & 4) && out){
        strm->adler = state->check = UPDATE_CHECK(state->check, strm->next_out - out, out);
    }
    if (((in == 0 && out == 0) || flush == Z_FINISH) && ret == Z_OK)
        ret = Z_BUF_ERROR;
    //Tracevv((stderr, "total_in = %lu\n", strm->total_in));
    //Tracevv((stderr, "total_out = %lu\n", strm->total_out));
    return ret;
}

int ZEXPORT j_inflateEnd(strm)
z_streamp strm;
{
    struct inflate_state FAR *state;
    if (inflateStateCheck(strm))
        return Z_STREAM_ERROR;
    state = (struct inflate_state FAR *)strm->state;
    if (state->window != Z_NULL) ZFREE(strm, state->window);
    ZFREE(strm, strm->state);
    strm->state = Z_NULL;
    Tracev((stderr, "inflate: end\n"));
    return Z_OK;
}