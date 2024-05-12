package distributed

import (
	"bufio"
	"bytes"
	"encoding/hex"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"os"
	"path"
	"reflect"
	"strconv"
	"strings"
	"time"

	consistentHash "github.com/nnzhaocs/consistenthash_sha256"
	log "github.com/sirupsen/logrus"
	"github.com/docker/simenc/context"
	storagedriver "github.com/docker/simenc/registry/storage/driver"
	"github.com/docker/simenc/registry/storage/driver/base"
	"github.com/docker/simenc/registry/storage/driver/factory"
	"launchpad.net/gozk/zookeeper"
)

const (
	driverName           = "distributed"
	defaultRootDirectory = "/home/simenc/docker_v2"
	defaultMaxThreads    = uint64(100)

	// minThreads is the minimum value for the maxthreads configuration
	// parameter. If the driver's parameters are less than this we set
	// the parameters to minThreads
	minThreads          = uint64(25)
	defaultRegistryName = "localhost:5005"
	//	defaultCacheSize      = uint64(8589934592)
	//	defaultCacheSizeLimit = uint64(10485760)
	defaultZookeeperName = "localhost:2181"
	defaultRedirect      = false
)

// DriverParameters represents all configuration options available for the
// filesystem driver
type DriverParameters struct {
	RootDirectory string
	MaxThreads    uint64
	RegistryName  string
	//CacheSize      uint64
	//CacheSizeLimit uint64
	ZookeeperName string
	Redirect      bool
}

func init() {
	factory.Register(driverName, &filesystemDriverFactory{})
}

// filesystemDriverFactory implements the factory.StorageDriverFactory interface
type filesystemDriverFactory struct{}

func (factory *filesystemDriverFactory) Create(parameters map[string]interface{}) (storagedriver.StorageDriver, error) {
	return FromParameters(parameters)
}

type driver struct {
	rootDirectory string
	ch            *consistentHash.ConsistentHash
	thisRegistry  string
	redirect      bool
}

type baseEmbed struct {
	base.Base
}

// Driver is a storagedriver.StorageDriver implementation backed by a local
// filesystem. All provided paths will be subpaths of the RootDirectory.
type Driver struct {
	baseEmbed
}

// FromParameters constructs a new Driver with a given parameters map
// Optional Parameters:
// - rootdirectory
// - maxthreads
func FromParameters(parameters map[string]interface{}) (*Driver, error) {
	params, err := fromParametersImpl(parameters)
	if err != nil || params == nil {
		return nil, err
	}
	return New(*params), nil
}

