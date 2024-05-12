#include "zutil.h"
#include "inftrees.h"
#include "inflate.h"
#include "inffast.h"

#ifdef ASMINF
#  pragma message("Assembler code may have bugs -- use at your own risk")
#else

/*
   Decode literal, length, and distance codes and write out the resulting
   literal and match bytes until either not enough input or output is
   available, an end-of-block is encountered, or a data error is encountered.
   When large enough input and output buffers are supplied to inflate(), for
   example, a 16K input buffer and a 64K output buffer, more than 95% of the
   inflate execution time is spent in this routine.

   Entry assumptions:

        state->mode == LEN
        strm->avail_in >= 6
        strm->avail_out >= 258
        start >= strm->avail_out
        state->bits < 8

   On return, state->mode is one of:

        LEN -- ran out of enough output space or enough available input
        TYPE -- reached end of block code, inflate() to interpret next block
        BAD -- error in block data

   Notes:

    - The maximum input bits used by a length/distance pair is 15 bits for the
      length code, 5 bits for the length extra, 15 bits for the distance code,
      and 13 bits for the distance extra.  This totals 48 bits, or six bytes.
      Therefore if strm->avail_in >= 6, then there is enough input to avoid
      checking for available input while decoding.

    - The maximum bytes that a single length/distance pair can output is 258
      bytes, which is the maximum length that can be coded.  inflate_fast()
      requires strm->avail_out >= 258 for each loop to avoid checking for
      output space.
 */
