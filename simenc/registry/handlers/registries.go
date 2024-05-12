package handlers

import (
	//"encoding/json"
	"net/http"

	//log "github.com/Sirupsen/logrus"
	//"github.com/docker/simenc"
	"github.com/docker/simenc/context"
	"github.com/docker/simenc/registry/api/errcode"
	//"github.com/docker/simenc/registry/api/v2"
	"github.com/gorilla/handlers"
	//"github.com/opencontainers/go-digest"
)

// blobDispatcher uses the request context to build a blobHandler.
func registriesDispatcher(ctx *Context, r *http.Request) http.Handler {

	registriesHandler := &registriesHandler{
		Context: ctx,
	}

	return handlers.MethodHandler{
		"GET": http.HandlerFunc(registriesHandler.GetRegistries),
	}
}

// blobHandler serves http blob requests.
type registriesHandler struct {
	*Context
}

/*type registriesAPIResponse struct {
	Registries []string
}*/

// GetBlob fetches the binary data from backend storage returns it in the
// response.
func (rh *registriesHandler) GetRegistries(w http.ResponseWriter, r *http.Request) {
	//blobs := bh.Repository.Blobs(bh)
	//log.Warnf("LITTLEY: handler registries.go GetRegistries")

	if err := rh.App.registry.URLWriter(rh, w, r); err != nil {
		context.GetLogger(rh).Debugf("unexpected error getting Registry HTTP handler: %v", err)
		rh.Errors = append(rh.Errors, errcode.ErrorCodeUnknown.WithDetail(err))
		return
	}
	/*w.Header().Set("Content-Type", "application/json; charset=utf-8")
	enc := json.NewEncoder(w)
	registries := make([]string, 0)
	if err := enc.Encode(registriesAPIResponse{
		Registries: registries,
	}); err != nil {
		context.GetLogger(rh).Debugf("unexpected error getting Registry HTTP handler: %v", err)
		rh.Errors = append(rh.Errors, errcode.ErrorCodeUnknown.WithDetail(err))
	}*/
}