func fromParametersImpl(parameters map[string]interface{}) (*DriverParameters, error) {
	var (
		err           error
		maxThreads    = defaultMaxThreads
		rootDirectory = defaultRootDirectory
		registryName  = defaultRegistryName
		//cacheSize      = defaultCacheSize
		//cacheSizeLimit = defaultCacheSizeLimit
		zookeeperName = defaultZookeeperName
		redirect      = defaultRedirect
	)

	if parameters != nil {
		if rootDir, ok := parameters["rootdirectory"]; ok {
			rootDirectory = fmt.Sprint(rootDir)
		}
		if name, ok := parameters["registryname"]; ok {
			registryName = fmt.Sprint(name)
		}
		if name, ok := parameters["zookeepername"]; ok {
			zookeeperName = fmt.Sprint(name)
		}

		// Get maximum number of threads for blocking filesystem operations,
		// if specified
		threads := parameters["maxthreads"]
		switch v := threads.(type) {
		case string:
			if maxThreads, err = strconv.ParseUint(v, 0, 64); err != nil {
				return nil, fmt.Errorf("maxthreads parameter must be an integer, %v invalid", threads)
			}
		case uint64:
			maxThreads = v
		case int, int32, int64:
			val := reflect.ValueOf(v).Convert(reflect.TypeOf(threads)).Int()
			// If threads is negative casting to uint64 will wrap around and
			// give you the hugest thread limit ever. Let's be sensible, here
			if val > 0 {
				maxThreads = uint64(val)
			}
		case uint, uint32:
			maxThreads = reflect.ValueOf(v).Convert(reflect.TypeOf(threads)).Uint()
		case nil:
			// do nothing
		default:
			return nil, fmt.Errorf("invalid value for maxthreads: %#v", threads)
		}

		//Get cache size if specified
		/*cSize := parameters["cachesize"]
		switch v := cSize.(type) {
		case string:
			if cacheSize, err = strconv.ParseUint(v, 0, 64); err != nil {
				return nil, fmt.Errorf("cachesize parameter must be an integer, %v invalid", cSize)
			}
		case uint64:
			cacheSize = v
		case int, int32, int64:
			val := reflect.ValueOf(v).Convert(reflect.TypeOf(cSize)).Int()
			// If threads is negative casting to uint64 will wrap around and
			// give you the hugest thread limit ever. Let's be sensible, here
			if val > 0 {
				cacheSize = uint64(val)
			}
		case uint, uint32:
			cacheSize = reflect.ValueOf(v).Convert(reflect.TypeOf(cSize)).Uint()
		case nil:
			// do nothing
		default:
			return nil, fmt.Errorf("invalid value for maxthreads: %#v", cSize)
		}*/

		//Get redirect if specified
		r := parameters["redirect"]
		switch v := r.(type) {
		case string:
			if r == "true" {
				redirect = true
			} else if r == "false" {
				redirect = false
			} else {
				return nil, fmt.Errorf("invalid value for redirect: %#v", r)
			}
		case bool:
			redirect = v
		case nil:
			// do nothing
		default:
			return nil, fmt.Errorf("invalid value for redirect: %#v", r)
		}

		//Get max cache entry size
		/*cLim := parameters["cachesizelimit"]
		switch v := cLim.(type) {
		case string:
			if cacheSizeLimit, err = strconv.ParseUint(v, 0, 64); err != nil {
				return nil, fmt.Errorf("cachesizelimit parameter must be an integer, %v invalid", cLim)
			}
		case uint64:
			cacheSizeLimit = v
		case int, int32, int64:
			val := reflect.ValueOf(v).Convert(reflect.TypeOf(cLim)).Int()
			// If threads is negative casting to uint64 will wrap around and
			// give you the hugest thread limit ever. Let's be sensible, here
			if val > 0 {
				cacheSizeLimit = uint64(val)
			}
		case uint, uint32:
			cacheSizeLimit = reflect.ValueOf(v).Convert(reflect.TypeOf(cLim)).Uint()
		case nil:
			// do nothing
		default:
			return nil, fmt.Errorf("invalid value for maxthreads: %#v", threads)
		}*/

		if maxThreads < 1 {
			maxThreads = 0
		}

		/*if cacheSize < 1 {
			cacheSize = 0
		}

		if cacheSizeLimit < 1 {
			cacheSizeLimit = 0
		}*/
	}

	params := &DriverParameters{
		RootDirectory: rootDirectory,
		MaxThreads:    maxThreads,
		RegistryName:  registryName,
		//CacheSize:      cacheSize,
		//CacheSizeLimit: cacheSizeLimit,
		ZookeeperName: zookeeperName,
		Redirect:      redirect,
	}
	return params, nil
}

/*
Returns two lists, first of items in old list that aren't in newlist,
meaning they should be removed from the old list, which in this case is the
consistent hash function. The second list returned is the list of items in the
new list that aren't in the old list, which are items that should be added to
the consistent hash
*/
func difference(newlist, oldlist []string) ([]string, []string) {
	var remove, add []string

	//Find new items
	for _, s1 := range newlist {
		present := false
		for _, s2 := range oldlist {
			if s1 == s2 {
				present = true
				break
			}
		}
		if present == false {
			add = append(add, s1)
		}
	}

	//Find removed items
	for _, s1 := range oldlist {
		present := false
		for _, s2 := range newlist {
			if s1 == s2 {
				present = true
				break
			}
		}
		if present == false {
			remove = append(remove, s1)
		}
	}
	return remove, add
}