void ZLIB_INTERNAL inflate_fast(strm, start)
z_streamp strm;
unsigned start;         /* inflate()'s starting value for strm->avail_out */
{
    struct inflate_state FAR *state;
    z_const unsigned char FAR *in;      /* local strm->next_in */
    z_const unsigned char FAR *last;    /* have enough input while in < last */
    unsigned char FAR *out;     /* local strm->next_out */
    unsigned char FAR *beg;     /* inflate()'s initial strm->next_out */
    unsigned char FAR *end;     /* while out < end, enough space available */
#ifdef INFLATE_STRICT
    unsigned dmax;              /* maximum distance from zlib header */
#endif
    unsigned wsize;             /* window size or zero if not using window */
    unsigned whave;             /* valid bytes in the window */
    unsigned wnext;             /* window write index */
    unsigned char FAR *window;  /* allocated sliding window, if wsize != 0 */
    unsigned long hold;         /* local strm->hold */
    unsigned bits;              /* local strm->bits */
    code const FAR *lcode;      /* local strm->lencode */
    code const FAR *dcode;      /* local strm->distcode */
    unsigned lmask;             /* mask for first level of length codes */
    unsigned dmask;             /* mask for first level of distance codes */
    code const *here;           /* retrieved table entry */
    unsigned op;                /* code bits, operation, extra bits, or */
                                /*  window position, window bytes to copy */
    unsigned len;               /* match length, unused bytes */
    unsigned dist;              /* match distance */
    unsigned char FAR *from;    /* where to copy match from */

    /* copy state to local variables */
    state = (struct inflate_state FAR *)strm->state;
    in = strm->next_in;
    last = in + (strm->avail_in - 5); // 保留5个字节，在外部解压
    out = strm->next_out;
    beg = out - (start - strm->avail_out);
    end = out + (strm->avail_out - 257); // 保留257字节，在外部解压。避免输出的字节超过缓存大小
#ifdef INFLATE_STRICT
    dmax = state->dmax;
#endif
    wsize = state->wsize;
    whave = state->whave;
    wnext = state->wnext;
    window = state->window;
    hold = state->hold;
    bits = state->bits;
    lcode = state->lencode;
    dcode = state->distcode;
    lmask = (1U << state->lenbits) - 1;
    dmask = (1U << state->distbits) - 1;

    /* decode literals and length/distances until end-of-block or not enough
       input data or output space */
    do {
        if (bits < 15) { // bits<15，读入bit流
            hold += (unsigned long)(*in++) << bits; // 新读取8bit，写入高位
            bits += 8;
            hold += (unsigned long)(*in++) << bits; // 新读取8bit，写入高位
            bits += 8;
        }
        here = lcode + (hold & lmask); // lenbits=9，hold & lmask作为CL1 entry索引？为什么索引固定Lenbits？
      dolen: // 怎么确定Huffman编码？
        op = (unsigned)(here->bits); // 获得code bits
        hold >>= op; bits -= op; // 舍弃code bits
        op = (unsigned)(here->op); // 指令类型
        if (op == 0) { /* literal 字符的编码 */
            Tracevv((stderr, here->val >= 0x20 && here->val < 0x7f ?
                    "inflate:         literal '%c'\n" :
                    "inflate:         literal 0x%02x\n", here->val));
#ifdef MY_ZLIB /* 在此处生成literal ins (cc)*/
            if(here->val==0xaa) *out++ = (unsigned char)(here->val);
            *out++ = (unsigned char)(here->val);
#else
            *out++ = (unsigned char)(here->val);
#endif
        }
        else if (op & 16) {  /* length base, length的编码 */
            len = (unsigned)(here->val); // length的实值
            op &= 15;        /* number of extra bits，op的低4位，获得extra bits长度*/
            if (op) { // 取extra bits
                if (bits < op) {
                    hold += (unsigned long)(*in++) << bits;
                    bits += 8;
                }
                len += (unsigned)hold & ((1U << op) - 1); // length读取完成
                hold >>= op; bits -= op; // 舍弃extra bis
            }
            Tracevv((stderr, "inflate:         length %u\n", len));
            if (bits < 15) { // 读取bit流
                hold += (unsigned long)(*in++) << bits;
                bits += 8;
                hold += (unsigned long)(*in++) << bits;
                bits += 8;
            }
            here = dcode + (hold & dmask); // distbits=6，hold & dmask作为CL2 entry索引？为什么索引固定distbits？
          dodist:
            op = (unsigned)(here->bits); // dist指令占用的Bit数
            hold >>= op; bits -= op; // 舍弃code bits
            op = (unsigned)(here->op);
            if (op & 16) {                      /* distance base, 检查是否是distance */
                dist = (unsigned)(here->val);
                op &= 15;                       /* number of extra bits, extra bits所占比特数 */
                if (bits < op) {
                    hold += (unsigned long)(*in++) << bits;
                    bits += 8;
                    if (bits < op) {
                        hold += (unsigned long)(*in++) << bits;
                        bits += 8;
                    }
                }
                dist += (unsigned)hold & ((1U << op) - 1); // dist读取完成
#ifdef INFLATE_STRICT
                if (dist > dmax) {
                    strm->msg = (char *)"invalid distance too far back";
                    state->mode = BAD;
                    break;
                }
#endif
                hold >>= op; bits -= op; // 舍弃extra bits
                Tracevv((stderr, "inflate:         distance %u\n", dist));
#ifdef MY_ZLIB /* 此处生成distance ins和length ins (0xaa, dist, dist >> 8, len - 3)*/
                *out++ = (unsigned char)0xaa;
                *out++ = (unsigned char)(dist >> 8);
                *out++ = (unsigned char)dist;
                *out++ = (unsigned char)(len-3);
#else           /* 输出解压数据 */
                op = (unsigned)(out - beg);     /* max distance in output */
                if (dist > op) {                /* see if copy from window 执行copy指令*/
                    op = dist - op;             /* distance back in window */
                    if (op > whave) {
                        if (state->sane) {
                            strm->msg =
                                (char *)"invalid distance too far back";
                            state->mode = BAD;
                            break;
                        }
#ifdef INFLATE_ALLOW_INVALID_DISTANCE_TOOFAR_ARRR
                        if (len <= op - whave) {
                            do {
                                *out++ = 0;
                            } while (--len);
                            continue;
                        }
                        len -= op - whave;
                        do {
                            *out++ = 0;
                        } while (--op > whave);
                        if (op == 0) {
                            from = out - dist;
                            do {
                                *out++ = *from++;
                            } while (--len);
                            continue;
                        }
#endif
                    }
                    from = window;
                    if (wnext == 0) {           /* very common case */
                        from += wsize - op;
                        if (op < len) {         /* some from window */
                            len -= op;
                            do {
                                *out++ = *from++;
                            } while (--op);
                            from = out - dist;  /* rest from output */
                        }
                    }
                    else if (wnext < op) {      /* wrap around window */
                        from += wsize + wnext - op;
                        op -= wnext;
                        if (op < len) {         /* some from end of window */
                            len -= op;
                            do {
                                *out++ = *from++;
                            } while (--op);
                            from = window;
                            if (wnext < len) {  /* some from start of window */
                                op = wnext;
                                len -= op;
                                do {
                                    *out++ = *from++;
                                } while (--op);
                                from = out - dist;      /* rest from output */
                            }
                        }
                    }
                    else {                      /* contiguous in window */
                        from += wnext - op;
                        if (op < len) {         /* some from window */
                            len -= op;
                            do {
                                *out++ = *from++;
                            } while (--op);
                            from = out - dist;  /* rest from output */
                        }
                    }
                    while (len > 2) {
                        *out++ = *from++;
                        *out++ = *from++;
                        *out++ = *from++;
                        len -= 3;
                    }
                    if (len) {
                        *out++ = *from++;
                        if (len > 1)
                            *out++ = *from++;
                    }
                }
                else {
                    from = out - dist;          /* copy direct from output 从自身复制*/
                    do {                        /* minimum length is three */
                        *out++ = *from++;
                        *out++ = *from++;
                        *out++ = *from++;
                        len -= 3;
                    } while (len > 2);
                    if (len) {
                        *out++ = *from++;
                        if (len > 1)
                            *out++ = *from++;
                    }
                }
#endif
            }
            else if ((op & 64) == 0) {          /* 2nd level distance code, op为table link*/
                here = dcode + here->val + (hold & ((1U << op) - 1)); // 0000tttt , tttt != 0 is the number of table index bits
                goto dodist;
            }
            else {
                strm->msg = (char *)"invalid distance code";
                state->mode = BAD;
                break;
            }
        }
        else if ((op & 64) == 0) {              /* 2nd level length code , op为table link*/
            here = lcode + here->val + (hold & ((1U << op) - 1)); // 0000tttt , tttt != 0 is the number of table index bits
            goto dolen;
        }
        else if (op & 32) {                     /* end-of-block */
            Tracevv((stderr, "inflate:         end of block\n"));
            state->mode = TYPE;
#ifdef MY_ZLIB /* 此处生成end of block ins */
            *out++ = 0xaa;
            *out++ = 255;
#endif
            break;
        }
        else {
            strm->msg = (char *)"invalid literal/length code";
            state->mode = BAD;
            break;
        }
    } while (in < last && out < end);

    /* return unused bytes (on entry, bits < 8, so in won't go too far back) */
    len = bits >> 3; // 如果bits<8，则没有未使用的字节
    in -= len; // 回退未使用的字节

    #define BITS(n) ((unsigned)hold & ((1U << (n)) - 1))
    if(op & 32){ // end of block
        //Tracevv((stderr, "unused_bits = %lu, val = %lu\n", bits, BITS(bits)));
        /*Tracevv((stderr, "total_in + used_bytes = %lu\n", strm->total_in + in - strm->next_in));
        Tracevv((stderr, "total_out + used_bytes = %lu\n", strm->total_out + out - strm->next_out));*/
    }
    
    bits -= len << 3; // bits减少未使用的比特数
    hold &= (1U << bits) - 1; // 高位舍弃，取低bits位

    /* update state and return */

    strm->next_in = in;
    strm->next_out = out;
    strm->avail_in = (unsigned)(in < last ? 5 + (last - in) : 5 - (in - last));
    strm->avail_out = (unsigned)(out < end ?
                                 257 + (end - out) : 257 - (out - end));
    state->hold = hold;
    state->bits = bits;
    return;
}

/*
   inflate_fast() speedups that turned out slower (on a PowerPC G3 750CXe):
   - Using bit fields for code structure
   - Different op definition to avoid & for extra bits (do & for table bits)
   - Three separate decoding do-loops for direct, window, and wnext == 0
   - Special case for distance > 1 copies to do overlapped load and store copy
   - Explicit branch predictions (based on measured branch probabilities)
   - Deferring match copy and interspersed it with decoding subsequent codes
   - Swapping literal/length else
   - Swapping window/direct else
   - Larger unrolled copy loops (three is about right)
   - Moving len -= 3 statement into middle of loop
 */

#endif /* !ASMINF */
