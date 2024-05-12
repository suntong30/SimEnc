package redis

import (
	"errors"
	"fmt"
	"strconv"
	"github.com/docker/simenc"
	"github.com/docker/simenc/context"
	"github.com/docker/simenc/reference"
	"github.com/docker/simenc/registry/storage/cache"
	//	redis "github.com/garyburd/redigo/redis"
	digest "github.com/opencontainers/go-digest"
	//simenc
	//"encoding/json"
	// "net"

	//	"flag"
	//rejson "github.com/secondspass/go-rejson"
	//	"log"
	redis "github.com/gomodule/redigo/redis"
	//	"github.com/go-redis/redis"
	//	redisc "github.com/mna/redisc"
	redisgo "github.com/go-redis/redis"
	//"strings"
)

// redisBlobStatService provides an implementation of
// BlobDescriptorCacheProvider based on redis. Blob descriptors are stored in
// two parts. The first provide fast access to repository membership through a
// redis set for each repo. The second is a redis hash keyed by the digest of
// the layer, providing path, length and mediatype information. There is also
// a per-repository redis hash of the blob descriptor, allowing override of
// data. This is currently used to override the mediatype on a per-repository
// basis.
//
// Note that there is no implied relationship between these two caches. The
// layer may exist in one, both or none and the code must be written this way.

// HERE, we store dbNoBlobl and dbNoBFRecipe on a redis standalone
// we store dbNoFile on a redis cluster
var (
	dbNoBlob = 0
)

type redisBlobDescriptorService struct {

	pool *redis.Pool
	client *redisgo.ClusterClient
}

// NewRedisBlobDescriptorCacheProvider returns a new redis-based
// BlobDescriptorCacheProvider using the provided redis connection pool.
func NewRedisBlobDescriptorCacheProvider(pool *redis.Pool,client *redisgo.ClusterClient) cache.BlobDescriptorCacheProvider {

	return &redisBlobDescriptorService{
		pool: pool,
		client : client,
	}
}

// RepositoryScoped returns the scoped cache.
func (rbds *redisBlobDescriptorService) RepositoryScoped(repo string) (distribution.BlobDescriptorService, error) {
	
	// repo = "testrepo"
	
	if _, err := reference.ParseNormalizedNamed(repo); err != nil {
		return nil, err
	}

	return &repositoryScopedRedisBlobDescriptorService{
		repo:     repo,
		upstream: rbds,
	}, nil
}

// Stat retrieves the descriptor data from the redis hash entry.
func (rbds *redisBlobDescriptorService) Stat(ctx context.Context, dgst digest.Digest) (distribution.Descriptor, error) {
	//fmt.Printf("cur Stat is rbds *redisBlobDescriptorService %v\n",dgst)
	if err := dgst.Validate(); err != nil {
		return distribution.Descriptor{}, err
	}

	return rbds.stat(ctx, dgst)
	
}

func (rbds *redisBlobDescriptorService) Clear(ctx context.Context, dgst digest.Digest) error {
	if err := dgst.Validate(); err != nil {
		return err
	}

	conn := rbds.pool.Get()
	defer conn.Close()
	//simenc
	if _, err := conn.Do("SELECT", dbNoBlob); err != nil {
		return err
	}

	// Not atomic in redis <= 2.3
	reply, err := conn.Do("HDEL", rbds.blobDescriptorHashKey(dgst), "digest", "length", "mediatype")
	if err != nil {
		return err
	}

	if reply == 0 {
		return distribution.ErrBlobUnknown
	}

	return nil
}

// stat provides an internal stat call that takes a connection parameter. This
// allows some internal management of the connection scope.
func (rbds *redisBlobDescriptorService) stat(ctx context.Context,  dgst digest.Digest) (distribution.Descriptor, error) {


	
	/// simenc
	//fmt.Printf("cur stat is rbds *redisBlobDescriptorService %v\n",dgst)
	reply, err := rbds.client.HMGet(rbds.blobDescriptorHashKey(dgst), "digest", "size", "mediatype").Result()
	if err != nil {
		return distribution.Descriptor{}, err
	}
	if len(reply) < 3 || reply[0] == nil || reply[1] == nil { // don't care if mediatype is nil
		return distribution.Descriptor{}, distribution.ErrBlobUnknown
	}

	var desc distribution.Descriptor
	var tmpDig string
	var tmpSize string
	var tmpType string 

	if _, err := redis.Scan(reply, &tmpDig, &tmpSize, &tmpType); err != nil {
		fmt.Printf("redis.Scan(reply, &tmpDig, &tmpSize, &tmpType) error = %v \n",err)
		return distribution.Descriptor{}, err
	}
	desc.Digest = digest.Digest( tmpDig)

	desc.Size , _  = strconv.ParseInt(tmpSize,10,64)
	desc.MediaType = tmpType
	//fmt.Printf("cur stat is rbds *redisBlobDescriptorService finish with %v %v %v \n",tmpDig,tmpSize,tmpType)
	//fmt.Printf("cur stat is rbds *redisBlobDescriptorService finish with %v  \n",desc)
	return desc, nil
}

