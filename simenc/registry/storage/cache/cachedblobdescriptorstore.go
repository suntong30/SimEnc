package cache

import (
	"github.com/docker/simenc"
	"github.com/docker/simenc/context"
	digest "github.com/opencontainers/go-digest"
	"fmt"

)

// Metrics is used to hold metric counters
// related to the number of times a cache was
// hit or missed.
type Metrics struct {
	Requests uint64
	Hits     uint64
	Misses   uint64
}

// MetricsTracker represents a metric tracker
// which simply counts the number of hits and misses.
type MetricsTracker interface {
	Hit()
	Miss()
	Metrics() Metrics
}

type cachedBlobStatter struct {
	cache         distribution.BlobDescriptorService
	metadatacache distribution.RedisDedupMetadataService
	backend       distribution.BlobDescriptorService
	tracker       MetricsTracker
}

// NewCachedBlobStatter creates a new statter which prefers a cache and
// falls back to a backend.
func NewCachedBlobStatter(cache distribution.BlobDescriptorService, backend distribution.BlobDescriptorService) distribution.BlobDescriptorService {
	return &cachedBlobStatter{
		cache:   cache,
		backend: backend,
	}
}

// NewCachedBlobStatter creates a new statter which prefers a cache and
// falls back to a backend.
func NewCachedBlobStatterWithMetadataCache(cache distribution.BlobDescriptorService, metadatacache distribution.RedisDedupMetadataService, backend distribution.BlobDescriptorService) distribution.BlobDescriptorService {
	return &cachedBlobStatter{
		cache:         cache,
		metadatacache: metadatacache,
		backend:       backend,
	}
}

// NewCachedBlobStatterWithMetrics creates a new statter which prefers a cache and
// falls back to a backend. Hits and misses will send to the tracker.
func NewCachedBlobStatterWithMetrics(cache distribution.BlobDescriptorService, backend distribution.BlobDescriptorService, tracker MetricsTracker) distribution.BlobStatter {
	return &cachedBlobStatter{
		cache:   cache,
		backend: backend,
		tracker: tracker,
	}
}

func (cbds *cachedBlobStatter) Stat(ctx context.Context, dgst digest.Digest) (distribution.Descriptor, error) {
	//first check cache
	//fmt.Printf("simenc: now dgst is %v \n ", dgst)
	desc, err := cbds.cache.Stat(ctx, dgst)

	if err != nil {
		fmt.Printf("Stat err = %v\n",err)
		if err != distribution.ErrBlobUnknown {
			context.GetLogger(ctx).Errorf("err != distribution.ErrBlobUnknown: %v", err)
		}
		context.GetLogger(ctx).Errorf("err  == other ")
		return distribution.Descriptor{}, err
		//goto fallback
	}
//fallback:
	if cbds.tracker != nil {
		cbds.tracker.Hit()
	}
	return desc, nil
}

func (cbds *cachedBlobStatter) Clear(ctx context.Context, dgst digest.Digest) error {
	err := cbds.cache.Clear(ctx, dgst)
	if err != nil {
		return err
	}

	err = cbds.backend.Clear(ctx, dgst)
	if err != nil {
		return err
	}
	return nil
}

func (cbds *cachedBlobStatter) SetDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.Descriptor) error {

	if err := cbds.cache.SetDescriptor(ctx, dgst, desc); err != nil {
		context.GetLogger(ctx).Errorf("SetDescriptor: error adding descriptor %v to cache: %v", desc.Digest, err)
	}
	return nil
}

//simenc
func (cbds *cachedBlobStatter) StatFile(ctx context.Context, dgst digest.Digest) (distribution.FileDescriptor, error) {
	desc, err := cbds.metadatacache.StatFile(ctx, dgst)
	if err != nil {
		if err != distribution.ErrBlobUnknown {
			context.GetLogger(ctx).Errorf("StatFile: error retrieving descriptor from cache: %v", err)
		}

		goto fallback
	}

	if cbds.tracker != nil {
		cbds.tracker.Hit()
	}
	return desc, nil
fallback:
	if cbds.tracker != nil {
		cbds.tracker.Miss()
	}

	return desc, err
}

func (cbds *cachedBlobStatter) SetFileDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.FileDescriptor) error {
	if err := cbds.metadatacache.SetFileDescriptor(ctx, dgst, desc); err != nil {
		context.GetLogger(ctx).Errorf("SetFileDescriptor: error adding file descriptor %v to cache: %v", desc.Digest, err)
	}
	return nil
}

