package handlers

import (
	"fmt"
	"net/http"
	"strings"

	"github.com/docker/simenc"
	ctxu "github.com/docker/simenc/context"
	"github.com/docker/simenc/registry/api/errcode"
	"github.com/docker/simenc/registry/api/v2"
	"github.com/docker/simenc/registry/auth"
	digest "github.com/opencontainers/go-digest"
	"golang.org/x/net/context"
)

// Context should contain the request specific context for use in across
// handlers. Resources that don't need to be shared across handlers should not
// be on this object.
type Context struct {
	// App points to the application structure that created this context.
	*App
	context.Context

	// Repository is the repository for the current request. All requests
	// should be scoped to a single repository. This field may be nil.
	Repository distribution.Repository

	// Errors is a collection of errors encountered during the request to be
	// returned to the client API. If errors are added to the collection, the
	// handler *must not* start the response via http.ResponseWriter.
	Errors errcode.Errors

	urlBuilder *v2.URLBuilder

	// TODO(stevvooe): The goal is too completely factor this context and
	// dispatching out of the web application. Ideally, we should lean on
	// context.Context for injection of these resources.
}

// Value overrides context.Context.Value to ensure that calls are routed to
// correct context.
func (ctx *Context) Value(key interface{}) interface{} {
	return ctx.Context.Value(key)
}

func getName(ctx context.Context) (name string) {
	varsname := ctxu.GetStringValue(ctx, "vars.name")
	//tp := GetType(ctx)

//	newname := strings.ReplaceAll(varsname, "type", "")
//	newname = strings.ReplaceAll(newname, "usraddr", "")
//	newname = strings.ReplaceAll(newname, "reponame", "")
//
//	newname = strings.ReplaceAll(newname, "layer", "")
//	newname = strings.ReplaceAll(newname, "slice", "")
//	newname = strings.ReplaceAll(newname, "manifest", "")
//
//	newname = strings.ReplaceAll(newname, "preconstruct", "")
	//	if ok := strings.Contains(tp, "PRECONSTRUCT"); ok{
	//		mname := strings.ReplaceAll(varsname, "preconstruct", "")
//	fmt.Printf("simenc: clear reference var.name from getName %s: => %s \n", varsname, newname)
	//		return mname
	//	}

	return varsname //newname
}

//TYPE XXX USRADDR XXX REPONAME XXX lowercases
//manifest or layer?
func GetType(ctx context.Context) (name string) {
	varsname := ctxu.GetStringValue(ctx, "vars.name")
	//forward_repo/forward_repo
	if ok := strings.Contains(varsname, "forward_repo"); ok {
		fmt.Println("simenc: this is forwarding! ")
		return strings.ToUpper("forward_repo")
	}
	//	fmt.Println("simenc: varsname input: ", varsname)
	tmps := strings.Split(varsname, "usraddr")[0]
	if len(tmps) < 2 {
		fmt.Println("simenc: wrong input: ", tmps)
		return ""
	}
	rtype := strings.Split(tmps, "type")[1]
	return strings.ToUpper(rtype)
}

//usraddr
func GetUsrAddr(ctx context.Context) (name string) {
	varsname := ctxu.GetStringValue(ctx, "vars.name")

	if ok := strings.Contains(varsname, "forward_repo"); ok {
		fmt.Println("simenc: this is forwarding! ")
		return strings.ToUpper("forward_repo")
	}

	tmps := strings.Split(varsname, "reponame")[0]
	if len(tmps) < 2 {
		fmt.Println("simenc: wrong input: ", tmps)
		return ""
	}
	usraddr := strings.Split(tmps, "usraddr")[1]
	return usraddr
}

//reponame
func GetRepoName(ctx context.Context) (name string) {
	varsname := ctxu.GetStringValue(ctx, "vars.name")

	if ok := strings.Contains(varsname, "forward_repo"); ok {
		fmt.Println("simenc: this is forwarding! ")
		return strings.ToUpper("forward_repo")
	}
	tmps := strings.Split(varsname, "reponame")
	if len(tmps) < 2 {
		fmt.Println("simenc: wrong input: ", varsname)
		return ""
	}
	reponame := strings.Split(varsname, "reponame")[1]
	return reponame
}

func getReference(ctx context.Context) (reference string) {
	return ctxu.GetStringValue(ctx, "vars.reference")
}

var errDigestNotAvailable = fmt.Errorf("digest not available in context")

func getDigest(ctx context.Context) (dgst digest.Digest, err error) {
	dgstStr := ctxu.GetStringValue(ctx, "vars.digest")

	if dgstStr == "" {
		ctxu.GetLogger(ctx).Errorf("digest not available")
		return "", errDigestNotAvailable
	}

	d, err := digest.Parse(dgstStr)
	if err != nil {
		ctxu.GetLogger(ctx).Errorf("error parsing digest=%q: %v", dgstStr, err)
		return "", err
	}

	return d, nil
}

func getUploadUUID(ctx context.Context) (uuid string) {
	return ctxu.GetStringValue(ctx, "vars.uuid")
}

// getUserName attempts to resolve a username from the context and request. If
// a username cannot be resolved, the empty string is returned.
func getUserName(ctx context.Context, r *http.Request) string {
	username := ctxu.GetStringValue(ctx, auth.UserNameKey)

	// Fallback to request user with basic auth
	if username == "" {
		var ok bool
		uname, _, ok := basicAuth(r)
		if ok {
			username = uname
		}
	}

	return username
}