// SetDescriptor sets the descriptor data for the given digest using a redis
// hash. A hash is used here since we may store unrelated fields about a layer
// in the future.
func (rbds *redisBlobDescriptorService) SetDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.Descriptor) error {
	//fmt.Println("simenc : (rbds *redisBlobDescriptorService) SetDescriptor")
	if err := dgst.Validate(); err != nil {
		return err
	}

	if err := cache.ValidateDescriptor(desc); err != nil {
		return err
	}

	return rbds.setDescriptor(ctx, dgst, desc)
}

func (rbds *redisBlobDescriptorService) setDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.Descriptor) error {

	
	if _, err := rbds.client.HMSet(rbds.blobDescriptorHashKey(dgst),
	    map[string]interface{}{
		"digest": desc.Digest.String(),
		"size":   desc.Size,}).Result(); 
		err != nil {
			fmt.Println("simenc: HMSET rbds.blobDescriptorHashKey err")
		return err
	}


	if _, err := rbds.client.HSetNX(rbds.blobDescriptorHashKey(dgst),"mediatype", desc.MediaType).Result(); err != nil {
		fmt.Println("simenc: HSETNX rbds.blobDescriptorHashKey err")
		return err
	}

	return nil
}

func (rbds *redisBlobDescriptorService) blobDescriptorHashKey(dgst digest.Digest) string {
	return "blobs::" + dgst.String()
}

type repositoryScopedRedisBlobDescriptorService struct {
	repo     string
	upstream *redisBlobDescriptorService
}

var _ distribution.BlobDescriptorService = &repositoryScopedRedisBlobDescriptorService{}

// Stat ensures that the digest is a member of the specified repository and
// forwards the descriptor request to the global blob store. If the media type
// differs for the repository, we override it.
func (rsrbds *repositoryScopedRedisBlobDescriptorService) Stat(ctx context.Context, dgst digest.Digest) (distribution.Descriptor, error) {
	//fmt.Printf("cur Stat is rsrbds *repositoryScopedRedisBlobDescriptorService dgst is %v\n",dgst)
	if err := dgst.Validate(); err != nil {
		fmt.Printf("err := dgst.Validate()\n")
		return distribution.Descriptor{}, err
	}


	// Check membership to repository first
	member , err := rsrbds.upstream.client.SIsMember(rsrbds.repositoryBlobSetKey(rsrbds.repo), dgst.String()).Result()
	if err != nil {
		fmt.Printf("err := rsrbds.upstream.client.SIsMember\n")
		return distribution.Descriptor{}, err
	}

	if !member {
		fmt.Printf("! member err := distribution.ErrBlobUnknown\n")
		return distribution.Descriptor{}, distribution.ErrBlobUnknown
	}

	upstream, err := rsrbds.upstream.Stat(ctx,dgst)
	if err != nil {
		fmt.Printf("rsrbds.upstream.Stat error\n")
		return distribution.Descriptor{}, err
	}

	// We allow a per repository mediatype, let's look it up here.

	mediatype, err :=  rsrbds.upstream.client.HGet(rsrbds.blobDescriptorHashKey(dgst), "mediatype").Result()
	if err != nil {
		fmt.Printf("rsrbds.upstream.client.HGet\n")
		return distribution.Descriptor{}, err
	}

	if mediatype != "" {
		upstream.MediaType = mediatype
	}

	return upstream, nil
}