func (d *driver) check_and_forward(thisReg string, registries []string) error {
	time.Sleep(1 * time.Second) // Give registry time to start
	log.Debugf("%v %s", registries, thisReg)
	if len(registries) < 1 {
		return nil
	}

	files, err := ioutil.ReadDir(d.fullPath("/docker/registry/v2/repositories/test_repo/_layers/sha256/"))
	log.Warnf("CHECK AND FORWARD: checking %d files", len(files))
	if err != nil {
		return err
	}
	var rmlist []string
	for _, file := range files {
		dgst := file.Name()
		log.Debugf("CHECKING	%s", dgst)
		var buffer bytes.Buffer
		buffer.WriteString(d.fullPath("/docker/registry/v2/blobs/sha256"))
		buffer.WriteString("/")
		buffer.WriteString(dgst[:2])
		buffer.WriteString("/")
		buffer.WriteString(dgst)
		buffer.WriteString("/data")
		blobFile := buffer.String()
		_, err := os.Stat(blobFile)
		if err != nil {
			log.Debugf("ERROR	%v", err)
			continue
		}

		regList, err := d.ch.GetReplicaNodes(dgst)
		if err != nil {
			continue
		}
		if regList[1] == thisReg {
			for _, r := range registries {
				if regList[0] == r {
					err = forwardToRegistry(r, dgst, blobFile)
					if err != nil {
						log.Warnf("Forward Failed! %v", err)
					}
					break
				}
			}
		} else {
			del := true
			for _, r := range regList {
				if r == thisReg {
					del = false
					break
				}
			}
			if del {
				rmlist = append(rmlist, blobFile)
			}
		}
	}
	log.Warnf("Done Checking Files")

	//Remove files sent to registry
	for _, blobFile := range rmlist {
		os.Remove(blobFile)
	}
	return nil
}

/*
This function is designed to be run from a goroutine. It connects to a specified
zookeeper hostname (which can be a comma separated list). If the zookeeper
doesn't have a /registry node it will create one and then create an ephemeral node
under /registry of this registries name/port. It will then watch for children of
/registry and update the consistent hash as registries are added or removed from
/registry.
If an error occurs it will send it to reporter.
*/
func (d *driver) watcher(zookeeperName string, registryName string, reporter chan error) {
	log.Debugf("Zookeeper: Attempting to connect to %s", zookeeperName)
	zk, session, err := zookeeper.Dial(zookeeperName, 5e9)
	if err != nil {
		reporter <- err
		return
	}
	defer zk.Close()
	//Time Wait for connection to Zookeeper to be established, make configurable?
	select {
	case event := <-session:
		if event.State != zookeeper.STATE_CONNECTED {
			reporter <- fmt.Errorf("Zookeeper: Can't connect: %v", event)
			return
		}
		log.Debugf("Zookeeper: Connected")
	case <-time.After(30 * time.Second):
		reporter <- fmt.Errorf("Zookeeper connection timeout")
		return
	}

	//Create Registry node if it does not exist
	_, _, err = zk.Get("/registry")
	if err != nil {
		_, err = zk.Create("/registry", "", 0, zookeeper.WorldACL(zookeeper.PERM_ALL))
		if err != nil {
			reporter <- fmt.Errorf("Zookeeper: Can't create /registry: %v", err)
			return
		}
		fmt.Println("/registry created!")
	}

	self := fmt.Sprintf("/registry/%s", registryName)
	_, err = zk.Create(self, "", zookeeper.EPHEMERAL, zookeeper.WorldACL(zookeeper.PERM_ALL))
	if err != nil {
		reporter <- fmt.Errorf("Zookeeper: Can't create %s: %v", self, err)
		return
	}
	log.Warnf("Zookeeper: %s created", self)
	reporter <- nil

	//Change Consistent hash table based on changes in zookeeper entries
	for {
		registries, _, sig, err := zk.ChildrenW("/registry")
		log.Warnf("Zookeeper in for loop")
		if err != nil {
			log.Errorf("Zookeeper ChildrenW error: %v\n", err) //don't know how to handle this
		} else {
			log.Warnf("Registries: %v", registries)
			remove, add := difference(registries, d.ch.GetNodes())
			//Add nodes:
			for _, newreg := range add {
				d.ch.AddNode(newreg)
				fmt.Printf("Adding %s\n", newreg)
			}
			for _, downreg := range remove {
				d.ch.InvalidateNode(downreg)
				fmt.Printf("Removing %s\n", downreg)
			}
			go d.check_and_forward(registryName, add)
		}
		<-sig
	}
}

