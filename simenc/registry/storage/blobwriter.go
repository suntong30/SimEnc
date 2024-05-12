package storage

import (
	"archive/tar"
	"errors"
	"fmt"
	"io"
	"path"
	"sort"
	"strings"
	"time"
	
	"github.com/sirupsen/logrus"
	"github.com/docker/simenc"
	"github.com/docker/simenc/context"
	storagedriver "github.com/docker/simenc/registry/storage/driver"
	"github.com/docker/simenc/version"
	"github.com/panjf2000/ants"
	//simenc
	"os"
	"path/filepath"
	"sync"
	"github.com/docker/simenc/registry/storage/cache"
	"bytes"
	"io/ioutil"
	"net/http"
	"math/rand"

	redisgo "github.com/go-redis/redis"
	digest "github.com/opencontainers/go-digest"
	"os/exec"
	"log"
)
type Counter struct {
	count int
	mu    sync.Mutex
}

func (c *Counter) Increment() int {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.count++
	return c.count
}
func (c *Counter) GetCount() int {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.count
}
var counter Counter
//simenc: TODO LIST
//1. when storing to recipe, remove prefix-:/var/lib/registry/docker/registry/v2/blobs/sha256/ for redis space savings.
var uploger *log.Logger
func init() {
	file := "./" + time.Now().Format("2006-01-02") + "_push.txt"
	logFile, err := os.OpenFile(file, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0766)
	if err != nil {
			panic(err)
	}
	uploger = log.New(logFile, "[j]",log.LstdFlags | log.Lshortfile | log.LUTC) // 将文件设置为loger作为输出
	return
}
var (
	errResumableDigestNotAvailable = errors.New("resumable digest not available")
	//simenc
	algorithm = digest.Canonical
)

const (
	// digestSha256Empty is the canonical sha256 digest of empty data
	digestSha256Empty = "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
)

// blobWriter is used to control the various aspects of resumable
// blob upload.
type blobWriter struct {
	ctx       context.Context
	blobStore *linkedBlobStore

	id        string
	startedAt time.Time
	digester  digest.Digester
	written   int64 // track the contiguous write
	//simenc: filewriter
	fileWriter storagedriver.FileWriter
	driver     storagedriver.StorageDriver
	path       string

	resumableDigestEnabled bool
	committed              bool
}

var _ distribution.BlobWriter = &blobWriter{}

// ID returns the identifier for this upload.
func (bw *blobWriter) ID() string {
	return bw.id
}

func (bw *blobWriter) StartedAt() time.Time {
	return bw.startedAt
}

// Commit marks the upload as completed, returning a valid descriptor. The
// final size and digest are checked against the first descriptor provided.
func (bw *blobWriter) Commit(ctx context.Context, desc distribution.Descriptor) (distribution.Descriptor, error) {

	if err := bw.fileWriter.Commit(); err != nil {
		return distribution.Descriptor{}, err
	}

	bw.Close()
	desc.Size = bw.Size()

	canonical, err := bw.validateBlob(ctx, desc)
	if err != nil {
		return distribution.Descriptor{}, err
	}

	if err := bw.moveBlob(ctx, canonical); err != nil {
		return distribution.Descriptor{}, err
	}

	if err := bw.blobStore.linkBlob(ctx, canonical, desc.Digest); err != nil {
		return distribution.Descriptor{}, err
	}

	if err := bw.removeResources(ctx); err != nil {
		return distribution.Descriptor{}, err
	}

	err = bw.blobStore.blobAccessController.SetDescriptor(ctx, canonical.Digest, canonical)
	if err != nil {
		fmt.Println("(*blobWriter).Commit SetDescriptor err")
		return distribution.Descriptor{}, err
	}

	bw.committed = true
	fmt.Println("(*blobWriter).Commit success ")
	return canonical, nil
}

func GetGID() float64 {
	s1 := rand.NewSource(time.Now().UnixNano())
	r1 := rand.New(s1)
	return r1.Float64()
}

type Pair struct {
	first  interface{}
	second interface{}
}

