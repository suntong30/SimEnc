// Package cache provides facilities to speed up access to the storage
// backend.
package cache

import (
	"fmt"

	"github.com/docker/simenc"
)

// BlobDescriptorCacheProvider provides repository scoped
// BlobDescriptorService cache instances and a global descriptor cache.
type BlobDescriptorCacheProvider interface {
	distribution.BlobDescriptorService

	RepositoryScoped(repo string) (distribution.BlobDescriptorService, error)
}

//simenc
type DedupMetadataServiceCacheProvider interface {
	distribution.RedisDedupMetadataService
}

// ValidateDescriptor provides a helper function to ensure that caches have
// common criteria for admitting descriptors.
func ValidateDescriptor(desc distribution.Descriptor) error {
	if err := desc.Digest.Validate(); err != nil {
		fmt.Println("simenc : desc.Digest.Validate error")
		return err
	}

	if desc.Size < 0 {
		return fmt.Errorf("cache: invalid length in descriptor: %v < 0", desc.Size)
	}

	if desc.MediaType == "" {
		return fmt.Errorf("cache: empty mediatype on descriptor: %v", desc)
	}

	return nil
}