/*
This function is used to forward put requests on to other registries along the chain
*/
func forwardToRegistry(regname, dgst, path string) error {
	log.Warnf("Littley: forwarding %s to %s", regname, dgst)
	var buffer bytes.Buffer
	buffer.WriteString("http://")
	buffer.WriteString(regname)
	buffer.WriteString("/v2/test_repo/blobs/sha256:")
	buffer.WriteString(dgst)
	url := buffer.String()

	//Send Get Request
	head, err := http.Head(url)
	if err != nil {
		return err
	}
	head.Body.Close()

	if head.StatusCode == 404 {
		buffer.Reset()
		buffer.WriteString("http://")
		buffer.WriteString(regname)
		buffer.WriteString("/v2/test_repo/blobs/uploads/")
		url = buffer.String()

		post, err := http.Post(url, "*/*", nil)
		if err != nil {
			return err
		}
		post.Body.Close()

		location := post.Header.Get("location")
		buffer.Reset()
		buffer.WriteString(location)
		buffer.WriteString("&digest=sha256%3A")
		buffer.WriteString(dgst)
		url = buffer.String()
		fi, err := os.Stat(path)
		if err != nil {
			return err
		}
		file, err := os.Open(path)

		request, err := http.NewRequest("PUT", url, file)
		if err != nil {
			return err
		}

		request.ContentLength = fi.Size()
		client := &http.Client{}
		put, err := client.Do(request)
		if err != nil {
			return err
		}
		if put.StatusCode < 200 || put.StatusCode > 299 {
			return fmt.Errorf("%s returned status code %d", regname, put.StatusCode)
		}
		put.Body.Close()

	}

	return nil
}

