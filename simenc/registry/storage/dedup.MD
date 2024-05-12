

//simenc: dedup operation
// once a layer or manifest is recieved, do dedup:
// read tarfile -> decompress -> unpack -> file digest -> check redis index table ->
// if not -> save, -> update redis index table
// else: -> drop
// update -> redis layer recipe
//
//			==== file level dedup: on disk storage ===
//			1. directory hierarchy, where unique files are saved
//				file_sha256/hex[:1]/sha256digest/filename.extension
//
//			2. directory,
//				sha256/hex[:1]/sha256digest/data -> removed
//				sha256/hex[:1]/sha256:digest/diff/path/to/uniqFile.extension
//
//			===== file level dedup: table on redis memory ====
//			added two table:
//					1. fileDescriptor table: [SUBIL CHECK: OK]
//								 |					|						|
//					files:digest |   file digest	|	location_on_disk	|
//								 |					|						|
//					2. recipe table [SUBIL CHECK: OK]
//								 |					|								     |
//			 layers:layer_digest |	 layer digest	|   /path/to/uniqFile.extension      |   file digest 
//								 |					|								     |
//
//			====== file level dedup: cache on redis memory ==== [SUBIL CHECK: UNKNOWN]
//					1. cache index table:
//					2. see if redis memory can store all recipe table
//								 |					|						|
//								 | uniq_file digest	|	location_on_disk	|
//								 |					|						|
//					3. cache files and cache layers
//
//
//

tables defined in blobs.go

three tables:

1. for dedup [SUBIL CHECK: OK]
type FileDescriptor struct {
	Descriptor
	// Digest uniquely identifies the content. A byte stream can be verified
	// against against this digest.
	Digest digest.Digest `json:"digest,omitempty"`

	FilePath string
	
	ServerIp string // the server which stores this digest' corresponding file
	RequestedServerIps  []string
}

2. for restoring [SUBIL CHECK: OK]
// simenc: Descriptors for blob-file recipe
type BFRecipeDescriptor struct{

	BlobDigest      digest.Digest
	BFDescriptors   []BFDescriptor  ----------------------------------------|
	ServerIp	s		[]string // the servers that stores all these files |
}                                                                           |
                                                                            |
//simenc: for blob-files info  [SUBIL CHECK: OK]                            |
type BFDescriptor struct{   <-----------------------------------------------|

	BlobFilePath    string // filepath of this blobfile
	Digest          digest.Digest
	DigestFilePath  string	// digest file path
	
	ServerIp			string // the server which stores this digest
}

add two more for distributed dedup

1. blob to server list