/*
This function is used to forward put requests on to other registries

DEBU[0021] authorizing request                           go.version=go1.12 http.request.host="localhost:5000"
http.request.id=d8119314-5400-4477-bee9-ca4f2d808d52
http.request.method=PUT http.request.remoteaddr="172.17.0.1:55964"
http.request.uri="/v2/nnzhaocs/hello-world/blobs/uploads/736b54f9-38cb-4498-904c-a28b684c1a1c?_state=4zFr9emRBkO_Ij5iKV4y8GEtYwEpsMD3Z-M3x31jbRF7Ik5hbWUiOiJubnpoYW9jcy9oZWxsby13b3JsZCIsIlVVSUQiOiI3MzZiNTRmOS0zOGNiLTQ0OTgtOTA0Yy1hMjhiNjg0YzFhMWMiLCJPZmZzZXQiOjk3NywiU3RhcnRlZEF0IjoiMjAxOS0wNC0xNVQwMjoyODoxNloifQ%3D%3D&digest=sha256%3A1b930d010525941c1d56ec53b97bd057a67ae1865eebf042686d2a2d18271ced"
http.request.useragent="docker/18.09.3 go/go1.10.8 git-commit/774a1f4 kernel/3.10.0-693.11.6.el7_lustre.x86_64 os/linux arch/amd64 UpstreamClient(Docker-Client/18.09.3 \\(linux\\))" vars.name="nnzhaocs/hello-world" vars.uuid=736b54f9-38cb-4498-904c-a28b684c1a1c

ERRO[0109] response completed with error
err.code="digest invalid" err.detail="digest parsing failed"
err.message="provided digest did not match uploaded content" go.version=go1.12 http.request.host="192.168.0.215:5000"
http.request.id=4af0c21c-315e-42f9-b04d-a99f0a6e6eac
http.request.method=PUT http.request.remoteaddr="192.168.0.210:48070"
http.request.uri="/v2/forward_repo/blobs/uploads/d8a15122-8119-4290-a100-bd6ccd4ce747?_state=8Jv7qNOl5I8kKqVzT3sst5mCBPTBdu8kH8v2Wzhvt6N7Ik5hbWUiOiJmb3J3YXJkX3JlcG8iLCJVVUlEIjoiZDhhMTUxMjItODExOS00MjkwLWExMDAtYmQ2Y2NkNGNlNzQ3IiwiT2Zmc2V0IjowLCJTdGFydGVkQXQiOiIyMDE5LTA0LTE1VDAyOjI4OjE2LjA5NTIzNTI2N1oifQ%3D%3D&digest=sha256%3Asha256:729a6da29d6e10228688fc0cf3e943068b459ef6f168afbbd2d3d44ee0f2fd01"
http.request.useragent="Go-http-client/1.1" http.response.contenttype="application/json; charset=utf-8" http.response.duration=9.048157ms
http.response.status=400 http.response.written=131 vars.name="forward_repo" vars.uuid=d8a15122-8119-4290-a100-bd6ccd4ce747
*/
func (bw *blobWriter) ForwardToRegistry(ctx context.Context, bss []byte, server string) error {

	regname := server

	var regnamebuffer bytes.Buffer
	regnamebuffer.WriteString(regname)
	regnamebuffer.WriteString(":5000")
	regname = regnamebuffer.String()
	fmt.Printf("simenc: ForwardToRegistry forwarding to %s \n", regname)

	var buffer bytes.Buffer

	bytesreader := bytes.NewReader(bss)
	digestFn := algorithm.FromReader
	dgst, err := digestFn(bytesreader)
	if err != nil {
		fmt.Printf("simenc: ForwardToRegistry compute dgst error: %v \n", err)
		return err
	}

	dgststring := dgst.String()
	dgststring = strings.SplitN(dgststring, "sha256:", 2)[1]
	url := buffer.String()

	//let's skip head request

	buffer.Reset()
	buffer.WriteString("http://")
	buffer.WriteString(regname)
	buffer.WriteString("/v2/forward_repo/forward_repo/blobs/uploads/")
	url = buffer.String()

	fmt.Printf("simenc: ForwardToRegistry POST URL: %s \n", url)
	post, err := http.Post(url, "*/*", nil)
	if err != nil {
		fmt.Printf("simenc: ForwardToRegistry POST URL: %s, err %s \n", url, err)
		return err
	}
	post.Body.Close()

	location := post.Header.Get("location")
	buffer.Reset()
	buffer.WriteString(location)
	buffer.WriteString("&digest=sha256%3A")
	buffer.WriteString(dgststring)
	url = buffer.String()

	bytesreader = bytes.NewReader(bss)
	request, err := http.NewRequest("PUT", url, bytesreader)
	if err != nil {
		fmt.Printf("simenc: ForwardToRegistry PUT URL %s, err %s \n", url, err)
		return err
	}

	request.ContentLength = int64(len(bss))
	client := &http.Client{}

	put, err := client.Do(request)
	if err != nil {
		fmt.Printf("simenc: ForwardToRegistry PUT URL: %s, err %s \n", url, err)
		return err
	}

	fmt.Printf("%s returned status code %d \n", regname, put.StatusCode)
	if put.StatusCode < 200 || put.StatusCode > 299 {
		return errors.New("put unique files to other servers, failed")
	}

	put.Body.Close()

	return nil
}

func packUniqFile(i interface{}) {

	task, ok := i.(*Task)
	if !ok {
		fmt.Println(ok)
		return
	}
	newsrc := task.Src
	desc := task.Desc
	//reg := task.Reg
	tf := task.Tf

	var contents *[]byte

	//	start := time.Now()

	var _, err = os.Stat(newsrc)
	if os.IsNotExist(err) {
		fmt.Printf("simenc: dedup src file %v: %v\n", newsrc, err)
		return
	}

	bfss, err := ioutil.ReadFile(newsrc)
	if err != nil {
		fmt.Printf("simenc: dedup read file %s generated error: %v\n", desc, err)
		return
	} else {
		contents = &bfss
	}

	//size
	_, err = addToTarFile(tf, desc, *contents)
	if err != nil {
		fmt.Printf("simenc: dedup desc file %s generated error: %v\n", desc, err)
		return
	}

	//	DurationFCP := time.Since(start).Seconds()
	//	fmt.Printf("simenc: dedup wrote %d bytes to file %s duration: %v\n", size, desc, DurationFCP)
	return
}

// prepare forward files to other servers/registries
// FIRST, READ "/docker/registry/v2/blobs/sha256/1b/
// 1b930d010525941c1d56ec53b97bd057a67ae1865eebf042686d2a2d18271ced/
// diff/8b6566f585bad55b6fb9efb1dc1b6532fd08bb1796b4b42a3050aacb961f1f3f"
// SECOND, store in "/docker/registry/v2/mv_tmp_serverfiles/192.168.0.200/tmp_dir/
// NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/1b/
// 1b930d010525941c1d56ec53b97bd057a67ae1865eebf042686d2a2d18271ced/diff/8b6566f585bad55b6fb9efb1dc1b6532fd08bb1796b4b42a3050aacb961f1f3f"
// THEN, compress as gizp files "/var/lib/registry/docker/registry/v2/mv_tmp_serverfiles/192.168.0.200/mv_tar.tar.gz"

func (bw *blobWriter) PrepareAndForward(ctx context.Context, serverForwardMap map[string][]string, fwg *sync.WaitGroup) error {

	//start := time.Now()
	for server, fpathlst := range serverForwardMap {
		fwg.Add(1)
		context.GetLogger(ctx).Debugf("simenc: serverForwardMap: [%s]=>%", server, fpathlst)
		go func(server string, fpathlst []string, fwg *sync.WaitGroup) {
			defer fwg.Done()
			var wg sync.WaitGroup
			var buf bytes.Buffer
			var compressbuf bytes.Buffer

			fcnt := len(fpathlst)
			if fcnt > 100 {
				fcnt = 100
			}
			antp, _ := ants.NewPoolWithFunc(fcnt, func(i interface{}) {
				packUniqFile(i)
				wg.Done()
			})
			defer antp.Release()

			tw := tar.NewWriter(&buf)
			tf := &TarFile{
				Tw: tw,
			}

			for _, fpath := range fpathlst {

				destfpath := path.Join("simenc_NO_NEED_TO_DEDUP_THIS_TARBALL", strings.TrimPrefix(fpath, "/home/simenc/docker_v2"))
				wg.Add(1)
				antp.Invoke(&Task{
					Src:  fpath, //sfdescriptor.FilePath, //strings.TrimPrefix(bfdescriptor.BlobFilePath, "/var/lib/registry"),
					Desc: destfpath,
					Reg:  bw.blobStore.registry,
					Tf:   tf,
				})
			}
			wg.Wait()

			if err := tw.Close(); err != nil {
				fmt.Printf("simenc: cannot close tar file for server: %s \n", server)
			}
			//DurationCP := time.Since(start).Seconds()

			bss := pgzipTarFile(&buf, &compressbuf, bw.blobStore.registry.compr_level)
			_ = bw.ForwardToRegistry(ctx, bss, server)

		}(server, fpathlst, fwg)
	}
	return nil
}