// Clear removes the descriptor from the cache and forwards to the upstream descriptor store
func (rsrbds *repositoryScopedRedisBlobDescriptorService) Clear(ctx context.Context, dgst digest.Digest) error {
	if err := dgst.Validate(); err != nil {
		return err
	}

	conn := rsrbds.upstream.pool.Get()
	defer conn.Close()
	//simenc
	if _, err := conn.Do("SELECT", dbNoBlob); err != nil {
		return err
	}

	// Check membership to repository first
	member, err := redis.Bool(conn.Do("SISMEMBER", rsrbds.repositoryBlobSetKey(rsrbds.repo), dgst))
	if err != nil {
		return err
	}

	if !member {
		return distribution.ErrBlobUnknown
	}

	return rsrbds.upstream.Clear(ctx, dgst)
}

func (rsrbds *repositoryScopedRedisBlobDescriptorService) SetDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.Descriptor) error {
	//fmt.Println("simenc : (rsrbds *repositoryScopedRedisBlobDescriptorService) SetDescriptor")
	if err := dgst.Validate(); err != nil {
		fmt.Println("dgst.Validate() error")
		return err
	}

	if err := cache.ValidateDescriptor(desc); err != nil {
		fmt.Println("cache.ValidateDescriptor(desc) error")
		return err
	}

	if dgst != desc.Digest {
		fmt.Println("dgst != desc.Digest")
		fmt.Println(dgst,desc.Digest)
		if dgst.Algorithm() == desc.Digest.Algorithm() {
			return fmt.Errorf("redis cache: digest for descriptors differ but algorthim does not: %q != %q", dgst, desc.Digest)
		}
	}
	return rsrbds.setDescriptor(ctx, dgst, desc)

}

func (rsrbds *repositoryScopedRedisBlobDescriptorService) setDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.Descriptor) error {
	fmt.Println(rsrbds.repositoryBlobSetKey(rsrbds.repo), dgst)

	if _, err := rsrbds.upstream.client.SAdd(rsrbds.repositoryBlobSetKey(rsrbds.repo), dgst.String()).Result(); err != nil {
		fmt.Println("simenc : rsrbds.upstream.client.Set err")
		fmt.Println(err)
		return err
	}

	if err := rsrbds.upstream.setDescriptor(ctx, dgst, desc); err != nil {
		fmt.Println("simenc : rsrbds.upstream.setDescriptor(ctx, conn, dgst, desc) err")
		fmt.Println(err)
		return err
	}

	// Override repository mediatype.
	//if _, err := conn.Do("HSET", rsrbds.blobDescriptorHashKey(dgst), "mediatype", desc.MediaType); err != nil {
	if _, err := rsrbds.upstream.client.HSet(rsrbds.blobDescriptorHashKey(dgst), "mediatype", desc.MediaType).Result(); err != nil {
		fmt.Println("simenc: conn.Do(HSET, rsrbds.blobDescriptorHashKey(dgst) err")
		fmt.Println(err)
		return err
	}

	// Also set the values for the primary descriptor, if they differ by
	// algorithm (ie sha256 vs sha512).
	if desc.Digest != "" && dgst != desc.Digest && dgst.Algorithm() != desc.Digest.Algorithm() {
		if err := rsrbds.setDescriptor(ctx, desc.Digest, desc); err != nil {
			fmt.Println("simenc: = rsrbds.setDescriptor(ctx, desc.Digest, desc) err")
			fmt.Println(err)
			return err
		}
	}
	//fmt.Println("simenc : (rsrbds *repositoryScopedRedisBlobDescriptorService) SetDescriptor final success")

	return nil

}

func (rsrbds *repositoryScopedRedisBlobDescriptorService) blobDescriptorHashKey(dgst digest.Digest) string {
	//return "testrepo::" + dgst.String() //"repository::" + rsrbds.repo + "::blobs::" + dgst.String()
	return "blob::" + dgst.String() //"repository::" + rsrbds.repo + "::blobs::" + dgst.String()
}

func (rsrbds *repositoryScopedRedisBlobDescriptorService) repositoryBlobSetKey(repo string) string {
	// return "testrepo::" // "repository::" + rsrbds.repo + "::blobs"
	return repo + "::"
}

//simenc: for deduplication
type redisDedupMetadataService struct {
	pool         *redis.Pool
	hostserverIp string
	cluster      *redisgo.ClusterClient
}