/*
This function is used to get layers from other registries for lazy population
*/
func (d *driver) getFromRegistry(regname, dgst, subPath string) error {
	fullPath := d.fullPath(subPath)
	var buffer bytes.Buffer
	buffer.WriteString(fullPath)
	buffer.WriteString("_temp")
	tempFile := buffer.String()
	buffer.Reset()
	_, err := os.Stat(tempFile)
	if err == nil {
		return fmt.Errorf("Another request is loading File")
	}
	if err := os.MkdirAll(path.Dir(tempFile), 0755); err != nil {
		return err
	}
	log.Warnf("Requesting %s from %s and storing at %s", dgst, regname, subPath)
	//Build URLHeadRegistry(regname, dgst)
	buffer.WriteString("http://")
	buffer.WriteString(regname)
	buffer.WriteString("/v2/test_repo/blobs/sha256:")
	buffer.WriteString(dgst)
	url := buffer.String()

	//Send Get Request
	resp, err := http.Get(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode > 299 {
		return fmt.Errorf("%s returned status code %d", regname, resp.StatusCode)
	}

	fp, err := os.OpenFile(fullPath, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return err
	}
	out := newFileWriter(fp, 0)

	_, err = io.Copy(out, resp.Body)
	out.Close()
	if err != nil {
		os.Remove(tempFile)
		return err
	}
	return os.Rename(tempFile, fullPath)

}

/*
check for existance in registry
*/

/*
This function is used to check for layers in other registries
*/
func headFromRegistry(regname, dgst string) error {
	log.Warnf("Head Request for %s to %s", dgst, regname)
	//Build URLHeadRegistry(regname, dgst)
	var buffer bytes.Buffer
	buffer.WriteString("http://")
	buffer.WriteString(regname)
	buffer.WriteString("/v2/test_repo/blobs/sha256:")
	buffer.WriteString(dgst)
	url := buffer.String()

	//Send Get Request
	resp, err := http.Head(url)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode > 299 {
		return fmt.Errorf("%s returned status code %d", regname, resp.StatusCode)
	}
	return nil
}

// New constructs a new Driver with a given rootDirectory
func New(params DriverParameters) *Driver {

	c := consistentHash.New()
	c.AddNode(params.RegistryName)
	reporter := make(chan error)

	fsDriver := &driver{
		rootDirectory: params.RootDirectory,
		ch:            c,
		redirect:      params.Redirect,
		thisRegistry:  params.RegistryName,
	}

	//Spawn the watcher to connect to zookeeper
	go fsDriver.watcher(params.ZookeeperName, params.RegistryName, reporter)
	err := <-reporter
	if err != nil {
		panic(err) //Panic since we can't return an error
	}
	return &Driver{
		baseEmbed: baseEmbed{
			Base: base.Base{
				StorageDriver: base.NewRegulator(fsDriver, params.MaxThreads),
			},
		},
	}
}

// Implement the storagedriver.StorageDriver interface

func (d *driver) Name() string {
	return driverName
}

//returns digest from path name
func getDigestFromPath(path string) (string, bool) {
	dirs := strings.Split(path, "/")
	isBlob := false
	for _, s := range dirs {
		a, err := hex.DecodeString(s)
		if err == nil && len(a) == 32 {
			return s, isBlob
		} else if s == "blobs" {
			isBlob = true
		}
	}
	return "", false
}

// GetContent retrieves the content stored at "path" as a []byte.
func (d *driver) GetContent(ctx context.Context, path string) ([]byte, error) {
	//log.Warnf("IBM: Get content %s", path)
	rc, err := d.Reader(ctx, path, 0)
	if err != nil {
		return nil, err
	}
	defer rc.Close()

	p, err := ioutil.ReadAll(rc)
	if err != nil {
		return nil, err
	}

	return p, nil
}

// PutContent stores the []byte content at a location designated by "path".
func (d *driver) PutContent(ctx context.Context, subPath string, contents []byte) error {
	//log.Warnf("IBM: Put content %s", subPath)
	writer, err := d.Writer(ctx, subPath, false)
	if err != nil {
		return err
	}
	defer writer.Close()
	_, err = io.Copy(writer, bytes.NewReader(contents))
	if err != nil {
		writer.Cancel()
		return err
	}
	return writer.Commit()
}

// Reader retrieves an io.ReadCloser for the content stored at "path" with a
// given byte offset.
func (d *driver) Reader(ctx context.Context, path string, offset int64) (io.ReadCloser, error) {
	//log.Warnf("IBM: MBL Reading %s from offset %d", path, offset)
	fullpath := d.fullPath(path)
	file, err := os.OpenFile(fullpath, os.O_RDONLY, 0644)
	if err != nil {

		if os.IsNotExist(err) {
			//Check to see if blob
			dgst, isBlob := getDigestFromPath(fullpath)
			if dgst != "" {
				var buffer bytes.Buffer
				buffer.WriteString("/docker/registry/v2/blobs/sha256/")
				buffer.WriteString(dgst[:2])
				buffer.WriteString("/")
				buffer.WriteString(dgst)
				buffer.WriteString("/data")
				blobFile := buffer.String()

				_, err := d.Stat(ctx, blobFile) //Check if Blob exists
				if err != nil {
					return nil, err
				}
				if isBlob {
					file, err = os.OpenFile(fullpath, os.O_RDONLY, 0644)
					if err != nil {
						return nil, err
					}
				} else {
					buffer.Reset()
					buffer.WriteString("sha256:")
					buffer.WriteString(dgst)
					link := buffer.Bytes()
					err := d.PutContent(ctx, path, link)
					if err != nil {
						return nil, err
					}
					file, err = os.OpenFile(fullpath, os.O_RDONLY, 0644)
					if err != nil {
						return nil, err
					}
				}
			} else {
				return nil, storagedriver.PathNotFoundError{Path: path}
			}
		} else {
			return nil, err
		}
	}

	seekPos, err := file.Seek(int64(offset), os.SEEK_SET)
	if err != nil {
		file.Close()
		return nil, err
	} else if seekPos < int64(offset) {
		file.Close()
		return nil, storagedriver.InvalidOffsetError{Path: fullpath, Offset: offset}
	}

	return file, nil
}

func (d *driver) Writer(ctx context.Context, subPath string, append bool) (storagedriver.FileWriter, error) {
	fullPath := d.fullPath(subPath)
	parentDir := path.Dir(fullPath)

	//log.Warnf("IBM: MBL Writer %s", fullPath)
	if err := os.MkdirAll(parentDir, 0777); err != nil {
		return nil, err
	}

	fp, err := os.OpenFile(fullPath, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return nil, err
	}

	var offset int64

	if !append {
		err := fp.Truncate(0)
		if err != nil {
			fp.Close()
			return nil, err
		}
	} else {
		n, err := fp.Seek(0, os.SEEK_END)
		if err != nil {
			fp.Close()
			return nil, err
		}
		offset = int64(n)
	}

	return newFileWriter(fp, offset), nil
}

// Stat retrieves the FileInfo for the given path, including the current size
// in bytes and the creation time.
func (d *driver) Stat(ctx context.Context, subPath string) (storagedriver.FileInfo, error) {
	//log.Warnf("IBM: Stat %s", subPath)
	fullPath := d.fullPath(subPath)
	//log.Warnf("CTX %v\n", ctx)
	fi, err := os.Stat(fullPath)
	if err != nil {
		if os.IsNotExist(err) {

			//Check to see if blob
			dgst, isBlob := getDigestFromPath(fullPath)
			if dgst != "" && isBlob == true {

				//Check if master or slave
				registries, _ := d.ch.GetReplicaNodes(dgst)
				if len(registries) > 1 {

					//Is master?
					if d.thisRegistry == registries[0] {
						return nil, storagedriver.PathNotFoundError{Path: subPath}
					} else { //if not master
						// Is it a slave
						slave := false
						for _, r := range registries {
							if r == d.thisRegistry {
								slave = true
								break
							}
						}

						if slave == true {
							err = d.getFromRegistry(registries[0], dgst, subPath) //get blob from master
							if err != nil {
								log.Warnf("%v", err)
								return nil, storagedriver.PathNotFoundError{Path: subPath}
							}

							fi, err = os.Stat(fullPath)
							if err != nil {
								return nil, storagedriver.PathNotFoundError{Path: subPath}
							}

						} else { //slave != true
							return nil, storagedriver.PathNotFoundError{Path: subPath}
						}
					}
				} else { //if len(registries) < 1
					return nil, storagedriver.PathNotFoundError{Path: subPath}
				}
			} else { //if dgst == ""
				return nil, storagedriver.PathNotFoundError{Path: subPath}
			}
		} else {
			return nil, err
		}
	}

	return fileInfo{
		path:     subPath,
		FileInfo: fi,
	}, nil
}

// List returns a list of the objects that are direct descendants of the given
// path.
func (d *driver) List(ctx context.Context, subPath string) ([]string, error) {
	fullPath := d.fullPath(subPath)
	//log.Warnf("IBM: List %s", fullPath)
	dir, err := os.Open(fullPath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, storagedriver.PathNotFoundError{Path: subPath}
		}
		return nil, err
	}

	defer dir.Close()

	fileNames, err := dir.Readdirnames(0)
	if err != nil {
		return nil, err
	}

	keys := make([]string, 0, len(fileNames))
	for _, fileName := range fileNames {
		keys = append(keys, path.Join(subPath, fileName))
	}

	return keys, nil
}