func (cbds *cachedBlobStatter) StatLayerRecipe(ctx context.Context, dgst digest.Digest) (distribution.LayerRecipeDescriptor, error) {
	desc, err := cbds.metadatacache.StatLayerRecipe(ctx, dgst)
	if err != nil {
		if err != distribution.ErrBlobUnknown {
			context.GetLogger(ctx).Errorf("StatLayerRecipe: error retrieving descriptor from cache: %v", err)
		}

		goto fallback
	}

	if cbds.tracker != nil {
		cbds.tracker.Hit()
	}
	return desc, nil
fallback:
	if cbds.tracker != nil {
		cbds.tracker.Miss()
	}
	return desc, err
}

func (cbds *cachedBlobStatter) SetLayerRecipe(ctx context.Context, dgst digest.Digest, desc distribution.LayerRecipeDescriptor) error {
	if err := cbds.metadatacache.SetLayerRecipe(ctx, dgst, desc); err != nil {
		context.GetLogger(ctx).Errorf("SetLayerRecipe: error adding recipe descriptor %v to cache: %v", desc.Digest, err)
	}
	return nil
}

func (cbds *cachedBlobStatter) StatSliceRecipe(ctx context.Context, dgst digest.Digest) (distribution.SliceRecipeDescriptor, error) {
	desc, err := cbds.metadatacache.StatSliceRecipe(ctx, dgst)
	if err != nil {
		if err != distribution.ErrBlobUnknown {
			context.GetLogger(ctx).Errorf("StatSliceRecipe: error retrieving descriptor from cache: %v", err)

		}
		goto fallback
	}

	if cbds.tracker != nil {
		cbds.tracker.Hit()

	}
	return desc, nil
fallback:
	if cbds.tracker != nil {
		cbds.tracker.Miss()

	}
	return desc, err
}

func (cbds *cachedBlobStatter) SetSliceRecipe(ctx context.Context, dgst digest.Digest, desc distribution.SliceRecipeDescriptor, sip string) error {
	if err := cbds.metadatacache.SetSliceRecipe(ctx, dgst, desc, sip); err != nil {
		context.GetLogger(ctx).Errorf("SetSliceRecipe: error adding recipe descriptor %v to cache: %v", desc.Digest, err)
	}
	return nil
}

func (cbds *cachedBlobStatter) StatRLMapEntry(ctx context.Context, reponame string) (distribution.RLmapEntry, error) {
	desc, err := cbds.metadatacache.StatRLMapEntry(ctx, reponame)
	if err != nil {
		if err != distribution.ErrBlobUnknown {
			context.GetLogger(ctx).Errorf("StatRLMapEntry: error retrieving descriptor from cache: %v", err)

		}
		goto fallback
	}

	if cbds.tracker != nil {
		cbds.tracker.Hit()
	}
	return desc, nil
fallback:
	if cbds.tracker != nil {
		cbds.tracker.Miss()

	}
	return desc, err
}

func (cbds *cachedBlobStatter) SetRLMapEntry(ctx context.Context, reponame string, desc distribution.RLmapEntry) error {
	if err := cbds.metadatacache.SetRLMapEntry(ctx, reponame, desc); err != nil {
		context.GetLogger(ctx).Errorf("SetRLMapEntry: error adding descriptor %v to cache: %v", reponame, err)
	}
	return nil

}

func (cbds *cachedBlobStatter) StatULMapEntry(ctx context.Context, usrname string) (distribution.ULmapEntry, error) {
	desc, err := cbds.metadatacache.StatULMapEntry(ctx, usrname)
	if err != nil {
		if err != distribution.ErrBlobUnknown {
			context.GetLogger(ctx).Errorf("StatULMapEntry: error retrieving descriptor from cache: %v", err)
		}
		goto fallback
	}

	if cbds.tracker != nil {
		cbds.tracker.Hit()

	}
	return desc, nil
fallback:
	if cbds.tracker != nil {
		cbds.tracker.Miss()

	}
	return desc, err
}
func (cbds *cachedBlobStatter) SetULMapEntry(ctx context.Context, usrname string, desc distribution.ULmapEntry) error {
	if err := cbds.metadatacache.SetULMapEntry(ctx, usrname, desc); err != nil {
		context.GetLogger(ctx).Errorf("SetULMapEntry: error adding descriptor %v to cache: %v", usrname, err)
	}
	return nil
}