// NewRedisBlobDescriptorCacheProvider returns a new redis-based
// DedupMetadataServiceCacheProvider using the provided redis connection pool.
func NewRedisDedupMetadataServiceCacheProvider(pool *redis.Pool, cluster *redisgo.ClusterClient, host_ip string) cache.DedupMetadataServiceCacheProvider {
	fmt.Printf("simenc: hostip: " + host_ip + "\n")
	return &redisDedupMetadataService{
		pool:         pool,
		cluster:      cluster,
		hostserverIp: host_ip,
	}
}

//"files::sha256:7173b809ca12ec5dee4506cd86be934c4596dd234ee82c0662eac04a8c2c71dc"
func (rdms *redisDedupMetadataService) fileDescriptorHashKey(dgst digest.Digest) string {
	return "File::" + dgst.String()
}

var _ distribution.RedisDedupMetadataService = &redisDedupMetadataService{}

func (rdms *redisDedupMetadataService) StatFile(ctx context.Context, dgst digest.Digest) (distribution.FileDescriptor, error) {
	reply, err := rdms.cluster.Get(rdms.fileDescriptorHashKey(dgst)).Result()
	if err == redisgo.Nil {
		//		context.GetLogger(ctx).Debug("simenc: key %s doesnot exist", dgst.String())
		return distribution.FileDescriptor{}, err
	} else if err != nil {
		context.GetLogger(ctx).Errorf("simenc: redis cluster error for key %s", err)
		return distribution.FileDescriptor{}, err
	} else {
		var desc distribution.FileDescriptor
		if err = desc.UnmarshalBinary([]byte(reply)); err != nil {
			context.GetLogger(ctx).Errorf("simenc: redis cluster cannot UnmarshalBinary for key %s", err)
			return distribution.FileDescriptor{}, err
		} else {
			return desc, nil
		}
	}
}

//use redis as global lock service
func (rdms *redisDedupMetadataService) SetFileDescriptor(ctx context.Context, dgst digest.Digest, desc distribution.FileDescriptor) error {
	set, err := rdms.cluster.SetNX(rdms.fileDescriptorHashKey(dgst), &desc, 0).Result()
	if err != nil {
		context.GetLogger(ctx).Errorf("simenc: SetFileDescriptor redis cluster cannot set value for key %s", err)
		return err
	}
	if set == true {
		return nil
	} else {
		context.GetLogger(ctx).Warnf("simenc: key %s already exsist!", dgst.String())
		return errors.New("key already exsits")
	}
}

func (rdms *redisDedupMetadataService) LayerRecipeHashKey(dgst digest.Digest) string {
	return "Layer:Recipe::" + dgst.String()
}

func (rdms *redisDedupMetadataService) SliceRecipeHashKey(dgst digest.Digest, sip string) string {
	return "Slice:Recipe::" + dgst.String() + "::" + sip //rdms.hostserverIp
}

func (rdms *redisDedupMetadataService) StatLayerRecipe(ctx context.Context, dgst digest.Digest) (distribution.LayerRecipeDescriptor, error) {

	reply, err := rdms.cluster.Get(rdms.LayerRecipeHashKey(dgst)).Result()
	if err == redisgo.Nil {
		//		context.GetLogger(ctx).Debug("simenc: key %s doesnot exist", dgst.String())
		return distribution.LayerRecipeDescriptor{}, err
	} else if err != nil {
		context.GetLogger(ctx).Errorf("simenc: redis cluster error for key %s", err)
		return distribution.LayerRecipeDescriptor{}, err
	} else {
		var desc distribution.LayerRecipeDescriptor
		if err = desc.UnmarshalBinary([]byte(reply)); err != nil {
			context.GetLogger(ctx).Errorf("simenc: redis cluster cannot UnmarshalBinary for key %s", err)
			return distribution.LayerRecipeDescriptor{}, err
		} else {
			return desc, nil
		}
	}
}

func (rdms *redisDedupMetadataService) SetLayerRecipe(ctx context.Context, dgst digest.Digest, desc distribution.LayerRecipeDescriptor) error {

	err := rdms.cluster.SetNX(rdms.LayerRecipeHashKey(dgst), &desc, 0).Err()
	if err != nil {
		context.GetLogger(ctx).Errorf("simenc : SetLayerRecipe redis cluster cannot set value for key %s", err)
		return err
	}
	return nil
}