func forwardRegistries(registryList []string, dgst, path string) {
	for _, registry := range registryList {
		log.Warnf("Forwarding %s to %s", dgst, registry)
		go forwardToRegistry(registry, dgst, path)
	}
}

// Move moves an object stored at sourcePath to destPath, removing the original
// object.
func (d *driver) Move(ctx context.Context, sourcePath string, destPath string) error {
	//log.Warnf("IBM: Move %s to %s", sourcePath, destPath)
	//Check if digest belongs to registry, raise error, do nothing if not digest
	dgst, isBlob := getDigestFromPath(destPath)
	var reglist []string
	if dgst != "" && isBlob == true {
		reglist, _ = d.ch.GetReplicaNodes(dgst)
		log.Warnf("Registry List of %s: %v", dgst, reglist)
		good := false
		for _, reg := range reglist {
			if reg == d.thisRegistry {
				good = true
				break
			}
		}
		if good == false {
			os.Remove(sourcePath) //remove content
			return fmt.Errorf("Registry Not Responsible for %s", dgst)
		}
	}
	source := d.fullPath(sourcePath)
	dest := d.fullPath(destPath)

	if _, err := os.Stat(source); os.IsNotExist(err) {
		return storagedriver.PathNotFoundError{Path: sourcePath}
	}

	if err := os.MkdirAll(path.Dir(dest), 0755); err != nil {
		return err
	}

	err := os.Rename(source, dest)
	if err != nil {
		return err
	}

	if len(reglist) > 1 && reglist[0] == d.thisRegistry {
		forwardRegistries(reglist[1:], dgst, dest)
	}
	return nil
}