//simenc: after finishing commit, start do deduplication
//TODO delete tarball
//type BFmap map[digest.Digest][]distribution.FileDescriptor

/*
NO_NEED_TO_DEDUP_THIS_TARBALL/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/1b/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/1b/
1b930d010525941c1d56ec53b97bd057a67ae1865eebf042686d2a2d18271ced/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/1b/
1b930d010525941c1d56ec53b97bd057a67ae1865eebf042686d2a2d18271ced/diff/
NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/1b/
1b930d010525941c1d56ec53b97bd057a67ae1865eebf042686d2a2d18271ced/diff/
8b6566f585bad55b6fb9efb1dc1b6532fd08bb1796b4b42a3050aacb961f1f3f
*/

func checkNeedDedupOrNot(unpackPath string) (bool, error) {

	files, err := ioutil.ReadDir(unpackPath)
	if err != nil {
		fmt.Printf("simenc: %s, cannot read this tar file, error: %v \n", err)
		return true, nil
	}

	for _, f := range files {
		fmatch, _ := path.Match("NO_NEED_TO_DEDUP_THIS_TARBALL", f.Name())
		if fmatch {
			fmt.Printf("simenc: NO_NEED_TO_DEDUP_THIS_TARBALL: %s \n", f.Name())
			/*
			 /home/simenc/dockerimages/layers
			 /docker/registry/v2/blobs/sha256/07/078bb24d9ee4ddf90f349d0b63004d3ac6897dae28dd37cc8ae97a0306e6aa33/
			 diff/NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/1b/1b930d010525941c1d56ec53b97bd057a67ae1865eebf042686d2a2d18271ced/diff
			*/

			files, err := ioutil.ReadDir(path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/"))
			if err != nil {
				fmt.Printf("simenc: %s, cannot read this unpackpath file: error: %s \n",
					path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/"), err)
				return false, err
			}
			for _, f := range files {
				fmt.Printf("simenc: find a layer subdir: %s \n", f.Name())
				if _, err := os.Stat(path.Join("/home/simenc/docker_v2/docker/registry/v2/blobs/sha256/", f.Name())); err == nil {
					//path exists
					//get next level directories
					fds, err := ioutil.ReadDir(path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/", f.Name()))
					if err != nil {
						fmt.Printf("simenc: %s, cannot read this unpackpath filepath: %s: error: %v \n",
							path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/", f.Name()), err)
						return false, err
					}
					for _, fd := range fds {
						if err = os.Rename(path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/", f.Name(), fd.Name()),
							path.Join("/home/simenc/docker_v2/docker/registry/v2/blobs/sha256/", f.Name(), fd.Name())); err != nil {
							fmt.Printf("simenc: %s, cannot rename this unpackpath filepath: error %s, probaly a deuplicate file, Ignore it.\n",
								path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/", f.Name(), fd.Name()), err)
							//return err
						}
					}

				} else {
					if err = os.Rename(path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/", f.Name()),
						path.Join("/home/simenc/docker_v2/docker/registry/v2/blobs/sha256/", f.Name())); err != nil {
						fmt.Printf("simenc: %s, cannot rename this unpackpath filepath: error %s, probaly a deuplicate file, Ignore it \n",
							path.Join(unpackPath, "NO_NEED_TO_DEDUP_THIS_TARBALL/docker/registry/v2/blobs/sha256/", f.Name()), err)
						//						return err
					}
				}
			}
			return false, nil
		}
		return true, nil
	}
	return true, nil

}
func IsEmpty(name string) (bool, error) {
	f, err := os.Open(name)
	if err != nil {
		return false, err
	}
	defer f.Close()

	_, err = f.Readdirnames(1) // Or f.Readdir(1)
	if err == io.EOF {
		return true, nil
	}
	return false, err // Either not empty or error, suits both cases
}

func (bw *blobWriter) Dedup(
	reqtype, reponame, usrname string,
	desc distribution.Descriptor) error {
	
	fmt.Printf(" Dedup: request type: %s, for repo (%s) and usr (%s) with dgst (%s)\n", reqtype, reponame, usrname, desc.Digest.String())
	blobPath, err := PathFor(BlobDataPathSpec{
		Digest: desc.Digest,
	})

	layerPath := path.Join("/home/simenc/docker_v2", blobPath)
	lfile, err := os.Open(layerPath)
	if err != nil {
		fmt.Printf("simenc: cannot open layer file =>%s\n", layerPath)
		return err

	}
	defer lfile.Close()

	stat, err := lfile.Stat()
	if err != nil {
		return err
	}

	comressSize := stat.Size()

	bss, err := ioutil.ReadFile(layerPath)
	if err != nil {
		fmt.Printf("simenc: cannot open layer file =>%s\n", layerPath)
		return err
	}

	ctx := context.WithVersion(context.Background(), version.Version)


	//then update RLMap ****
	//start deduplication,
	size := len(bss)
	if size == 0  {
		context.GetLogger(ctx).Warnf("size := len(bss) = 0 %s ",layerPath)
		return nil
	}

	rlmapentry, err := bw.blobStore.registry.metadataService.StatRLMapEntry(ctx, reponame)
	if err == nil {
		// 仓库存在，可根据Bmode进行调整

		// layer_num := len(rlmapentry.Dgstmap)
	
		// if layer_num >= 1 {
		// 	uploger.Printf("Mode-B1 return\n")
		// 	return nil
		// }

		// if layer_num >= 2{
		// 	uploger.Printf("Mode-B2 return\n")
		// 	return nil
		// }

		// if layer_num >= 3{
		// 	uploger.Printf("Mode-B3 return\n")
		// 	return nil
		// }
		
		if _, ok := rlmapentry.Dgstmap[desc.Digest]; ok {
			return nil
		} else {
			//add layer to repo
			rlmapentry.Dgstmap[desc.Digest] = 1
			err1 := bw.blobStore.registry.metadataService.SetRLMapEntry(ctx, reponame, rlmapentry)
			if err1 != nil {
				return err1
			}
		}
	} else {
		//not exisit
		dgstmap := make(map[digest.Digest]int64)
		dgstmap[desc.Digest] = 1
		rlmapentry = distribution.RLmapEntry{
			Dgstmap: dgstmap,
		}
		err1 := bw.blobStore.registry.metadataService.SetRLMapEntry(ctx, reponame, rlmapentry)
		if err1 != nil {
			return err1
		}
	}
	do_partial := true

	// Used only when compared with the duphunter experiment
	// if strings.HasSuffix(reponame, "_nodedup"){
	// 	uploger.Printf("NO%d_skip_dedup layer_path:%s  size:%d \n" , counter.Increment() ,layerPath ,comressSize)
	// 	return nil
	// }

	unpackPath := layerPath[ : len(layerPath) - 5 ]  + "/unpack"

	if os.MkdirAll(unpackPath, 0777) != nil {
		return err
	}
	start := time.Now()
	if(do_partial){
		// start partial
		command := "./decode"
		partialFileName := layerPath + "_partial"
		args := []string{layerPath , partialFileName}
		cmd := exec.Command(command, args...)
		defer os.Remove(partialFileName)
		_, err = cmd.CombinedOutput()
		if err != nil {
			fmt.Println("Error executing command:", err)
			os.RemoveAll(unpackPath)
			return err
		}
		serr := splitFileChunk(partialFileName , unpackPath)
		if serr != nil{
			fmt.Println("Error splitFileChunk(partialFileName , unpackPath):", serr)
			os.RemoveAll(unpackPath)
			return err
		}
		
	}else{
		// duphunter's processing logic
		command := "tar"
		args := []string{"xzf", layerPath , "-C", unpackPath}
		cmd := exec.Command(command, args...)
		_, err = cmd.CombinedOutput()
		if err != nil {
			uploger.Printf("NO%d_Error : with:%v  layer_path:%s  dist:%v \n", counter.Increment() , err , layerPath ,desc.Digest)
			sudocmd := exec.Command("sudo", "-S", "rm", "-rf", unpackPath)
			sudocmd.Stdin = strings.NewReader("1\n")
			_ , _ = sudocmd.CombinedOutput()
			return err
		}

	}
	
		cmdTime := time.Since(start).Seconds()
		var DurationRDF, DurationSRM, _ , dirSize, errmes, isdedup, someWrong = 
		bw.doDedup(ctx, distribution.Descriptor{ Digest: desc.Digest,} ,unpackPath , comressSize, do_partial)
		if(!someWrong){
			// everything is OK
			uploger.Printf("NO%d_Success_dedup_%t checkFileFp:%.2f SetRecipe:%.2f doCmdTime:%.2f dirSize:%d  layer_path:%s layer_size:%d\n",
			counter.Increment(), isdedup, DurationRDF, DurationSRM, cmdTime, dirSize, layerPath ,comressSize)
			if do_partial {
				if isdedup {
					//partial 使用一部分数据进行预热 并且成功dedup
					os.Remove(layerPath)
				}else{
					//select 模式中不去做partial 需要保留原始layer
					os.RemoveAll(unpackPath)
					diffFolderPath :=  layerPath[ : len(layerPath) - 5 ]  + "/diff"
					os.RemoveAll(diffFolderPath)
				}
			}else{
				os.Remove(layerPath)
			}
		}else{
			uploger.Printf("NO%d_Error err:%v checkFileFp:%.2f SetRecipe:%.2f doCmdTime:%.2f dirSize:%d layer_path:%s layer_size:%d\n",
			counter.Increment(),  errmes, DurationRDF, DurationSRM, cmdTime, dirSize, layerPath ,comressSize)
			rerr := os.RemoveAll(unpackPath)
			//常见错误为没有权限删除 ， 可以考虑切换到sudo执行删除命令
			if rerr != nil{
				// 创建带有 sudo 的命令
				sudocmd := exec.Command("sudo", "-S", "rm", "-rf", unpackPath)
				// 设置 sudo 密码
				sudocmd.Stdin = strings.NewReader("1\n")
				// 执行命令并检查错误
				_, _ = sudocmd.CombinedOutput()
			}
			diffFolderPath :=  layerPath[ : len(layerPath) - 5 ]  + "/diff"
			os.RemoveAll(diffFolderPath)
		}
	return nil
}
func (bw *blobWriter) CheckPartialDedup(unpackPath string , ctx context.Context, serverIp string, db cache.DedupMetadataServiceCacheProvider,
	nodistributedfiles *[]distribution.FileDescriptor,
	slices map[string][]distribution.FileDescriptor,
	sliceSizeMap map[string]int64,
	dirSize *int64,
	fcnt *int64)error{
	dir, _ := os.ReadDir(unpackPath)
	var count int64 
	count = 0 
	for _ , _ = range dir {
		count++
	}
	*fcnt = count
	var no int64
	for no = 1 ; no <= count;no++{
		fpath := fmt.Sprintf("%s/%d",unpackPath ,no)
		fp, err := os.Open(fpath)
		if err != nil {
			fmt.Printf("j : %v",err)
		}
		defer fp.Close()
		stat, err := fp.Stat()
		if err != nil {
			fmt.Printf("j : %v",err)
		}
		fsize := stat.Size()
		*dirSize += fsize
		
		digestFn := algorithm.FromReader
		dgst, err := digestFn(fp)
		if err != nil{
			return err
		}
		des, err := db.StatFile(ctx, dgst)
		// 如果Redis中不存在该键，err将会是redisgo.Nil
		// 如果解码成功，函数将返回解码后的文件描述符和nil，表示成功获取文件的元数据信息。
		if err == nil {
			//命中存在的文件,删除当前文件
			err := os.Remove(fpath)
			if err != nil {
				return err
			}
			slices[des.HostServerIp] = append(slices[des.HostServerIp], des)
			sliceSizeMap[des.HostServerIp] += fsize
			continue
		} else if err != redisgo.Nil {
			return err
		}

		//to avoid invalid filepath, rename the original file to .../diff/uniquefiles/randomid/digest
		diffpath := strings.SplitN(fpath, "unpack", 2)[0]
		gid := GetGID()
		tmp_dir := fmt.Sprintf("%f", gid)
		reFPath := path.Join(diffpath, "/diff/uniquefiles", tmp_dir, strings.SplitN(fpath, "unpack/", 2)[1])

		newdir := path.Join(diffpath, "/diff/uniquefiles", tmp_dir)
		if os.MkdirAll(newdir, 0777) != nil {
			return err
		}
		err = os.Rename(fpath, reFPath)
		if err != nil {
			return err
		}
		fpath = reFPath
		des = distribution.FileDescriptor{
			Digest:   dgst,
			FilePath: fpath,
			Size:     fsize,
		}
		*nodistributedfiles = append(*nodistributedfiles, des)
	}
	return nil

}

func (bw *blobWriter) doDedup(ctx context.Context, desc distribution.Descriptor, unpackPath string, comressSize int64, doPartial bool) (float64, float64, float64, int64, error, bool, bool) {

	var nodistributedfiles []distribution.FileDescriptor
	slices := make(map[string][]distribution.FileDescriptor)
	serverForwardMap := make(map[string][]string)
	sliceSizeMap := make(map[string]int64)
	//	serverStoreCntMap := make(map[string]int)

	var dirSize int64 = 0
	var fcnt int64 = 0

	var DurationRDF float64 = 0.0
	var DurationSRM float64 = 0.0
	var DurationSFT float64 = 0.0

	isdedup := false
	someWrong := false

	for _, sip := range bw.blobStore.registry.servers {
		sliceSizeMap[sip] = 0
	}

	var err error
	start := time.Now()
	if doPartial{
		err = bw.CheckPartialDedup(unpackPath,ctx, bw.blobStore.registry.hostserverIp, bw.blobStore.registry.metadataService,
			&nodistributedfiles,
			slices,
			sliceSizeMap,
			&dirSize,
			&fcnt)
	}else{
		err = filepath.Walk(unpackPath, bw.CheckDuplicate(ctx, bw.blobStore.registry.hostserverIp, bw.blobStore.registry.metadataService,
			&nodistributedfiles,
			slices,
			sliceSizeMap,
			&dirSize,
			&fcnt))
	}
	DurationRDF = time.Since(start).Seconds()
	if err != nil {
		someWrong = true;   //duphunter很可能会失败，代表需要保留原始文件
	}

	if fcnt == 0 { 
		//set layer recipe empty
		des := distribution.LayerRecipeDescriptor{
			Digest:            desc.Digest,
			MasterIp:          bw.blobStore.registry.hostserverIp, //bw.blobStore.registry.hostserverIp,
			HostServerIps:     []string{},                         //RemoveDuplicateIpsFromIps(serverIps),
			SliceSizeMap:      map[string]int64{},
			UncompressionSize: dirSize,
			CompressionSize:   comressSize,
			Fcnt:              fcnt,
		}
		start = time.Now()
		err = bw.blobStore.registry.metadataService.SetLayerRecipe(ctx, desc.Digest, des)
		if err != nil {
			//cleanup everything; omitted
			return DurationRDF, DurationSRM, DurationSFT, dirSize, err, isdedup, someWrong
		}
		DurationSRM := time.Since(start).Seconds()

		return DurationRDF, DurationSRM, DurationSFT, dirSize, nil, isdedup, someWrong
	}
	isdedup = !someWrong
	
	//F-mode 进行预热
	if(isdedup && doPartial && counter.GetCount() > 200 && float64(dirSize) > float64(comressSize) * 1.1){
		return DurationRDF, DurationSRM, DurationSFT, dirSize, nil, false, false
	}

	// H-mode
	// if(isdedup && doPartial && float64(dirSize) > float64(comressSize) * 1.1){
	// 	return DurationRDF, DurationSRM, DurationSFT, dirSize, nil, false, false
	// }

	// F-mode test
	// if(isdedup && doPartial && counter.GetCount() > 300 && float64(dirSize) > float64(comressSize) * 1.1){
	// 	return DurationRDF, DurationSRM, DurationSFT, dirSize, nil, false, false
	// }

	bw.Uniqdistribution(ctx, dirSize, fcnt, nodistributedfiles, sliceSizeMap, slices, serverForwardMap)

	var hostserverIps []string
	sliceSizeMapnew := make(map[string]int64)
	var maxsize int64 = 0
	var masterIp string
	for sip := range slices {
		if sliceSizeMap[sip] > 0 {
			hostserverIps = append(hostserverIps, sip)
			sliceSizeMapnew[sip] = sliceSizeMap[sip]
			if maxsize < sliceSizeMapnew[sip] {
				masterIp = sip
				maxsize = sliceSizeMapnew[sip]
			}
		}
	}

	start = time.Now()
	for sip, files := range slices {
		if 0 < len(files) && 0 < sliceSizeMap[sip] {
			des := distribution.SliceRecipeDescriptor{
				Digest:       desc.Digest,
				HostServerIp: sip,
				Files:        files,
				SliceSize:    sliceSizeMap[sip],
				Fcnt:         int64(len(files)),
			}
			err = bw.blobStore.registry.metadataService.SetSliceRecipe(ctx, desc.Digest, des, sip)
			if err != nil {
				//cleanup slice // omitted
				return DurationRDF, DurationSRM, DurationSFT, dirSize, err, isdedup, someWrong
			}
		}
	}

	des := distribution.LayerRecipeDescriptor{
		Digest:            desc.Digest,
		MasterIp:          masterIp,      //bw.blobStore.registry.hostserverIp,
		HostServerIps:     hostserverIps, //RemoveDuplicateIpsFromIps(serverIps),
		SliceSizeMap:      sliceSizeMapnew,
		UncompressionSize: dirSize,
		Compressratio:     float64(dirSize) / float64(comressSize),
		CompressionSize:   comressSize,
		Fcnt:              fcnt,
	}

	err = bw.blobStore.registry.metadataService.SetLayerRecipe(ctx, desc.Digest, des)
	if err != nil {
		//cleanup everything; omitted
		fmt.Printf("simenc: bw.blobStore.registry.metadataService.SetLayerRecipe error  %v \n", err)
		return DurationRDF, DurationSRM, DurationSFT, dirSize, err, isdedup, someWrong
	}

	DurationSRM = time.Since(start).Seconds()

//	if len(serverForwardMap) == 0 {
	return DurationRDF, DurationSRM, DurationSFT, dirSize, nil, isdedup, someWrong
//	}


	// isforward = true
	// // let's do forwarding *****
	// start = time.Now()
	// var wg sync.WaitGroup
	// _ = bw.PrepareAndForward(ctx, serverForwardMap, &wg)
	// //	go func(){
	// //		wg.Wait()
	// //	}()
	// DurationSFT = time.Since(start).Seconds()

	// return DurationRDF, DurationSRM, DurationSFT, dirSize, nil, isdedup, isforward
}

/*
simenc check dedup
no lock
and skip empty file
filepath.Walk(unpackPath, bw.CheckDuplicate(ctx, bw.blobStore.registry.hostserverIp, bw.blobStore.registry.metadataService,
		&nodistributedfiles,
		slices,
		sliceSizeMap,
		serverStoreCntMap,
		&dirSize,
		&fcnt))
*/
func (bw *blobWriter) CheckDuplicate(ctx context.Context, serverIp string, db cache.DedupMetadataServiceCacheProvider,
	nodistributedfiles *[]distribution.FileDescriptor,
	slices map[string][]distribution.FileDescriptor,
	sliceSizeMap map[string]int64,
	dirSize *int64,
	fcnt *int64) filepath.WalkFunc {

	return func(fpath string, info os.FileInfo, err error) error {
		if err != nil {
			fmt.Printf("simenc: error: %v\n", err)
			return err
		}

		if !(info.Mode().IsRegular()) {
			return nil
		}

		fp, err := os.Open(fpath)
		if err != nil {
			fmt.Printf("simenc: error os.Open(fpath): %v\n", err)
			return nil
		}
		defer fp.Close()

		stat, err := fp.Stat()
		if err != nil {
			fmt.Printf("simenc: error fp.Stat(): %v\n", err)
			return nil
		}

		fsize := stat.Size()
		if fsize <= 0 {
			//			fmt.Printf("simenc: empty file")
			return nil
		}

		*dirSize += fsize
		*fcnt += 1

		digestFn := algorithm.FromReader
		dgst, err := digestFn(fp)
		if err != nil {
			fmt.Printf("simenc: %s: error %v \n", fpath, err)
			return err
		}

		des, err := db.StatFile(ctx, dgst)
		// 如果Redis中不存在该键，err将会是redisgo.Nil
		// 如果解码成功，函数将返回解码后的文件描述符和nil，表示成功获取文件的元数据信息。
		if err == nil {
			// file content already stored
			//first update layer metadata
			//then delete this file
			err := os.Remove(fpath)
			if err != nil {
				fmt.Printf("simenc: error: %v\n", err)
				return err
			}

			slices[des.HostServerIp] = append(slices[des.HostServerIp], des)
			sliceSizeMap[des.HostServerIp] += fsize

			return nil
		} else if err != redisgo.Nil {
			fmt.Printf("simenc: checkDuplicate: error stating file (%v): %v \n", dgst, err)
			return err
		}
		// fmt.Printf("simenc: !!!!!重命名的文件!!!! %s \n",fpath)
		//to avoid invalid filepath, rename the original file to .../diff/uniquefiles/randomid/digest
		diffpath := strings.SplitN(fpath, "unpack", 2)[0]
		gid := GetGID()
		tmp_dir := fmt.Sprintf("%f", gid)
		reFPath := path.Join(diffpath, "/diff/uniquefiles", tmp_dir, strings.SplitN(fpath, "unpack/", 2)[1])

		newdir := filepath.Dir(reFPath)
		if os.MkdirAll(newdir, 0777) != nil {
			fmt.Printf("simenc: checkdedup newdir %s <create dir for newly added files>  error: %s, \n",newdir, err)
			return err
		}

		err = os.Rename(fpath, reFPath)
		if err != nil {
			fmt.Printf("simenc: error: fail to rename path (%v): %v \n", fpath, reFPath)
			return err
		}

		fpath = reFPath

		des = distribution.FileDescriptor{
			Digest:   dgst,
			FilePath: fpath,
			Size:     fsize,
		}
		*nodistributedfiles = append(*nodistributedfiles, des)
		return nil
	}
}

/*
bw.Uniqdistribution(ctx, dirSize, fcnt, nodistributedfiles, sliceSizeMap, slices, serverForwardMap)
*/
func (bw *blobWriter) Uniqdistribution(
	ctx context.Context,
	dirSize int64,
	fcnt int64,
	nodistributedfiles []distribution.FileDescriptor,
	sliceSizeMap map[string]int64,
	slices map[string][]distribution.FileDescriptor,
	serverForwardMap map[string][]string) bool {

	var nodistributedSize int64 = 0
	nodistributedfcnt := 0

	for _, f := range nodistributedfiles {
		nodistributedSize += f.Size
		nodistributedfcnt += 1
	}

	if dirSize <= bw.blobStore.registry.layerslicingdirsizethres ||
		nodistributedSize <= bw.blobStore.registry.layerslicingdirsizethres ||
		nodistributedfcnt <= bw.blobStore.registry.layerslicingfcntthres ||
		fcnt <= int64(bw.blobStore.registry.layerslicingfcntthres) {
		//no need to distribute *****
		for _, f := range nodistributedfiles {
			f.HostServerIp = bw.blobStore.registry.hostserverIp
			err := bw.blobStore.registry.metadataService.SetFileDescriptor(ctx, f.Digest, f)
			if err != nil {
				if err1 := os.Remove(f.FilePath); err1 != nil {
					fmt.Printf("simenc: Uniqdistribution: error %v \n", err1)
				}
				//add existing file to des slice
				if des, err := bw.blobStore.registry.metadataService.StatFile(ctx, f.Digest); err != nil {
					slices[des.HostServerIp] = append(slices[des.HostServerIp], des)
					sliceSizeMap[des.HostServerIp] += f.Size
				}
			} else {
				//add to this server
				slices[bw.blobStore.registry.hostserverIp] = append(slices[bw.blobStore.registry.hostserverIp], f)
				sliceSizeMap[bw.blobStore.registry.hostserverIp] += f.Size
			}
		}
		return true
	}

	// sort from big to small desend
	sort.Slice(nodistributedfiles, func(i, j int) bool {
		return nodistributedfiles[i].Size > nodistributedfiles[j].Size
	})
	//fmt.Printf("simenc: len(sliceSizeMap) = %d\n",len(sliceSizeMap))
	sss := make([]Pair, len(sliceSizeMap))
	i := 0

	for sip, size := range sliceSizeMap {
		sss[i] = Pair{sip, int64(size)}
		i += 1
	}

	for _, f := range nodistributedfiles {
		// each time, sort slice from small to big
		sort.Slice(sss, func(i, j int) bool {
			secondi, ok1 := sss[i].second.(int64)
			secondj, ok2 := sss[j].second.(int64)
			if ok1 && ok2 {
				return secondi < secondj
			} else {
				fmt.Printf("simenc: Uniqdistribution: cannot covert to int64 : %v, %v \n", ok1, ok2)
				return secondi < secondj
			}
		})

		HostServerIp, _ := sss[0].first.(string)
		f.HostServerIp = HostServerIp

		err := bw.blobStore.registry.metadataService.SetFileDescriptor(ctx, f.Digest, f)
		if err != nil {
			if err1 := os.Remove(f.FilePath); err1 != nil {
				fmt.Printf("simenc: Uniqdistribution: error %v \n", err1)
			}
			//add existing file to des slice
			if des, err := bw.blobStore.registry.metadataService.StatFile(ctx, f.Digest); err != nil {
				slices[des.HostServerIp] = append(slices[des.HostServerIp], des)
				sliceSizeMap[des.HostServerIp] += f.Size
				for i, item := range sss {
					ssssecond, ok1 := item.second.(int64)
					sssfirst, ok2 := item.first.(string)
					if !ok1 || !ok2 {
						fmt.Printf("simenc: Uniqdistribution: cannot covert to int64 and string : %v, %v \n", ok1, ok2)
					} else {
						if sssfirst == des.HostServerIp {
							ssssecond += f.Size
							sss[i].second = ssssecond
						}
					}
				}
			}

		} else {
			//add to smallest sip
			ssssecond, ok1 := sss[0].second.(int64)
			ssssecond += f.Size
			sssfirst, ok2 := sss[0].first.(string)
			if !ok1 || !ok2 {
				fmt.Printf("simenc: Uniqdistribution: cannot covert to int64 and string : %v, %v \n", ok1, ok2)
			}
			// biggest file to smallest bucket
			slices[sssfirst] = append(slices[sssfirst], f)
			sss[0].second = ssssecond

			if sssfirst != bw.blobStore.registry.hostserverIp {
				serverForwardMap[sssfirst] = append(serverForwardMap[sssfirst], f.FilePath)
			}
		}
	}

	for _, pelem := range sss {
		pelemfirst, ok1 := pelem.first.(string)
		pelemsecond, ok2 := pelem.second.(int64)
		if ok1 && ok1 {
			sliceSizeMap[pelemfirst] = pelemsecond
		} else {
			fmt.Printf("simenc: Uniqdistribution: cannot covert to string and int64 : %v, %v \n", ok1, ok2)
		}
	}

	return true
}

// Cancel the blob upload process, releasing any resources associated with
// the writer and canceling the operation.
func (bw *blobWriter) Cancel(ctx context.Context) error {
	context.GetLogger(ctx).Debug("(*blobWriter).Cancel")
	if err := bw.fileWriter.Cancel(); err != nil {
		return err
	}

	if err := bw.Close(); err != nil {
		context.GetLogger(ctx).Errorf("error closing blobwriter: %s", err)
	}

	if err := bw.removeResources(ctx); err != nil {
		return err
	}

	return nil
}

func (bw *blobWriter) Size() int64 {
	return bw.fileWriter.Size()
}

func (bw *blobWriter) Write(p []byte) (int, error) {
	// Ensure that the current write offset matches how many bytes have been
	// written to the digester. If not, we need to update the digest state to
	// match the current write position.
	if err := bw.resumeDigest(bw.blobStore.ctx); err != nil && err != errResumableDigestNotAvailable {
		return 0, err
	}

	n, err := io.MultiWriter(bw.fileWriter, bw.digester.Hash()).Write(p)
	bw.written += int64(n)

	return n, err
}

func (bw *blobWriter) ReadFrom(r io.Reader) (n int64, err error) {
	// Ensure that the current write offset matches how many bytes have been
	// written to the digester. If not, we need to update the digest state to
	// match the current write position.
	if err := bw.resumeDigest(bw.blobStore.ctx); err != nil && err != errResumableDigestNotAvailable {
		return 0, err
	}

	nn, err := io.Copy(io.MultiWriter(bw.fileWriter, bw.digester.Hash()), r)
	bw.written += nn

	return nn, err
}

func (bw *blobWriter) Close() error {
	if bw.committed {
		return errors.New("blobwriter close after commit")
	}

	if err := bw.storeHashState(bw.blobStore.ctx); err != nil && err != errResumableDigestNotAvailable {
		return err
	}

	return bw.fileWriter.Close()
}

// validateBlob checks the data against the digest, returning an error if it
// does not match. The canonical descriptor is returned.
func (bw *blobWriter) validateBlob(ctx context.Context, desc distribution.Descriptor) (distribution.Descriptor, error) {
	var (
		verified, fullHash bool
		canonical          digest.Digest
	)

	if desc.Digest == "" {
		// if no descriptors are provided, we have nothing to validate
		// against. We don't really want to support this for the registry.
		return distribution.Descriptor{}, distribution.ErrBlobInvalidDigest{
			Reason: fmt.Errorf("cannot validate against empty digest"),
		}
	}

	var size int64

	// Stat the on disk file
	if fi, err := bw.driver.Stat(ctx, bw.path); err != nil {
		switch err := err.(type) {
		case storagedriver.PathNotFoundError:
			// NOTE(stevvooe): We really don't care if the file is
			// not actually present for the reader. We now assume
			// that the desc length is zero.
			desc.Size = 0
		default:
			// Any other error we want propagated up the stack.
			return distribution.Descriptor{}, err
		}
	} else {
		if fi.IsDir() {
			return distribution.Descriptor{}, fmt.Errorf("unexpected directory at upload location %q", bw.path)
		}

		size = fi.Size()
	}

	if desc.Size > 0 {
		if desc.Size != size {
			return distribution.Descriptor{}, distribution.ErrBlobInvalidLength
		}
	} else {
		// if provided 0 or negative length, we can assume caller doesn't know or
		// care about length.
		desc.Size = size
	}

	// TODO(stevvooe): This section is very meandering. Need to be broken down
	// to be a lot more clear.

	if err := bw.resumeDigest(ctx); err == nil {
		canonical = bw.digester.Digest()

		if canonical.Algorithm() == desc.Digest.Algorithm() {
			// Common case: client and server prefer the same canonical digest
			// algorithm - currently SHA256.
			verified = desc.Digest == canonical
		} else {
			// The client wants to use a different digest algorithm. They'll just
			// have to be patient and wait for us to download and re-hash the
			// uploaded content using that digest algorithm.
			fullHash = true
		}
	} else if err == errResumableDigestNotAvailable {
		// Not using resumable digests, so we need to hash the entire layer.
		fullHash = true
	} else {
		return distribution.Descriptor{}, err
	}

	if fullHash {
		// a fantastic optimization: if the the written data and the size are
		// the same, we don't need to read the data from the backend. This is
		// because we've written the entire file in the lifecycle of the
		// current instance.
		if bw.written == size && digest.Canonical == desc.Digest.Algorithm() {
			canonical = bw.digester.Digest()
			verified = desc.Digest == canonical
		}

		// If the check based on size fails, we fall back to the slowest of
		// paths. We may be able to make the size-based check a stronger
		// guarantee, so this may be defensive.
		if !verified {
			digester := digest.Canonical.Digester()
			verifier := desc.Digest.Verifier()

			// Read the file from the backend driver and validate it.
			fr, err := newFileReader(ctx, bw.driver, bw.path, desc.Size)
			if err != nil {
				return distribution.Descriptor{}, err
			}
			defer fr.Close()

			tr := io.TeeReader(fr, digester.Hash())

			if _, err := io.Copy(verifier, tr); err != nil {
				return distribution.Descriptor{}, err
			}

			canonical = digester.Digest()
			verified = verifier.Verified()
		}
	}

	if !verified {
		context.GetLoggerWithFields(ctx,
			map[interface{}]interface{}{
				"canonical": canonical,
				"provided":  desc.Digest,
			}, "canonical", "provided").
			Errorf("canonical digest does match provided digest")
		return distribution.Descriptor{}, distribution.ErrBlobInvalidDigest{
			Digest: desc.Digest,
			Reason: fmt.Errorf("content does not match digest"),
		}
	}

	// update desc with canonical hash
	desc.Digest = canonical

	if desc.MediaType == "" {
		desc.MediaType = "application/octet-stream"
	}
	//fmt.Println("simenc: func (bw *blobWriter) validateBlob return ",desc)
	return desc, nil
}

// moveBlob moves the data into its final, hash-qualified destination,
// identified by dgst. The layer should be validated before commencing the
// move.
func (bw *blobWriter) moveBlob(ctx context.Context, desc distribution.Descriptor) error {
	blobPath, err := pathFor(blobDataPathSpec{
		digest: desc.Digest,
	})

	if err != nil {
		return err
	}

	// Check for existence
	if _, err := bw.blobStore.driver.Stat(ctx, blobPath); err != nil {
		switch err := err.(type) {
		case storagedriver.PathNotFoundError:
			break // ensure that it doesn't exist.
		default:
			return err
		}
	} else {
		// If the path exists, we can assume that the content has already
		// been uploaded, since the blob storage is content-addressable.
		// While it may be corrupted, detection of such corruption belongs
		// elsewhere.
		return nil
	}

	// If no data was received, we may not actually have a file on disk. Check
	// the size here and write a zero-length file to blobPath if this is the
	// case. For the most part, this should only ever happen with zero-length
	// blobs.
	if _, err := bw.blobStore.driver.Stat(ctx, bw.path); err != nil {
		switch err := err.(type) {
		case storagedriver.PathNotFoundError:
			// HACK(stevvooe): This is slightly dangerous: if we verify above,
			// get a hash, then the underlying file is deleted, we risk moving
			// a zero-length blob into a nonzero-length blob location. To
			// prevent this horrid thing, we employ the hack of only allowing
			// to this happen for the digest of an empty blob.
			if desc.Digest == digestSha256Empty {
				return bw.blobStore.driver.PutContent(ctx, blobPath, []byte{})
			}

			// We let this fail during the move below.
			logrus.
				WithField("upload.id", bw.ID()).
				WithField("digest", desc.Digest).Warnf("attempted to move zero-length content with non-zero digest")
		default:
			return err // unrelated error
		}
	}

	// TODO(stevvooe): We should also write the mediatype when executing this move.
	//fmt.Println("simenc: func (bw *blobWriter) moveBlob return")
	return bw.blobStore.driver.Move(ctx, bw.path, blobPath)
}

// removeResources should clean up all resources associated with the upload
// instance. An error will be returned if the clean up cannot proceed. If the
// resources are already not present, no error will be returned.
func (bw *blobWriter) removeResources(ctx context.Context) error {
	dataPath, err := pathFor(uploadDataPathSpec{
		name: bw.blobStore.repository.Named().Name(),
		id:   bw.id,
	})

	if err != nil {
		return err
	}

	// Resolve and delete the containing directory, which should include any
	// upload related files.
	dirPath := path.Dir(dataPath)
	if err := bw.blobStore.driver.Delete(ctx, dirPath); err != nil {
		switch err := err.(type) {
		case storagedriver.PathNotFoundError:
			break // already gone!
		default:
			// This should be uncommon enough such that returning an error
			// should be okay. At this point, the upload should be mostly
			// complete, but perhaps the backend became unaccessible.
			context.GetLogger(ctx).Errorf("unable to delete layer upload resources %q: %v", dirPath, err)
			return err
		}
	}
	//fmt.Println("simenc: func (bw *blobWriter) removeResources return nil")
	return nil
}

func (bw *blobWriter) Reader() (io.ReadCloser, error) {
	// todo(richardscothern): Change to exponential backoff, i=0.5, e=2, n=4
	try := 1
	for try <= 5 {
		_, err := bw.driver.Stat(bw.ctx, bw.path)
		if err == nil {
			break
		}
		switch err.(type) {
		case storagedriver.PathNotFoundError:
			context.GetLogger(bw.ctx).Debugf("Nothing found on try %d, sleeping...", try)
			time.Sleep(1 * time.Second)
			try++
		default:
			return nil, err
		}
	}

	readCloser, err := bw.driver.Reader(bw.ctx, bw.path, 0)
	if err != nil {
		return nil, err
	}

	return readCloser, nil
}