func (rdms *redisDedupMetadataService) StatSliceRecipe(ctx context.Context, dgst digest.Digest) (distribution.SliceRecipeDescriptor, error) {

	reply, err := rdms.cluster.Get(rdms.SliceRecipeHashKey(dgst, rdms.hostserverIp)).Result()
	if err == redisgo.Nil {
		//		context.GetLogger(ctx).Debug("simenc: key %s doesnot exist", dgst.String())
		return distribution.SliceRecipeDescriptor{}, err
	} else if err != nil {
		context.GetLogger(ctx).Errorf("simenc: redis cluster error for key %s", err)
		return distribution.SliceRecipeDescriptor{}, err
	} else {
		var desc distribution.SliceRecipeDescriptor
		if err = desc.UnmarshalBinary([]byte(reply)); err != nil {
			context.GetLogger(ctx).Errorf("simenc: redis cluster cannot UnmarshalBinary for key %s", err)
			return distribution.SliceRecipeDescriptor{}, err
		} else {
			return desc, nil
		}
	}
}

func (rdms *redisDedupMetadataService) SetSliceRecipe(ctx context.Context, dgst digest.Digest, desc distribution.SliceRecipeDescriptor, sip string) error {

	err := rdms.cluster.SetNX(rdms.SliceRecipeHashKey(dgst, sip), &desc, 0).Err()
	if err != nil {
		context.GetLogger(ctx).Errorf("simenc:SetSliceRecipe redis cluster cannot set value for key %s", err)
		return err
	}
	return nil
}

//metadataservice for rlmap and ulmap

func (rdms *redisDedupMetadataService) RLMapHashKey(reponame string) string {
	return "RLMap::" + reponame
}

func (rdms *redisDedupMetadataService) ULMapHashKey(usrname string) string {
	return "ULMap::" + usrname
}

func (rdms *redisDedupMetadataService) StatRLMapEntry(ctx context.Context, reponame string) (distribution.RLmapEntry, error) {

	reply, err := rdms.cluster.Get(rdms.RLMapHashKey(reponame)).Result()
	if err == redisgo.Nil {
		//		context.GetLogger(ctx).Debug("simenc: key %s doesnot exist", dgst.String())
		return distribution.RLmapEntry{}, err
	} else if err != nil {
		context.GetLogger(ctx).Errorf("simenc: redis cluster error for key %s", err)
		return distribution.RLmapEntry{}, err
	} else {
		var desc distribution.RLmapEntry
		if err = desc.UnmarshalBinary([]byte(reply)); err != nil {
			context.GetLogger(ctx).Errorf("simenc: redis cluster cannot UnmarshalBinary for key %s", err)
			return distribution.RLmapEntry{}, err
		} else {
			return desc, nil
		}
	}
}

func (rdms *redisDedupMetadataService) SetRLMapEntry(ctx context.Context, reponame string, desc distribution.RLmapEntry) error {

	//set slicerecipe
	err := rdms.cluster.Set(rdms.RLMapHashKey(reponame), &desc, 0).Err()
	if err != nil {
		context.GetLogger(ctx).Errorf("simenc: SetRLMapEntry redis cluster cannot set value for key %s", err)
		return err
	}
	return nil
}

func (rdms *redisDedupMetadataService) StatULMapEntry(ctx context.Context, usrname string) (distribution.ULmapEntry, error) {

	reply, err := rdms.cluster.Get(rdms.ULMapHashKey(usrname)).Result()
	if err == redisgo.Nil {
		//		context.GetLogger(ctx).Debug("simenc: key %s doesnot exist", dgst.String())
		return distribution.ULmapEntry{}, err
	} else if err != nil {
		context.GetLogger(ctx).Errorf("simenc: redis cluster error for key %s", err)
		return distribution.ULmapEntry{}, err
	} else {
		var desc distribution.ULmapEntry
		if err = desc.UnmarshalBinary([]byte(reply)); err != nil {
			context.GetLogger(ctx).Errorf("simenc: redis cluster cannot UnmarshalBinary for key %s", err)
			return distribution.ULmapEntry{}, err
		} else {
			return desc, nil
		}
	}
}

func (rdms *redisDedupMetadataService) SetULMapEntry(ctx context.Context, usrname string, desc distribution.ULmapEntry) error {

	//set slicerecipe
	err := rdms.cluster.Set(rdms.ULMapHashKey(usrname), &desc, 0).Err()
	if err != nil {
		context.GetLogger(ctx).Errorf("simenc : SetULMapEntry redis cluster cannot set value for key %s", err)
		return err
	}
	return nil
}
