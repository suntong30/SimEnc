package main

import (
	_ "net/http/pprof"

	"github.com/docker/simenc/registry"
	_ "github.com/docker/simenc/registry/auth/htpasswd"
	_ "github.com/docker/simenc/registry/auth/silly"
	_ "github.com/docker/simenc/registry/auth/token"
	_ "github.com/docker/simenc/registry/proxy"
	_ "github.com/docker/simenc/registry/storage/driver/azure"
	_ "github.com/docker/simenc/registry/storage/driver/filesystem"
	_ "github.com/docker/simenc/registry/storage/driver/gcs"
	_ "github.com/docker/simenc/registry/storage/driver/inmemory"
	_ "github.com/docker/simenc/registry/storage/driver/middleware/cloudfront"
	_ "github.com/docker/simenc/registry/storage/driver/middleware/redirect"
	_ "github.com/docker/simenc/registry/storage/driver/oss"
	_ "github.com/docker/simenc/registry/storage/driver/s3-aws"
	_ "github.com/docker/simenc/registry/storage/driver/s3-goamz"
	_ "github.com/docker/simenc/registry/storage/driver/swift"
	_ "github.com/docker/simenc/registry/storage/driver/distributed"
)


func main() {
	registry.RootCmd.Execute()
}