// Delete recursively deletes all objects stored at "path" and its subpaths.
func (d *driver) Delete(ctx context.Context, subPath string) error {

	//log.Warnf("IBM: Removing objects %s", subPath)
	fullPath := d.fullPath(subPath)

	_, err := os.Stat(fullPath)
	if err != nil && !os.IsNotExist(err) {
		return err
	} else if err != nil {
		return storagedriver.PathNotFoundError{Path: subPath}
	}

	err = os.RemoveAll(fullPath)
	return err
}

// URLFor returns a URL which may be used to retrieve the content stored at the given path.
// May return an UnsupportedMethodErr in certain StorageDriver implementations.
func (d *driver) URLFor(ctx context.Context, path string, options map[string]interface{}) (string, error) {
	//log.Warn("URLFor Beginning")
	dgst, _ := getDigestFromPath(path)
	var registries []string
	if dgst == "" {
		registries = d.ch.GetNodes()
	} else {
		registries, _ = d.ch.GetReplicaNodes(dgst)
	}
	var buffer bytes.Buffer
	for _, reg := range registries[:len(registries)-1] {
		buffer.WriteString(reg)
		buffer.WriteString(",")
	}
	buffer.WriteString(registries[len(registries)-1])
	ret := buffer.String()
	//log.Warnf("URLFor Registries: %s", ret)
	return ret, nil
}

// fullPath returns the absolute path of a key within the Driver's storage.
func (d *driver) fullPath(subPath string) string {
	return path.Join(d.rootDirectory, subPath)
}

type fileInfo struct {
	os.FileInfo
	path string
}

var _ storagedriver.FileInfo = fileInfo{}

// Path provides the full path of the target of this file info.
func (fi fileInfo) Path() string {
	return fi.path
}

// Size returns current length in bytes of the file. The return value can
// be used to write to the end of the file at path. The value is
// meaningless if IsDir returns true.
func (fi fileInfo) Size() int64 {
	if fi.IsDir() {
		return 0
	}

	return fi.FileInfo.Size()
}

// ModTime returns the modification time for the file. For backends that
// don't have a modification time, the creation time should be returned.
func (fi fileInfo) ModTime() time.Time {
	return fi.FileInfo.ModTime()
}

// IsDir returns true if the path is a directory.
func (fi fileInfo) IsDir() bool {
	return fi.FileInfo.IsDir()
}

type fileWriter struct {
	file      *os.File
	size      int64
	bw        *bufio.Writer
	closed    bool
	committed bool
	cancelled bool
}

func newFileWriter(file *os.File, size int64) *fileWriter {
	return &fileWriter{
		file: file,
		size: size,
		bw:   bufio.NewWriter(file),
	}
}

func (fw *fileWriter) Write(p []byte) (int, error) {
	if fw.closed {
		return 0, fmt.Errorf("already closed")
	} else if fw.committed {
		return 0, fmt.Errorf("already committed")
	} else if fw.cancelled {
		return 0, fmt.Errorf("already cancelled")
	}
	n, err := fw.bw.Write(p)
	fw.size += int64(n)
	return n, err
}

func (fw *fileWriter) Size() int64 {
	return fw.size
}

func (fw *fileWriter) Close() error {
	if fw.closed {
		return fmt.Errorf("already closed")
	}

	if err := fw.bw.Flush(); err != nil {
		return err
	}

	if err := fw.file.Sync(); err != nil {
		return err
	}

	if err := fw.file.Close(); err != nil {
		return err
	}
	fw.closed = true

	return nil
}

func (fw *fileWriter) Cancel() error {
	if fw.closed {
		return fmt.Errorf("already closed")
	}

	fw.cancelled = true
	fw.file.Close()
	return os.Remove(fw.file.Name())
}

func (fw *fileWriter) Commit() error {
	if fw.closed {
		return fmt.Errorf("already closed")
	} else if fw.committed {
		return fmt.Errorf("already committed")
	} else if fw.cancelled {
		return fmt.Errorf("already cancelled")
	}

	if err := fw.bw.Flush(); err != nil {
		return err
	}

	if err := fw.file.Sync(); err != nil {
		return err
	}

	fw.committed = true
	return nil
}
