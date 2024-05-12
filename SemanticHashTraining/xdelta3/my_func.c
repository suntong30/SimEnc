#ifndef XDELTA3_COMPRESS
#define XDELTA3_COMPRESS
int xdelta3_compress(char* InFile, int InSize, char* SrcFile, int SrcSize, char* OutFile, int encode) {
	printf("aaaaaaa\n");
	int BufSize = 512*1024+10; // 修改大小
	int OutSize = 0;

	int r, ret;
	xd3_stream stream;
	xd3_config config;
	xd3_source source;
	void* Input_Buf;
	int Input_Buf_Read;


	printf("start\n");
	if (BufSize < XD3_ALLOCSIZE)
		BufSize = XD3_ALLOCSIZE;

	memset (&stream, 0, sizeof (stream));
	memset (&source, 0, sizeof (source));

	xd3_init_config(&config, XD3_ADLER32);
	config.winsize = BufSize;
	xd3_config_stream(&stream, &config);

	source.blksize = BufSize;
	source.curblk = (const unsigned char*)malloc(source.blksize);

	/* Load 1st block of stream. */
	source.onblk = min(source.blksize, SrcSize);
	memcpy((void*)source.curblk, SrcFile, source.onblk);
	SrcSize -= source.onblk;
	source.curblkno = 0;
	/* Set the stream. */
	xd3_set_source(&stream, &source);


	Input_Buf = malloc(BufSize);

	do
	{
		Input_Buf_Read = min(BufSize, InSize);
		memcpy(Input_Buf, InFile, Input_Buf_Read);
		if (Input_Buf_Read < BufSize)
		{
			xd3_set_flags(&stream, XD3_FLUSH | stream.flags);
		}
		xd3_avail_input(&stream, (const unsigned char*)Input_Buf, Input_Buf_Read);

process:
		if (encode)
			ret = xd3_encode_input(&stream);
		else
			ret = xd3_decode_input(&stream);

		switch (ret)
		{
			case XD3_INPUT:
				{
					continue;
				}

			case XD3_OUTPUT:
				{
					memcpy(OutFile + OutSize, stream.next_out, stream.avail_out);
					OutSize += stream.avail_out;
					xd3_consume_output(&stream);
					goto process;
				}

			case XD3_GETSRCBLK:
				{
					source.onblk = min(source.blksize, SrcSize);
					memcpy((void*)source.curblk, SrcFile + source.blksize * source.getblkno, source.onblk);
					SrcSize -= source.onblk;
					source.curblkno = source.getblkno;
					goto process;
				}

			case XD3_GOTHEADER:
				{
					goto process;
				}

			case XD3_WINSTART:
				{
					goto process;
				}

			case XD3_WINFINISH:
				{
					goto process;
				}

			default:
				{
					return ret;
				}

		}

	}
	while (Input_Buf_Read == BufSize);

	free(Input_Buf);

	free((void*)source.curblk);
	xd3_close_stream(&stream);
	xd3_free_stream(&stream);

	return OutSize;

};
#endif XDELTA3_COMPRESS
