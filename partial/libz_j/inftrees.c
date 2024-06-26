#include "zutil.h"
#include "inftrees.h"

#define MAXBITS 15
/*
   Build a set of tables to decode the provided canonical Huffman code.
   The code lengths are lens[0..codes-1].  The result starts at *table,
   whose indices are 0..2^bits-1.  work is a writable array of at least
   lens shorts, which is used as a work area.  type is the type of code
   to be generated, CODES, LENS, or DISTS.  On return, zero is success,
   -1 is an invalid code, and +1 means that ENOUGH isn't enough.  table
   on return points to the next available entry's address.  bits is the
   requested root table index bits, and on return it is the actual root
   table index bits.  It will differ if the request is greater than the
   longest code or if it is less than the shortest code.
 */
int ZLIB_INTERNAL inflate_table(type, lens, codes, table, bits, work)
codetype type;
unsigned short FAR *lens;
unsigned codes;
code FAR * FAR *table;
unsigned FAR *bits;
unsigned short FAR *work;
{
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
    code here;                  /* table entry for duplication */
    code FAR *next;             /* next available space in table */
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

    /*
       Process a set of code lengths to create a canonical Huffman code.  The
       code lengths are lens[0..codes-1].  Each length corresponds to the
       symbols 0..codes-1.  The Huffman code is generated by first sorting the
       symbols by length from short to long, and retaining the symbol order
       for codes with equal lengths.  Then the code starts with all zero bits
       for the first code of the shortest length, and the codes are integer
       increments for the same length, and zeros are appended as the length
       increases.  For the deflate format, these bits are stored backwards
       from their more natural integer increment ordering, and so when the
       decoding tables are built in the large loop below, the integer codes
       are incremented backwards.

       This routine assumes, but does not check, that all of the entries in
       lens[] are in the range 0..MAXBITS.  The caller must assure this.
       1..MAXBITS is interpreted as that code length.  zero means that that
       symbol does not occur in this code.

       The codes are sorted by computing a count of codes for each length,
       creating from that a table of starting indices for each length in the
       sorted table, and then entering the symbols in order in the sorted
       table.  The sorted table is work[], with that space being provided by
       the caller.

       The length counts are used for other purposes as well, i.e. finding
       the minimum and maximum length codes, determining if there are any
       codes at all, checking for a valid set of lengths, and looking ahead
       at length counts to determine sub-table sizes when building the
       decoding tables.
     */

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

    /*
       Create and fill in decoding tables.  In this loop, the table being
       filled is at next and has curr index bits.  The code being used is huff
       with length len.  That code is converted to an index by dropping drop
       bits off of the bottom.  For codes where len is less than drop + curr,
       those top drop + curr - len bits are incremented through all values to
       fill the table with replicated entries.

       root is the number of index bits for the root table.  When len exceeds
       root, sub-tables are created pointed to by the root entry with an index
       of the low root bits of huff.  This is saved in low to check for when a
       new sub-table should be started.  drop is zero when the root table is
       being filled, and drop is root when sub-tables are being filled.

       When a new sub-table is needed, it is necessary to look ahead in the
       code lengths to determine what size sub-table is needed.  The length
       counts are used for this, and so count[] is decremented as codes are
       entered in the tables.

       used keeps track of how many table entries have been allocated from the
       provided *table space.  It is checked for LENS and DIST tables against
       the constants ENOUGH_LENS and ENOUGH_DISTS to guard against changes in
       the initial root table size constants.  See the comments in inftrees.h
       for more information.

       sym increments through all symbols, and the loop terminates when
       all codes of length max, i.e. all codes, have been processed.  This
       routine permits incomplete codes, so another loop after this one fills
       in the rest of the decoding tables with invalid code markers.
     */

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
    for (;;) {
        /* create table entry 从排名第一的开始，即从编码最短的开始。构造huffman table。*/
        // here.bits获得code len，即ctdata.len；huff获得huffman编码，即ctdata.code；
        // here.val作为ctdata索引；here.op作为指令类型
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
            if(work[sym]==3 && huff == 511 && len==9 && work[sym-1]==2&& work[sym-2]==0&& work[sym-3]==29&& work[sym-4]==28){
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
