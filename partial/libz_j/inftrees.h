/* Structure for decoding tables.  Each entry provides either the
   information needed to do the operation requested by the code that
   indexed that table entry, or it provides a pointer to another
   table that indexes more bits of the code.  op indicates whether
   the entry is a pointer to another table, a literal, a length or
   distance, an end-of-block, or an invalid code.  For a table
   pointer, the low four bits of op is the number of index bits of
   that table.  For a length or distance, the low four bits of op
   is the number of extra bits to get after the code.  bits is
   the number of bits in this code or part of the code to drop off
   of the bit buffer.  val is the actual byte to output in the case
   of a literal, the base length or distance, or the offset from
   the current table to the next table.  Each entry is four bytes. 
   表之间的连接？ 
   op: 指令类型（pointer, literal, length, distance, end）,低4比特可以表示extra bits的比特数，
   bits: 该指令所用的code bits，直接进行dropbits
   val: 实值，literal或者Base length,distance。或者是table切换的偏移*/
typedef struct {
    unsigned char op;           /* operation, extra bits, table bits 1B */
    unsigned char bits;         /* bits in this part of the code (code的比特长度)1B */
    unsigned short val;         /* offset in table or code value 2B */
} code;

/* op values as set by inflate_table():
    00000000 - literal 0
    0000tttt - table link, tttt != 0 is the number of table index bits ，低4位表示新表的索引比特
    0001eeee - length or distance, eeee is the number of extra bits ,&16
    01100000 - end of block 96 (32+64)
    01000000 - invalid code 64
 */

/* Maximum size of the dynamic table.  The maximum number of code structures is
   1444, which is the sum of 852 for literal/length codes and 592 for distance
   codes.  These values were found by exhaustive searches using the program
   examples/enough.c found in the zlib distribution.  The arguments to that
   program are the number of symbols, the initial root table size, and the
   maximum bit length of a code.  "enough 286 9 15" for literal/length codes
   returns returns 852, and "enough 30 6 15" for distance codes returns 592.
   The initial root table size (9 or 6) is found in the fifth argument of the
   inflate_table() calls in inflate.c and infback.c.  If the root table size is
   changed, then these maximum sizes would be need to be recalculated and
   updated. */
#define ENOUGH_LENS 852
#define ENOUGH_DISTS 592
#define ENOUGH (ENOUGH_LENS+ENOUGH_DISTS)

/* Type of code to build for inflate_table() */
typedef enum {
    CODES,
    LENS,
    DISTS
} codetype;

int ZLIB_INTERNAL inflate_table OF((codetype type, unsigned short FAR *lens,
                             unsigned codes, code FAR * FAR *table,
                             unsigned FAR *bits, unsigned short FAR *work));
