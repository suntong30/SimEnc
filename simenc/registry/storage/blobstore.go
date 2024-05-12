package storage

import (
	"path"

	//log "github.com/Sirupsen/logrus"
	"github.com/docker/simenc"
	"github.com/docker/simenc/context"
	"github.com/docker/simenc/registry/storage/driver"
	digest "github.com/opencontainers/go-digest"
	"fmt"
)

// blobStore implements the read side of the blob store interface over a
// driver without enforcing per-repository membership. This object is
// intentionally a leaky abstraction, providing utility methods that support
// creating and traversing backend links.
type blobStore struct {
	driver  driver.StorageDriver
	statter distribution.BlobStatter
}

var _ distribution.BlobProvider = &blobStore{}

// Get implements the BlobReadService.Get call.
func (bs *blobStore) Get(ctx context.Context, dgst digest.Digest) ([]byte, error) {

	//log.Warnf("FAST: calling get content from appropriate driver %s", dgst)
	bp, err := bs.path(dgst)
	if err != nil {
		return nil, err
	}

	p, err := bs.driver.GetContent(ctx, bp)
	if err != nil {
		switch err.(type) {
		case driver.PathNotFoundError:
			return nil, distribution.ErrBlobUnknown
		}

		return nil, err
	}

	return p, err
}

func (bs *blobStore) Open(ctx context.Context, dgst digest.Digest) (distribution.ReadSeekCloser, error) {
	desc, err := bs.statter.Stat(ctx, dgst)
	if err != nil {
		return nil, err
	}

	path, err := bs.path(desc.Digest)
	if err != nil {
		return nil, err
	}

	return newFileReader(ctx, bs.driver, path, desc.Size)
}

// Put stores the content p in the blob store, calculating the digest. If the
// content is already present, only the digest will be returned. This should
// only be used for small objects, such as manifests. This implemented as a convenience for other Put implementations
func (bs *blobStore) Put(ctx context.Context, mediaType string, p []byte) (distribution.Descriptor, error) {

	//log.Warnf("FAST: calling put blobstore")
	dgst := digest.FromBytes(p)
	desc, err := bs.statter.Stat(ctx, dgst)
	if err == nil {
		// content already present
		return desc, nil
	} else if err != distribution.ErrBlobUnknown {
		context.GetLogger(ctx).Errorf("blobStore: error stating content (%v): %v", dgst, err)
		// real error, return it
		return distribution.Descriptor{}, err
	}

	bp, err := bs.path(dgst)
	if err != nil {
		return distribution.Descriptor{}, err
	}

	//log.Warnf("IBM: writing small object %s", mediaType)

	// TODO(stevvooe): Write out mediatype here, as well.
	return distribution.Descriptor{
		Size: int64(len(p)),

		// NOTE(stevvooe): The central blob store firewalls media types from
		// other users. The caller should look this up and override the value
		// for the specific repository.
		MediaType: "application/octet-stream",
		Digest:    dgst,
	}, bs.driver.PutContent(ctx, bp, p)
}

func (bs *blobStore) Enumerate(ctx context.Context, ingester func(dgst digest.Digest) error) error {

	specPath, err := pathFor(blobsPathSpec{})
	if err != nil {
		return err
	}

	err = Walk(ctx, bs.driver, specPath, func(fileInfo driver.FileInfo) error {
		// skip directories
		if fileInfo.IsDir() {
			return nil
		}

		currentPath := fileInfo.Path()
		// we only want to parse paths that end with /data
		_, fileName := path.Split(currentPath)
		if fileName != "data" {
			return nil
		}

		digest, err := digestFromPath(currentPath)
		if err != nil {
			return err
		}

		return ingester(digest)
	})
	return err
}

// path returns the canonical path for the blob identified by digest. The blob
// may or may not exist.
func (bs *blobStore) path(dgst digest.Digest) (string, error) {

	//log.Warnf("FAST: path blobstore")
	bp, err := pathFor(blobDataPathSpec{
		digest: dgst,
	})

	if err != nil {
		return "", err
	}

	return bp, nil
}

// link links the path to the provided digest by writing the digest into the
// target file. Caller must ensure that the blob actually exists.
func (bs *blobStore) link(ctx context.Context, path string, dgst digest.Digest) error {
	// The contents of the "link" file are the exact string contents of the
	// digest, which is specified in that package.
	//log.Warnf("FAST: link blobstore")
	return bs.driver.PutContent(ctx, path, []byte(dgst))
}

// readlink returns the linked digest at path.
func (bs *blobStore) readlink(ctx context.Context, path string) (digest.Digest, error) {

	//log.Warnf("FAST: readlink blobstore")
	content, err := bs.driver.GetContent(ctx, path)
	if err != nil {
		return "", err
	}

	linked, err := digest.Parse(string(content))
	if err != nil {
		return "", err
	}

	return linked, nil
}

// resolve reads the digest link at path and returns the blob store path.
func (bs *blobStore) resolve(ctx context.Context, path string) (string, error) {
	//log.Warnf("FAST: resolve blobstore")
	dgst, err := bs.readlink(ctx, path)
	if err != nil {
		return "", err
	}

	return bs.path(dgst)
}

type blobStatter struct {
	driver driver.StorageDriver
}

//type fileStatter struct{
//	driver driver.StorageDriver
//}

var _ distribution.BlobDescriptorService = &blobStatter{}

//var _ distribution.DedupMetadataService = &fileStatter{}

// Stat implements BlobStatter.Stat by returning the descriptor for the blob
// in the main blob store. If this method returns successfully, there is
// strong guarantee that the blob exists and is available.
func (bs *blobStatter) Stat(ctx context.Context, dgst digest.Digest) (distribution.Descriptor, error) {
	path, err := pathFor(blobDataPathSpec{
		digest: dgst,
	})
	var size int64 = 100
	if err != nil {
		return distribution.Descriptor{}, err
	}
	context.GetLogger(ctx).Infof("simenc: blobStatter: Stat, call bs.driver.stat")
	fi, err := bs.driver.Stat(ctx, path)
	if err != nil {
		switch err := err.(type) {
		case driver.PathNotFoundError:
			return distribution.Descriptor{}, distribution.ErrBlobUnknown
		default:
			return distribution.Descriptor{}, err
		}
	}

	if fi != nil {
		if fi.IsDir() {
			// NOTE(stevvooe): This represents a corruption situation. Somehow, we
			// calculated a blob path and then detected a directory. We log the
			// error and then error on the side of not knowing about the blob.
			context.GetLogger(ctx).Warnf("blob path should not be a directory: %q", path)
			return distribution.Descriptor{}, distribution.ErrBlobUnknown
		}
		size = fi.Size()
	}

	// TODO(stevvooe): Add method to resolve the mediatype. We can store and
	// cache a "global" media type for the blob, even if a specific repo has a
	// mediatype that overrides the main one.

	return distribution.Descriptor{
		Size: size,

		// NOTE(stevvooe): The central blob store firewalls media types from
		// other users. The caller should look this up and override the value
		// for the specific repository.
		MediaType: "application/octet-stream",
		Digest:    dgst,
	}, nil
}

func (bs *blobStatter) Clear(ctx context.Context, dgst digest.Digest) error {
	return distribution.ErrUnsupported
}

func (bs *blobStatter) SetDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.Descriptor) error {
	fmt.Println("distribution.ErrUnsupported")
	return distribution.ErrUnsupported
}

//// Stat implements BlobStatter.Stat by returning the descriptor for the blob
//// in the main blob store. If this method returns successfully, there is
//// strong guarantee that the blob exists and is available.
//func (bs *blobStatter) Stat(ctx context.Context, dgst digest.Digest) (distribution.Descriptor, error) {
//	path, err := pathFor(blobDataPathSpec{
//		digest: dgst,
//	})
//
//	if err != nil {
//		return distribution.Descriptor{}, err
//	}
//	context.GetLogger(ctx).Infof("simenc: blobStatter: Stat, call bs.driver.stat")
//	fi, err := bs.driver.Stat(ctx, path)
//	if err != nil {
//		switch err := err.(type) {
//		case driver.PathNotFoundError:
//			return distribution.Descriptor{}, distribution.ErrBlobUnknown
//		default:
//			return distribution.Descriptor{}, err
//		}
//	}
//
//	if fi.IsDir() {
//		// NOTE(stevvooe): This represents a corruption situation. Somehow, we
//		// calculated a blob path and then detected a directory. We log the
//		// error and then error on the side of not knowing about the blob.
//		context.GetLogger(ctx).Warnf("blob path should not be a directory: %q", path)
//		return distribution.Descriptor{}, distribution.ErrBlobUnknown
//	}
//
//	// TODO(stevvooe): Add method to resolve the mediatype. We can store and
//	// cache a "global" media type for the blob, even if a specific repo has a
//	// mediatype that overrides the main one.
//
//	return distribution.Descriptor{
//		Size: fi.Size(),
//
//		// NOTE(stevvooe): The central blob store firewalls media types from
//		// other users. The caller should look this up and override the value
//		// for the specific repository.
//		MediaType: "application/octet-stream",
//		Digest:    dgst,
//	}, nil
//}
//
//func (bs *blobStatter) Clear(ctx context.Context, dgst digest.Digest) error {
//	return distribution.ErrUnsupported
//}
//
//func (bs *blobStatter) SetDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.Descriptor) error {
//	return distribution.ErrUnsupported
//}
